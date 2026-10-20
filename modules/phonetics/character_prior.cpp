#include "../runtime/resource_access.h"
#include "pch.h"
#include <cmath>
#include <cstring>
#include <limits>
#include "character_prior.h"
#include "trace_log.h"

constexpr int ALPHABET_SIZE = 28;
constexpr int START_CHAR_ID = 1;	// <s>
constexpr int END_CHAR_ID = 28;		// </s>

// 数组大小 (从1开始索引，需要+1的空间)
constexpr uint32_t UNIGRAM_ARRAY_SIZE = 29;		// ALPHABET_SIZE + 1（字母表大小加一）
constexpr uint32_t BIGRAM_ARRAY_SIZE = 785;		// ALPHABET_SIZE * ALPHABET_SIZE + 1（字母表大小平方加一）
constexpr uint32_t TRIGRAM_ARRAY_SIZE = 21953;	// ALPHABET_SIZE * ALPHABET_SIZE * ALPHABET_SIZE + 1（字母表大小立方加一）

// 预计算表按 (bigram key, id3) 索引；bigram key 上界是 id1=id2=ALPHABET_SIZE 时的取值
constexpr uint32_t MAX_BIGRAM_KEY = ALPHABET_SIZE * ALPHABET_SIZE + ALPHABET_SIZE;
constexpr uint32_t ID_SLOT_COUNT = ALPHABET_SIZE + 1;
constexpr size_t LOG_TABLE_SIZE = static_cast<size_t>(MAX_BIGRAM_KEY + 1) * ID_SLOT_COUNT;

inline size_t logTableSlot(const uint32_t bigramKey, const int id3)
{
	return static_cast<size_t>(bigramKey) * ID_SLOT_COUNT + static_cast<size_t>(id3);
}

constexpr float MIN_LN_PROB = -23.025850929940457f; // ln(1e-10)

bool coral::phonetics::CharacterPrior::load(const std::filesystem::path &modelPath, const std::string &versionId, const std::string &identifier)
{
	if (!std::filesystem::exists(modelPath))
	{
		LOG_ERROR("Character language model file does not exist: " + modelPath.string());
		return false;
	}

	// 使用SharedMemoryMapper加载文件，传入正确的版本ID和标识符
	// Windows: 使用全局共享内存名称 + 文件路径
	// macOS/Linux: 第一个参数不重要(只用于记录),关键是第二个参数文件路径
#if defined(_WIN32) || defined(_MSC_VER)
	std::wstring mapName = hlp::buildSHNameW(versionId, "CharLM");
#else
	std::wstring mapName = modelPath.wstring(); // macOS/Linux: 用文件路径作为名称
#endif
	if (!mapper_.load(mapName, modelPath, identifier, coral::runtime::resource_access()))
	{
		LOG_ERROR("Failed to map character language model file: " + modelPath.string());
		return false;
	}

	const char *data = static_cast<const char *>(mapper_.data());
	size_t file_size = mapper_.size();

	// 检查文件大小
	if (file_size < sizeof(coral::storage::CharacterModelHeader))
	{
		LOG_ERROR("Character language model file is too small");
		return false;
	}

	// 读取文件头
	const coral::storage::CharacterModelHeader *header = reinterpret_cast<const coral::storage::CharacterModelHeader *>(data);

	// 验证魔数
	if (std::strncmp(header->magic, "CHAR_LM", 7) != 0)
	{
		LOG_ERROR("Invalid character language model file format");
		return false;
	}

	// 验证版本 (支持版本1和版本2)
	if (header->version != 1 && header->version != 2)
	{
		LOG_ERROR("Unsupported character language model version: " + std::to_string(header->version));
		return false;
	}

	// 验证字母表大小
	if (header->alphabet_size != ALPHABET_SIZE)
	{
		LOG_ERROR("Alphabet size mismatch: expected " + std::to_string(ALPHABET_SIZE) + ", actual " + std::to_string(header->alphabet_size));
		return false;
	}

	// 计算预期文件大小
	size_t headerSize = sizeof(coral::storage::CharacterModelHeader);
	size_t unigram_size = UNIGRAM_ARRAY_SIZE * sizeof(uint64_t);
	size_t bigram_size = BIGRAM_ARRAY_SIZE * sizeof(uint64_t);
	size_t trigram_size = TRIGRAM_ARRAY_SIZE * sizeof(uint64_t);
	size_t thresholdSize = 0;
	
	// 版本2包含拼音长度阈值数据
	if (header->version == 2)
	{
		thresholdSize = (MAX_PINYIN_LENGTH - MIN_PINYIN_LENGTH + 1) * sizeof(double);  // 长度2-10的阈值
	}
	
	size_t expectedSize = headerSize + unigram_size + bigram_size + trigram_size + thresholdSize;

	if (file_size != expectedSize)
	{
		LOG_ERROR("Character language model file size incorrect: expected " + std::to_string(expectedSize) + ", actual " + std::to_string(file_size));
		return false;
	}

	// 设置数据指针
	const char *currentPtr = data + headerSize;

	unigram_array_ = reinterpret_cast<const uint64_t *>(currentPtr);
	currentPtr += unigram_size;

	bigram_array_ = reinterpret_cast<const uint64_t *>(currentPtr);
	currentPtr += bigram_size;

	trigram_array_ = reinterpret_cast<const uint64_t *>(currentPtr);
	currentPtr += trigram_size;

	// 版本2包含拼音长度阈值数据
	if (header->version == 2)
	{
		pinyin_length_thresholds_ = reinterpret_cast<const double *>(currentPtr);
	}
	else
	{
		pinyin_length_thresholds_ = nullptr;
	}

	// 计算总计数
	total_unigram_count_ = 0;
	total_bigram_count_ = 0;
	total_trigram_count_ = 0;

	for (uint32_t i = 0; i < UNIGRAM_ARRAY_SIZE; ++i)
	{
		total_unigram_count_ += unigram_array_[i];
	}

	for (uint32_t i = 0; i < BIGRAM_ARRAY_SIZE; ++i)
	{
		total_bigram_count_ += bigram_array_[i];
	}

	for (uint32_t i = 0; i < TRIGRAM_ARRAY_SIZE; ++i)
	{
		total_trigram_count_ += trigram_array_[i];
	}

	loaded_ = true;
	build_trigram_log_table();

	LOG_INFO("Character language model loaded successfully: fileSize=" + std::to_string(file_size) +
			 " unigramTotal=" + std::to_string(total_unigram_count_) + " bigramTotal=" + std::to_string(total_bigram_count_) +
			 " trigramTotal=" + std::to_string(total_trigram_count_));

	return true;
}

int coral::phonetics::CharacterPrior::char_to_id(char c)
{
	if (c >= 'a' && c <= 'z')
	{
		return c - 'a' + 2;	 // a=2, b=3, ..., z=27 (从1开始的索引系统)
	}
	// 特殊字符需要在调用处处理：<s>=1, </s>=28
	return -1;
}

int coral::phonetics::CharacterPrior::char_to_id_with_special(char c) const
{
	if (c == '<')
		return START_CHAR_ID;  // <s> = 1
	if (c == '>')
		return END_CHAR_ID;	 // </s> = 28
	return char_to_id(c);
}

uint32_t coral::phonetics::CharacterPrior::compute_bigram_key(int id1, int id2)
{
	return static_cast<uint32_t>(id1 * ALPHABET_SIZE + id2);
}

uint32_t coral::phonetics::CharacterPrior::compute_trigram_key(int id1, int id2, int id3)
{
	return static_cast<uint32_t>(id1 * ALPHABET_SIZE * ALPHABET_SIZE + id2 * ALPHABET_SIZE + id3);
}

uint64_t coral::phonetics::CharacterPrior::get_unigram_count(char c) const
{
	if (!loaded_)
		return 0;

	int id = char_to_id(c);
	if (id < 0 || id >= static_cast<int>(UNIGRAM_ARRAY_SIZE))
	{
		return 0;
	}

	return unigram_array_[id];
}

uint64_t coral::phonetics::CharacterPrior::get_bigram_count(char c1, char c2) const
{
	if (!loaded_)
		return 0;

	int id1 = char_to_id_with_special(c1);
	int id2 = char_to_id_with_special(c2);

	if (id1 < 0 || id2 < 0 || id1 > static_cast<int>(ALPHABET_SIZE) || id2 > static_cast<int>(ALPHABET_SIZE))
	{
		return 0;
	}

	uint32_t key = compute_bigram_key(id1, id2);
	if (key >= BIGRAM_ARRAY_SIZE)
	{
		return 0;
	}

	return bigram_array_[key];
}

uint64_t coral::phonetics::CharacterPrior::get_trigram_count(char c1, char c2, char c3) const
{
	if (!loaded_)
		return 0;

	int id1 = char_to_id_with_special(c1);
	int id2 = char_to_id_with_special(c2);
	int id3 = char_to_id_with_special(c3);

	if (id1 < 0 || id2 < 0 || id3 < 0 || id1 > static_cast<int>(ALPHABET_SIZE) || id2 > static_cast<int>(ALPHABET_SIZE) ||
		id3 > static_cast<int>(ALPHABET_SIZE))
	{
		return 0;
	}

	uint32_t key = compute_trigram_key(id1, id2, id3);
	if (key >= TRIGRAM_ARRAY_SIZE)
	{
		return 0;
	}

	return trigram_array_[key];
}

void coral::phonetics::CharacterPrior::build_trigram_log_table()
{
	// 注意不能直接拿 trigram key 当表下标：id3 的取值上界（END_CHAR_ID）等于进位基数
	// ALPHABET_SIZE，同一个 trigram key 会对应到不同的二元上下文，而分母取的是二元计数。
	// 所以这里按 (bigram key, id3) 这对真正决定结果的量建表。
	trigram_log_prob_.assign(LOG_TABLE_SIZE, MIN_LN_PROB);
	trigram_valid_.assign(LOG_TABLE_SIZE, 0);
	for (int id1 = 0; id1 <= ALPHABET_SIZE; ++id1)
	{
		for (int id2 = 0; id2 <= ALPHABET_SIZE; ++id2)
		{
			const uint32_t bigramKey = compute_bigram_key(id1, id2);
			const uint64_t bigramCount = bigramKey < BIGRAM_ARRAY_SIZE ? bigram_array_[bigramKey] : 0;
			for (int id3 = 0; id3 <= ALPHABET_SIZE; ++id3)
			{
				const uint32_t trigramKey = compute_trigram_key(id1, id2, id3);
				const uint64_t trigramCount =
					trigramKey < TRIGRAM_ARRAY_SIZE ? trigram_array_[trigramKey] : 0;
				if (trigramCount == 0)
					continue;	// 保持平滑值 MIN_LN_PROB，且不计入 validTrigrams
				const size_t slot = logTableSlot(bigramKey, id3);
				if (bigramCount == 0)
				{
					// 三元有计数但二元没有：原实现既不累加也不计数
					trigram_log_prob_[slot] = 0.0f;
					continue;
				}
				const float prob = static_cast<float>(trigramCount) / static_cast<float>(bigramCount);
				trigram_log_prob_[slot] = std::logf(prob);
				trigram_valid_[slot] = 1;
			}
		}
	}
}

float coral::phonetics::CharacterPrior::calculate_log_prob(const std::string_view sequence) const
{
	if (!loaded_ || sequence.empty() || trigram_log_prob_.empty())
	{
		return -std::numeric_limits<float>::infinity();
	}

	// 扩展序列是 '<' + sequence + '>'，这里直接按下标虚拟出来，省掉每次调用一次堆分配
	const size_t n = sequence.size();
	const auto idAt = [&](const size_t i) -> int {
		if (i == 0)
			return START_CHAR_ID;
		if (i == n + 1)
			return END_CHAR_ID;
		return char_to_id_with_special(sequence[i - 1]);
	};

	float logProb = 0.0f;
	uint32_t validTrigrams = 0;
	int id1 = idAt(0);
	int id2 = idAt(1);
	for (size_t i = 0; i < n; ++i)
	{
		const int id3 = idAt(i + 2);
		float contribution = MIN_LN_PROB;
		if (id1 >= 0 && id2 >= 0 && id3 >= 0)
		{
			const size_t slot = logTableSlot(compute_bigram_key(id1, id2), id3);
			contribution = trigram_log_prob_[slot];
			validTrigrams += trigram_valid_[slot];
		}
		logProb += contribution;
		id1 = id2;
		id2 = id3;
	}

	// 如果没有有效的trigram，返回负无穷
	if (validTrigrams == 0)
	{
		return -std::numeric_limits<float>::infinity();
	}

	return logProb / validTrigrams;
}

float coral::phonetics::CharacterPrior::get_pinyin_length_threshold(int length) const
{
	if (!loaded_ || !pinyin_length_thresholds_)
	{
		return -std::numeric_limits<float>::infinity();
	}

	if (length < MIN_PINYIN_LENGTH || length > MAX_PINYIN_LENGTH)
	{
		return -std::numeric_limits<float>::infinity();
	}

	// 阈值数组索引：length-2 (长度2对应索引0，长度3对应索引1，...)
	int index = length - MIN_PINYIN_LENGTH;
	return static_cast<float>(pinyin_length_thresholds_[index]);
}

bool coral::phonetics::CharacterPrior::is_pinyin_above_threshold(const std::string_view pinyin_sequence, const float score) const
{
	if (!loaded_ || pinyin_sequence.empty())
	{
		return false;
	}

	int length = static_cast<int>(pinyin_sequence.length());
	
	// 长度小于2的直接返回true（不使用阈值检查）
	if (length < MIN_PINYIN_LENGTH)
	{
		return true;
	}

	// 长度大于MAX_PINYIN_LENGTH的也返回true（超出阈值范围）
	if (length > MAX_PINYIN_LENGTH)
	{
		return true;
	}

	float threshold = get_pinyin_length_threshold(length);
	if (threshold == -std::numeric_limits<float>::infinity())
	{
		return true;  // 没有阈值时默认接受
	}

	return score >= threshold;
}

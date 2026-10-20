/*
* @author: huangzhishou
* @date: 2021-5-28 18:02:25
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_LEXICON_SOURCE_H_INCLUDED
#define CORAL_MODULES_PHONETICS_LEXICON_SOURCE_H_INCLUDED

#include <thirdparty/parallel_hashmap/phmap.h>
#include <marisa.h>
#include "phonetic_sequence.hpp"
#include "support/parse_arena.hpp"
using phmap::flat_hash_map;

namespace coral::phonetics {
struct ResourceLocation
{
	std::filesystem::path path;
	std::string identifier;
};
} // 命名空间 coral::phonetics 结束（批次 3）


inline bool is_valid(const coral::phonetics::ResourceLocation& data)
{
	const bool isExists = std::filesystem::exists(data.path);
	if (!isExists)
	{
		LOG_ERROR("checkValidPath failed: the path is an invalid path:" + data.path.string());
		return false;
	}
	const bool isRegularFile = std::filesystem::is_regular_file(data.path);
	if (!isRegularFile)
	{
		LOG_ERROR("checkValidPath failed: the data file is not a regular file:" + data.path.string());
		return false;
	}
	const auto calculateFileMd5 = hlp::calculateFileMD5(data.path.string());
	const bool isValidMd5 = data.identifier == calculateFileMd5;
	if (!isValidMd5)
		LOG_ERROR("checkValidPath failed: the MD5 is mismatch:" + data.path.string()
			+ ", calculateFileMD5 is: " + calculateFileMd5
			+ ", the MD5 from config file is: " + data.identifier);
	return isValidMd5;
};

namespace coral::phonetics {
struct LexiconLocations
{
	coral::phonetics::ResourceLocation bushouTrie;
	coral::phonetics::ResourceLocation bushouData;
	coral::phonetics::ResourceLocation strokeTrie;
	coral::phonetics::ResourceLocation strokeData;
	// languagemodel.coral 二元资源；加载器按 magic 区分旧静态图与表面词模型。
	coral::phonetics::ResourceLocation bigramGBin;
	coral::phonetics::ResourceLocation englishData;
	coral::phonetics::ResourceLocation englishMarisa;
	coral::phonetics::ResourceLocation primaryData;
	coral::phonetics::ResourceLocation primaryMarisa;
	coral::phonetics::ResourceLocation correctionData;
	coral::phonetics::ResourceLocation charLMData;
	std::filesystem::path userLexiconFile;
	std::filesystem::path lexiconBaseDir;
	std::string versionId;

	bool check_valid_path()
	{
		if (!std::filesystem::exists(lexiconBaseDir) || !std::filesystem::is_directory(lexiconBaseDir))
		{
			return false;
		}
		const std::vector<std::reference_wrapper<const coral::phonetics::ResourceLocation>> filesToCheck = {
			bigramGBin,
			englishData,
			englishMarisa,
			primaryData,
			primaryMarisa,
			correctionData,
			charLMData,
		};
		return std::all_of(filesToCheck.begin(), filesToCheck.end(), [](const auto& file){
		    const coral::phonetics::ResourceLocation& p = file.get();
			return is_valid(p);
		});
	}
};
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
struct LexiconPhrase {
	LexiconPhrase() = default;
	LexiconPhrase(const string_view& phrase,
		const string_view& pinyin,
		coral::phonetics::Score weight,
		uint32_t phrase_id,
		coral::phonetics::Score backprob,
		bool freHighest = false,
		bool inM2 = false,
		bool english = false,
		bool corrected = false,
		const coral::phonetics::PersonalLexiconIndex idx = -1,
		const int isFixed = 0,
		const int tick = -1)
		: phrase(phrase)
		, phrase_pinyin(pinyin)
		, phrase_weight(weight)
		, id(phrase_id)
		, phrase_backprob(backprob)
		, b_fre_highest(freHighest)
		, b_in_m2(inM2)
		, b_english(english)
		, is_corrected(corrected)
		, index(idx)
		, is_fixed_candidate(isFixed)
		, tick(tick)
	{}
	coral::phonetics::TextSlice phrase;
	string phrase_pinyin;
	coral::phonetics::Score phrase_weight{};
	uint32_t id = kInvalidPhraseId;
	coral::phonetics::Score phrase_backprob{};
	coral::phonetics::PersonalLexiconIndex index{ -1 };
	int is_fixed_candidate{ 0 };
	int tick{ -1 };
	bool b_fre_highest{ false };
	bool b_in_m2{ false };
	bool b_english{ false };
	bool is_corrected{ false };

	// 拷贝/移动语义使用编译器隐式生成版本：与手写逐字段版本完全等价，
	// 隐式版本保持 noexcept 与可平凡性推导最优。
	// 注意：这里必须保留显式的移动构造/移动赋值声明，否则 std::sort 搬移元素时会深拷贝 phrasePinyin，排序是取词阶段的热点
	LexiconPhrase(const LexiconPhrase& obj) = default;
	LexiconPhrase(LexiconPhrase&& obj) noexcept = default;
	LexiconPhrase& operator=(const LexiconPhrase& obj) = default;
	LexiconPhrase& operator=(LexiconPhrase&& obj) noexcept = default;

	bool operator <(const LexiconPhrase& p) const {
		if (this->phrase_weight != p.phrase_weight) {
			return this->phrase_weight > p.phrase_weight;
		}
		else {
			return this->phrase > p.phrase;
		}
	}

	bool operator==(const LexiconPhrase& rhs) const {
		return (id == rhs.id &&  phrase == rhs.phrase && phrase_pinyin == rhs.phrase_pinyin && phrase_weight == rhs.phrase_weight && phrase_backprob == rhs.phrase_backprob && tick == rhs.tick);
	}
};
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
using ParsePhrases = std::vector<coral::phonetics::LexiconPhrase, coral::phonetics::support::PoolAllocator<coral::phonetics::LexiconPhrase>>;
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
struct PhraseTextHash {
	size_t operator()(const coral::phonetics::LexiconPhrase& p) const {
		return hash<string_view>()(p.phrase) ^ hash<string_view>()(p.phrase_pinyin) ^ hash<coral::phonetics::Score>()(p.phrase_weight) ^ hash<uint32_t>()(p.id) ^ hash<coral::phonetics::Score>()(p.phrase_backprob) ^ hash<int>()(p.tick);
	}
};
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
struct PhraseTextEqual {
	string_view phrase;
	explicit PhraseTextEqual(const string_view& w) :phrase(w) {}//构造函数
	bool operator()(const coral::phonetics::LexiconPhrase& p) const noexcept {//仿函数
		return (p.phrase.compare(phrase) == 0);
	}
};
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
class LexiconSource
{
public:
	LexiconSource(): max_len_(0) {}
	virtual ~LexiconSource() = default;
	LexiconSource(const LexiconSource&) = delete;
	LexiconSource& operator=(const LexiconSource&) = delete;
	LexiconSource(LexiconSource&&) = delete;	// 移动构造函数禁用，因为marisa::Trie不支持移动
	LexiconSource& operator=(LexiconSource&&) = delete;

	// 前缀搜索1次，搜索完毕所有结果时返回false
	virtual bool predictive_search(marisa::Agent& agent, const string& prefixKey, const string& prefixValue, coral::phonetics::ParsePhrases& phrases) = 0;

	virtual bool load(const coral::phonetics::ResourceLocation& trieFile, const coral::phonetics::ResourceLocation& infoFile, const string& versionId) = 0;

	virtual void reset_storage() = 0;

	virtual bool build_lexicon(
		const std::filesystem::path& rawDataPath,
		const std::filesystem::path& triePath,
		const std::filesystem::path& dataPath) = 0;

	[[nodiscard]] int max_length() const
	{
		return max_len_;
	}

	void set_max_length(const int len)
	{
		max_len_ = len;
	}

protected:
	int max_len_ = 0;
	marisa::Trie trie_;
	// 以下是对派生类对象的要求
	// LexiconView m_DataView 派生类需要有一个类似的成员,用来访问trie中的内存映射的数据
	// coral::storage::VerifiedResource m_trieMapper 是可选项,表示是否使用共享内存存储索引
	volatile bool loaded_ = false;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // LEXICONBASE_H

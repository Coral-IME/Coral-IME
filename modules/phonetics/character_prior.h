#pragma once
#ifndef CORAL_MODULES_PHONETICS_CHARACTER_PRIOR_H_INCLUDED
#define CORAL_MODULES_PHONETICS_CHARACTER_PRIOR_H_INCLUDED

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include "verified_resource.h"

#pragma pack(push, 1)
namespace coral::storage {
struct CharacterModelHeader
{
	char magic[8];			// "CHAR_LM\0"（字符语言模型魔数）
	uint32_t version;		// 版本号 (现在支持版本2)
	uint32_t alphabet_size;	// 字母表大小 (28)
};
} // 命名空间 coral::storage 结束（批次 3）

#pragma pack(pop)

/**
 * 字符级三元语言模型
 * 用于为拼音纠错提供字符级别的概率支持
 */
namespace coral::phonetics {
class CharacterPrior
{
public:
	CharacterPrior() = default;
	~CharacterPrior() = default;

	// 支持阈值的拼音长度限制(外部也会使用)
	static constexpr int MIN_PINYIN_LENGTH = 2;
	static constexpr int MAX_PINYIN_LENGTH = 16;

	/**
	 * 加载字符语言模型
	 * @param modelPath 模型文件路径
	 * @return 是否加载成功
	 */
	bool load(const std::filesystem::path &modelPath, const std::string &versionId, const std::string &identifier);

	/**
	 * 获取Unigram计数
	 * @param c 字符
	 * @return 计数值
	 */
	[[nodiscard]] uint64_t get_unigram_count(char c) const;

	/**
	 * 获取Bigram计数
	 * @param c1 第一个字符
	 * @param c2 第二个字符
	 * @return 计数值
	 */
	[[nodiscard]] uint64_t get_bigram_count(char c1, char c2) const;

	/**
	 * 获取Trigram计数
	 * @param c1 第一个字符
	 * @param c2 第二个字符
	 * @param c3 第三个字符
	 * @return 计数值
	 */
	[[nodiscard]] uint64_t get_trigram_count(char c1, char c2, char c3) const;

	/**
	 * 计算字符序列的对数概率,取音节平均值
	 * @param sequence 字符序列
	 * @return 对数概率
	 */
	[[nodiscard]] float calculate_log_prob(std::string_view sequence) const;

	/**
	 * 获取指定长度拼音的最小概率阈值
	 * @param length 拼音长度 (2-10)
	 * @return 最小概率阈值，如果长度无效返回负无穷
	 */
	[[nodiscard]] float get_pinyin_length_threshold(int length) const;

	/**
	 * 检查拼音字符串是否超过最小概率阈值
	 * @param pinyinSequence 拼音字符序列
	 * @return 是否超过阈值
	 */
	[[nodiscard]] bool is_pinyin_above_threshold(std::string_view pinyin_sequence, const float score) const;

	/**
	 * 检查模型是否已加载
	 * @return 是否已加载
	 */
	[[nodiscard]] bool is_loaded() const
	{
		return loaded_;
	}

private:
	/**
	 * 字符转ID
	 * @param c 字符
	 * @return 字符ID，-1表示无效字符
	 */
	[[nodiscard]] static int char_to_id(char c);

	/**
	 * 字符转ID（包含特殊字符处理）
	 * @param c 字符
	 * @return 字符ID，-1表示无效字符
	 */
	[[nodiscard]] int char_to_id_with_special(char c) const;

	/**
	 * 计算Bigram键值
	 * @param id1 第一个字符ID
	 * @param id2 第二个字符ID
	 * @return 键值
	 */
	[[nodiscard]] static uint32_t compute_bigram_key(int id1, int id2);

	/**
	 * 计算Trigram键值
	 * @param id1 第一个字符ID
	 * @param id2 第二个字符ID
	 * @param id3 第三个字符ID
	 * @return 键值
	 */
	[[nodiscard]] static uint32_t compute_trigram_key(int id1, int id2, int id3);

	// 每个 trigram 键的贡献是 load 时就定死的，纠错阶段每个候选拼音都要把整串扫一遍，
	// 与其反复做除法和 logf，不如在这里一次性把 21953 个键的结果摊平成表。
	void build_trigram_log_table();

private:
	bool loaded_ = false;
	coral::storage::VerifiedResource mapper_;

	std::vector<float> trigram_log_prob_;   // logf(trigram / bigram)，无贡献项为 0
	std::vector<uint8_t> trigram_valid_;   // 该键是否计入 validTrigrams

	// 模型数据指针
	const uint64_t *unigram_array_ = nullptr;
	const uint64_t *bigram_array_ = nullptr;
	const uint64_t *trigram_array_ = nullptr;
	const double *pinyin_length_thresholds_ = nullptr;  // 拼音长度阈值数组

	// 统计信息
	uint64_t total_unigram_count_ = 0;
	uint64_t total_bigram_count_ = 0;
	uint64_t total_trigram_count_ = 0;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // CHARLANGUAGEMODEL_H

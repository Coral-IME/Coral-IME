#pragma once
#ifndef CORAL_MODULES_PHONETICS_SPELLING_REPAIR_H_INCLUDED
#define CORAL_MODULES_PHONETICS_SPELLING_REPAIR_H_INCLUDED

#include <set>
#include <string>
#include <vector>
#include <marisa.h>
#include <unordered_map>
#include <utility>
#include "character_prior.h"
#include <syllable_helpers.h>
#include "marisa-trie/include/marisa/trie.h"
#include "verified_resource.h"

#pragma pack(push, 1)
namespace coral::storage {
struct SpellingModelHeader {
    char magic[8];
    uint32_t version;
    uint64_t trie_offset;
    uint64_t trie_size;
    uint64_t unigram_offset;
    uint64_t unigram_size;
    uint64_t bigram_offset;
    uint64_t bigram_size;
    uint64_t trigram_offset;
    uint64_t trigram_size;
};
} // 命名空间 coral::storage 结束（批次 3）


namespace coral::storage {
struct SpellingPairRecord {
    uint32_t key;
    uint64_t count;
};
} // 命名空间 coral::storage 结束（批次 3）


namespace coral::storage {
struct SpellingTripleRecord {
    uint64_t key;
    uint64_t count;
};
} // 命名空间 coral::storage 结束（批次 3）

#pragma pack(pop)

//KN算法的折扣参数类型
namespace coral::phonetics {
using Discount = float;
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
class SpellingRepair {
public:
    SpellingRepair() = default;
    bool load(const std::filesystem::path& modelPath, const std::string& versionId, const std::string& identifier);
	bool load_char_language_model(const std::filesystem::path &charLMPath, const std::string &versionId, const std::string &identifier);
	[[nodiscard]] std::pair<std::vector<std::string>, float> predict(const std::string &text, const coral::phonetics::syntax::CorrectionMethod& correctionMethod) const;
	std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>> generate_all_corrections_with_details(const std::string &pinyin, const std::vector<std::string>& originalSequence, bool useSmartCorrection);

private:
    [[nodiscard]] uint64_t get_bigram_count(uint16_t id1, uint16_t id2) const;
    [[nodiscard]] uint64_t get_trigram_count(uint16_t id1, uint16_t id2, uint16_t id3) const;
    [[nodiscard]] float get_trigram_log_prob(uint16_t id1, uint16_t id2, uint16_t id3) const;
	[[nodiscard]] uint64_t get_continuation_count1(uint16_t w) const;
    [[nodiscard]] uint64_t get_follow_count1(uint16_t w) const;
    [[nodiscard]] uint64_t get_follow_count2(uint16_t w1, uint16_t w2) const;
    [[nodiscard]] float get_kneser_ney_discount(uint64_t count) const;
	[[nodiscard]] float get_kneser_ney_trigram_log_prob(uint16_t id1, uint16_t id2, uint16_t id3) const;
    [[nodiscard]] float get_kneser_ney_bigram_prob(uint16_t id2, uint16_t id3) const;
    [[nodiscard]] float get_kneser_ney_unigram_prob(uint16_t id3) const;
    [[nodiscard]] float get_bigram_backoff(uint16_t id2, uint16_t id3) const;
    [[nodiscard]] std::string get_syllable_from_id(uint16_t id) const;
    [[nodiscard]] uint16_t get_syllable_id(const char* s) const;
	[[nodiscard]] int get_valid_initial_length(const std::string &pinyin);
	[[nodiscard]] bool check_correct_pinyin_valid(const std::string& pinyin, float originalScore, float& candidateScore) const;
	void generate_insertions(const std::string &pinyin, std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>> &corrections,
							float originalPinyinCharLMScore);
	void generate_deletions(const std::string &pinyin, std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>> &corrections,
							float originalPinyinCharLMScore);
	void generate_replacements(const std::string &pinyin, std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>> &corrections,
							float originalPinyinCharLMScore, const std::vector<std::string>& originalSequence);
	void generate_swaps(const std::string &pinyin, std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>> &corrections,
							float originalPinyinCharLMScore);
	void generate_must_corrections(const std::string &pinyin, std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>> &corrections,
							float originalPinyinCharLMScore);
	void build_must_correction_rules();
	void calculate_discount_parameters();
    void precompute_kneser_ney_counts();

private:
    const uint64_t *unigram_counts_ = nullptr;
    const coral::storage::SpellingPairRecord *bigram_data_ = nullptr;
    const coral::storage::SpellingTripleRecord *trigram_data_ = nullptr;
    size_t max_correct_key_length_ = 0;
    size_t vocab_size_ = 0;
    uint64_t total_unigram_count_ = 0;
    uint32_t bigram_count_ = 0;
    uint32_t trigram_count_ = 0;
    uint16_t start_id_ = 0;
    uint16_t end_id_ = 0;
    coral::phonetics::Discount discountlow_ = 0.0f;
    coral::phonetics::Discount discountmid_ = 0.0f;
    coral::phonetics::Discount discounthigh_ = 0.0f;
    marisa::Trie trie_;
    coral::storage::VerifiedResource mapper_;
    coral::phonetics::CharacterPrior char_lm_;
    std::unordered_map<std::string, std::set<std::string>> correct_map_;
    std::unordered_map<uint16_t, uint64_t> continuation_vocab_cache_;
    std::unordered_map<uint32_t, uint64_t> continuation_bigram_cache_;
    std::unordered_map<uint16_t, uint64_t> follow_count1_cache_;
    std::unordered_map<uint32_t, uint64_t> follow_count2_cache_;
    // 二元组概率缓存
    mutable std::unordered_map<uint32_t, float> bigram_prob_cache_;
    // 三元组概率缓存：定长直接映射，冲突直接覆盖。
    // 原来用 unordered_map 且写满 10000 条后就不再收录，纠错候选一多命中率就塌到两成，
    // 后面全在重算 Kneser-Ney（三次二分查找起步）。定长表既没有扩容和结点开销，
    // 也不会因为写满而永久退化。
    struct TrigramProbSlot
    {
        uint64_t key;
        float value;
    };
    static constexpr size_t TRIGRAM_CACHE_SLOTS = 1u << 16;
    static constexpr uint64_t TRIGRAM_CACHE_EMPTY = ~uint64_t{ 0 };
    mutable std::vector<TrigramProbSlot> trigram_prob_cache_;
    bool is_enable_correction_ = false;
    volatile bool loaded_ = false;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // PINYINCORRECTOR_H

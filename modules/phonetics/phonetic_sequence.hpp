#pragma once
#ifndef CORAL_MODULES_PHONETICS_PHONETIC_SEQUENCE_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_PHONETIC_SEQUENCE_HPP_INCLUDED

#include "phonetic_types.h"
#include "../runtime/runtime_context.h"	// 配置快照（limit_count）
#include <algorithm>
#include "support/parse_arena.hpp"
#include "support/parse_metrics.hpp"
#include <cstdlib>
namespace coral::phonetics {
struct PhoneticSequence
{
	PhoneticSequence() : from_(-1), to_(-1), path_nodes_(0), is_match_english_vocab_(false) {}

	PhoneticSequence(
		const coral::phonetics::ParseTextViews& sequences,
		const coral::phonetics::ParseTextViews& subSequences,
		const coral::phonetics::ParseTextViews& showSequences,
		int from, int to, int size, bool isMatchEnglishVocab)
		: sequences_(sequences)
		, sub_sequences_(subSequences)
		, show_sequences_(showSequences)
		, from_(from)
		, to_(to)
		, path_nodes_(size)
		, is_match_english_vocab_(isMatchEnglishVocab)
	{}

	PhoneticSequence(const coral::phonetics::ParseTextViews& sequences
		, const coral::phonetics::ParseTextViews& subSequences
		, const coral::phonetics::ParseTextViews& showSequences
		, const coral::phonetics::ParseTextViews& originalSequences
		, const int from
		, const int to
		, const int size
		, const bool isMatchEnglishVocab
		, const bool isFuzzyMatch
		, const bool isAbbreviatehasFull)
		: sequences_(sequences)
		, sub_sequences_(subSequences)
		, original_sequences_(originalSequences)
		, show_sequences_(showSequences)
		, from_(from)
		, to_(to)
		, path_nodes_(size)
		, is_match_english_vocab_(isMatchEnglishVocab)
		, is_fuzzy_match_(isFuzzyMatch)
		, is_abbreviatehas_full_(isAbbreviatehasFull)
	{}

	PhoneticSequence(
		const coral::phonetics::ParseTextViews& sequences,
		const coral::phonetics::ParseTextViews& subSequences,
		int from, int to, int size, bool isMatchEnglishVocab ,bool isAbbreviatehasFull)
		: sequences_(sequences)
		, sub_sequences_(subSequences)
		, from_(from)
		, to_(to)
		, path_nodes_(size)
		, is_match_english_vocab_(isMatchEnglishVocab)
		, is_abbreviatehas_full_(isAbbreviatehasFull)
	{}

	PhoneticSequence(const PhoneticSequence& obj)
	: sequences_(obj.sequences_)
	, sub_sequences_(obj.sub_sequences_)
	, original_sequences_(obj.original_sequences_)
	, show_sequences_(obj.show_sequences_)
	{
		from_ = obj.from_;
		to_ = obj.to_;
		path_nodes_ = obj.path_nodes_;
		is_match_english_vocab_ = obj.is_match_english_vocab_;
		is_fuzzy_match_ = obj.is_fuzzy_match_;
		is_edit_corrected_ = obj.is_edit_corrected_;
		is_abbreviatehas_full_ = obj.is_abbreviatehas_full_;
	}

	PhoneticSequence(PhoneticSequence&& obj) noexcept
	: sequences_(std::move(obj.sequences_))
	, sub_sequences_(std::move(obj.sub_sequences_))
	, original_sequences_(std::move(obj.original_sequences_))
	, show_sequences_(std::move(obj.show_sequences_))
	{
		from_ = obj.from_;
		to_ = obj.to_;
		path_nodes_ = obj.path_nodes_;
		is_match_english_vocab_ = obj.is_match_english_vocab_;
		is_fuzzy_match_ = obj.is_fuzzy_match_;
		is_edit_corrected_ = obj.is_edit_corrected_;
		is_abbreviatehas_full_ = obj.is_abbreviatehas_full_;
	}

	const PhoneticSequence& operator =(const PhoneticSequence& obj) {
		if (this != &obj) {
			from_ = obj.from_;
			to_ = obj.to_;
			path_nodes_ = obj.path_nodes_;
			is_match_english_vocab_ = obj.is_match_english_vocab_;
			is_fuzzy_match_ = obj.is_fuzzy_match_;
			is_edit_corrected_ = obj.is_edit_corrected_;
			is_abbreviatehas_full_ = obj.is_abbreviatehas_full_;
			sequences_ = obj.sequences_;
			sub_sequences_ = obj.sub_sequences_;
			show_sequences_ = obj.show_sequences_;
			original_sequences_ = obj.original_sequences_;
		}
		return *this;
	}

	PhoneticSequence& operator =(PhoneticSequence&& obj) noexcept
	{
		from_ = obj.from_;
		to_ = obj.to_;
		path_nodes_ = obj.path_nodes_;
		is_match_english_vocab_ = obj.is_match_english_vocab_;
		is_fuzzy_match_ = obj.is_fuzzy_match_;
		is_edit_corrected_ = obj.is_edit_corrected_;
		is_abbreviatehas_full_ = obj.is_abbreviatehas_full_;
		sequences_ = std::move(obj.sequences_);
		sub_sequences_ = std::move(obj.sub_sequences_);
		show_sequences_ = std::move(obj.show_sequences_);
		original_sequences_ = std::move(obj.original_sequences_);
		return *this;
	}


	const coral::phonetics::ParseTextViews& get_ui_show_pinyin_sequence() const {
		if (show_sequences_.empty()) {
			return sequences_;
		}
		return show_sequences_;
	}

	const int get_pinyin_length() const {
		if (is_match_english_vocab_)
			return static_cast<int>(sequences_.front().size());
		if (!is_match_english_vocab_ || original_sequences_.empty()) {
			return (int)sequences_.size();
		}
		if (sequences_.size() > original_sequences_.size()) {
			return (int)sequences_.size();
		}
		return (int)original_sequences_.size();
	}

#ifdef _DEBUG
	std::string to_debug_string() const
	{
		std::string msg;
		msg.reserve(5 * sequences_.size() + 2);
		msg.push_back('[');
		for (size_t i = 0; i < sequences_.size(); ++i)
		{
			if (i > 0)
				msg.push_back('\'');
			msg += std::string(sequences_[i]);
		}
		msg.push_back(']');
		return msg;
	}
#endif

#if LANGENGINE_PERF_COUNTERS
	// 实验开关：把所有 span 的取词上限压到 CORAL_SPAN_CAP，用来量"截断到第 N 条"对输出的影响
	int get_limit() const {
		const int base = get_limit_impl();
		static const int cap = [] {
			const char* env = std::getenv("CORAL_SPAN_CAP");
			return env ? std::atoi(env) : 0;
		}();
		if (cap > 0 && (base < 0 || base > cap))
			return cap;
		return base;
	}

	int get_limit_impl() const {
#else
	int get_limit() const {
#endif
		/* 控制取词数策略：zhong'hua'ren'min
		 * 0. 模糊音限制取词数为5
		 * 1. 混拼不限制取词数（例：z'guo'r → z'g'r）
		 * 2. 若词图路径节点过长（≥6），限制中间 span，保留开头完整音节的候选长尾
		 * 3. 从头开始的简拼：
		 *    - 若拼音数 = 2，3，则放宽限制为60，30
		 *    - 否则限制
		 * 4. 非从头开始的简拼，统一限制
		 * 5. 非从头开始的全拼且为单音节，限制取词
		 * 6. 纠错目前不做限制，限制对性能影响不大，但可能因为限制导致部分纠错后的音节查询到的候选词较少，使语言模型找不到某些词导致解析失败
		 * 7. 其他情况不做限制
		 */
		const int defaultLimit = coral::Engine::compatInstance().config().limit_count();
		if (is_fuzzy_match_) {
			if(from_ == 0)
			{
				if (std::any_of(sequences_.begin(), sequences_.end(), [](const auto& sequence) {
					return coral::phonetics::syntax::is_jian_pin(sequence);
				})) {
					return 5;
				}
				return sequences_.size() == 1 ? defaultLimit : -1;
			}
			return 5;
		}
		if (!sub_sequences_.empty()) {
			// 混拼不限制取词，确保在词典文件中的词组能够被召回
			return -1;
		}
		// 长输入仍限制中间 span，避免词格膨胀；但从输入开头命中的首个完整
		// 音节是候选长尾的入口，不能跟着总路径长度一起被压到 limit_count。
		// 例如 nishibushisha 必须继续保留 ni 的完整候选，而不是只剩十余项。
		const bool isLeadingFullSyllable = from_ == 0 && sequences_.size() == 1
			&& !coral::phonetics::syntax::is_jian_pin(sequences_.front());
		if (path_nodes_ >= 6 && !isLeadingFullSyllable) {
			return defaultLimit;
		}
		// 排除全拼串的情况
		for (auto it = sequences_.cbegin();
			it != sequences_.cend(); ++it) 
		{
			if (coral::phonetics::syntax::is_jian_pin(*it)) 
			{
				if (from_ == 0) 
				{
					// 从头开始的简拼长度判断
					if (sequences_.size() == 1) {
						// 单个声母固定预取候选窗的 10 页，不随通用 limit_count 波动。
						return kSingleJianpinCandidateLimit;
					}else if (sequences_.size() == 2) {
						// 限制为默认限制的五倍
						return defaultLimit * 5;
					}else if (sequences_.size() == 3) {
						// 限制为默认限制的三倍
						return defaultLimit * 3;
					}
					return defaultLimit;
				}
				// 非从头开始的简拼，统一限制
				return defaultLimit;
			}
		}

		// 非从头开始的单全拼
		if (from_ != 0 && sequences_.size() == 1) 
		{
			return defaultLimit;
		}

		return -1;
	}
	// 存储实际的查询码
	coral::phonetics::ParseTextViews sequences_;
	// 子码、辅码，针对混拼的情况，例如 m_sequences 是 z'guo'r（中国人）的情况下，其需要查询 z'g'r 的子码
	coral::phonetics::ParseTextViews sub_sequences_;
	// 存储最原始的查询码，例如 mamgg 切分后 m_originalSequences是 ma'm'g'g 纠正后mSubsequences是 m'g, m_sequences是 mang'g, m_showSequences 是 mamg'g
	// 实际查询码不是纠正过的，改变量为空
	coral::phonetics::ParseTextViews original_sequences_;
	// 实际查询码不是纠正过的，改变量为空
	coral::phonetics::ParseTextViews show_sequences_;
	int from_;
	int to_;
	// 拼音查询码对应的拼音解析路径节点数
	int path_nodes_;
	// 是否能匹配到英文单词
	bool is_match_english_vocab_;
	// 是否是模糊音匹配而来的，这将影响到后续的限制取词
	bool is_fuzzy_match_ = false;
	// 方案B第二阶段：是否由编辑距离纠错边（swap/delete/insert/replace）产生，
	// 影响词弧惩罚与全句命中剪枝的触发资格
	bool is_edit_corrected_ = false;
	// 是否是有全拼拼音的简拼,如da'ban'g，这将影响到后续的限制取词
	bool is_abbreviatehas_full_ = false;
};
} // 命名空间 coral::phonetics 结束（批次 2）


// arena 分配的拼音序列容器（单次解析生命周期）
namespace coral::phonetics {
using ParseSequences = std::vector<coral::phonetics::PhoneticSequence, coral::phonetics::support::PoolAllocator<coral::phonetics::PhoneticSequence>>;
} // 命名空间 coral::phonetics 结束（批次 3）


#endif // PINYINSEQUENCE_HPP（拼音序列头文件）

#pragma once
#ifndef CORAL_MODULES_PHONETICS_CANDIDATE_RESULT_H_INCLUDED
#define CORAL_MODULES_PHONETICS_CANDIDATE_RESULT_H_INCLUDED

#include "phonetic_types.h"

enum class MiddlewareType {
	Default = 0,      // 默认汉字
	NeedConvert = 1 << 0,  // 繁简需要转化的中间件，如日期
	NoConvert = 1 << 1,     // 繁简不需要转化的中间件，如emoji
	NoStoreUserLexicon = 1 << 2  //某些词条不需要落盘
};

inline MiddlewareType operator|(MiddlewareType a, MiddlewareType b) {
	return static_cast<MiddlewareType>(
		static_cast<unsigned int>(a) | static_cast<unsigned int>(b)
		);
}

inline MiddlewareType operator&(MiddlewareType a, MiddlewareType b) {
	return static_cast<MiddlewareType>(
		static_cast<unsigned int>(a) & static_cast<unsigned int>(b)
		);
}

namespace coral::phonetics {
class CandidateResult
{
public:
	CandidateResult() = default;
	CandidateResult(CandidateResult&& other) noexcept;

	// 防止sort不能用
	CandidateResult& operator=(CandidateResult&& other) noexcept;
	// 删除拷贝构造函数 拷贝赋值运算符
	CandidateResult(const CandidateResult&) = delete;
	CandidateResult& operator=(const CandidateResult&) = delete;

	void reset();
	void set_probability_with_weight(coral::phonetics::Score weight);
	int word_size{};

public:
	//Solution因为要保留上次解析的内容，所以不能使用string_view优化
	std::string sentence;
	// 可选的带注音候选标签；sentence 始终保持为上屏文本。
	std::string display_sentence;
	// Solution 会被 move 进 m_sentsCache 跨解析存活，pinyinview 不能用 arena 分配器
	coral::phonetics::PersistentTextViews pinyinview;
	std::vector<std::string> pinyin;
	MiddlewareType transform_flags{ MiddlewareType::Default };
	uint32_t phrase_id{ kInvalidPhraseId };
	coral::phonetics::Score probability{};
	coral::phonetics::PersonalLexiconIndex index{ -1 };
	int is_fixed_candidate{ 0 };
	bool is_corrected{ false };
	// 附加的混合句子可被选中，但不得改变纠错门控。
	bool is_additional_mixed{ false };
};
} // 命名空间 coral::phonetics 结束（批次 3）


#endif // SOLUTION_H

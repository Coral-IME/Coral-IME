#include <limits>
#include <utility>
#include <vector>
#include "candidate_result.h"

coral::phonetics::CandidateResult::CandidateResult(coral::phonetics::CandidateResult&& other) noexcept:
	word_size(other.word_size),
	sentence(std::move(other.sentence)),
	display_sentence(std::move(other.display_sentence)),
	pinyinview(std::move(other.pinyinview)),
	pinyin(std::move(other.pinyin)),
	transform_flags(other.transform_flags),
	phrase_id(other.phrase_id),
	probability(other.probability),
	index(other.index),
	is_fixed_candidate(other.is_fixed_candidate),
	is_corrected(other.is_corrected),
	is_additional_mixed(other.is_additional_mixed)
{
	other.word_size = 0;
	other.transform_flags = MiddlewareType::Default;;
	other.phrase_id = kInvalidPhraseId;
	other.probability = 0.0;
	other.index = -1;
	other.is_additional_mixed = false;
}

void coral::phonetics::CandidateResult::reset()
{
	sentence = "";
	display_sentence.clear();
	probability = 0.0;
	word_size = 0;
	pinyin.clear();
	pinyinview.clear();
	transform_flags = MiddlewareType::Default;
	phrase_id = kInvalidPhraseId;
	index = -1;
	is_fixed_candidate = 0;
	is_corrected = false;
	is_additional_mixed = false;
}

void coral::phonetics::CandidateResult::set_probability_with_weight(coral::phonetics::Score weight)
{
	probability *= weight;
}

coral::phonetics::CandidateResult& coral::phonetics::CandidateResult::CandidateResult::operator=(coral::phonetics::CandidateResult&& other) noexcept
{
	if (this != &other)
	{
		sentence = std::move(other.sentence);
		display_sentence = std::move(other.display_sentence);
		pinyin = std::move(other.pinyin);
		pinyinview = std::move(other.pinyinview);
		word_size = other.word_size;
		transform_flags = other.transform_flags;
		phrase_id = other.phrase_id;
		probability = other.probability;
		index = other.index;
		is_fixed_candidate = other.is_fixed_candidate;
		is_corrected = other.is_corrected;
		is_additional_mixed = other.is_additional_mixed;
		// 三个容器成员已经被 move 走，标准保证它们处于空状态，
		// 这里只补齐标量字段；调 reset() 会再对空串空 vector 做一遍无谓的清空。
		other.word_size = 0;
		other.transform_flags = MiddlewareType::Default;
		other.phrase_id = kInvalidPhraseId;
		other.probability = 0.0;
		other.index = -1;
		other.is_fixed_candidate = 0;
		other.is_corrected = false;
		other.is_additional_mixed = false;
	}
	return *this;
}

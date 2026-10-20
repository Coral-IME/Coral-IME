/*
* @author: s_yanxiaoren
* @date: 2025-6-16
* @brief: Vertex和Edge的模版实例化
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_LATTICE_PAYLOAD_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_LATTICE_PAYLOAD_HPP_INCLUDED

#include "support/object_pool.hpp"
#include "directed_lattice.hpp"
#include "phonetic_types.h"
#include "phonetic_sequence.hpp"

namespace coral::phonetics {
struct CandidateEdgePayload;
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
struct CandidateEdgePayload
{
	CandidateEdgePayload() = default;
	~CandidateEdgePayload() = default;

	coral::phonetics::TextSlice phrase;
	uint32_t phrase_id = kInvalidPhraseId;
	int id = -1;
	// 一个查询序列会产出成百条弧，且这些弧的拼音序列完全相同。这里只引用共享副本，
	// 副本由 CWordLattice 用引用稳定的容器持有（见 CWordLattice::shareSequence），
	// 生命周期与词格一致；逐弧深拷贝 4 个 arena 向量曾是取词阶段的主要开销。
	const coral::phonetics::PhoneticSequence* sequence = nullptr;
	coral::phonetics::Score weight = kLogInfiniteSimal;
	bool is_corrected{ false };
	bool english_context_only = false;
	CandidateEdgePayload(const CandidateEdgePayload& other) = delete;
	CandidateEdgePayload(CandidateEdgePayload && other) noexcept = default;
	CandidateEdgePayload& operator=(const CandidateEdgePayload & other) = delete;
	CandidateEdgePayload& operator=(CandidateEdgePayload && other) noexcept = default;

	[[nodiscard]] const coral::phonetics::PhoneticSequence& pinyin_sequence() const noexcept { return *sequence; }

	// 词与拼音齐备才是一条能参与解码的弧
	[[nodiscard]] bool is_usable() const noexcept
	{
		return !phrase.empty() && sequence != nullptr && !sequence->sequences_.empty();
	}

	void emplace(const string_view& str,
		uint32_t phrase_id,
		int i,
		const coral::phonetics::PhoneticSequence& sharedSequence,
		coral::phonetics::Score w,
		bool corrected = false) noexcept
	{
		phrase = str;
		this->phrase_id = phrase_id;
		id = i;
		sequence = &sharedSequence;
		weight = w;
		is_corrected = corrected;
	}
};
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
struct SyllableEdgePayload;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
struct SyllableVertexPayload;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
using SyllableEdge = coral::phonetics::LatticeEdge<coral::phonetics::SyllableEdgePayload>;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
using SyllableVertex = coral::phonetics::LatticeVertex<coral::phonetics::SyllableVertexPayload>;
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
struct SyllableVertexPayload
{
	SyllableVertexPayload() = default;
	~SyllableVertexPayload() = default;
	int compact_id = -1;
#ifdef _DEBUG
	string word;
#endif
	SyllableVertexPayload(const SyllableVertexPayload& other) = default;
	SyllableVertexPayload(SyllableVertexPayload&& other) noexcept = default;
	SyllableVertexPayload& operator=(const SyllableVertexPayload& other) = default;
	SyllableVertexPayload& operator=(SyllableVertexPayload&& other) noexcept = default;
};
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
struct SyllableEdgePayload
{
	SyllableEdgePayload() = default;
	~SyllableEdgePayload() = default;
	
	int weight{100};
	coral::phonetics::TextSlice phrase;
	

	// ===== 方案B：纠错边扩展 =====
	// 第一阶段（模糊音）：isFuzzyEdge 为 true 时，phrase 是模糊替换后的查询音节
	// （指向 CPinyinModel 模糊表的内存，跨解析稳定，可安全作为 string_view 存入图中），
	// originalPhrase 是该边对应的原始输入切分子串（指向本次输入串，仅解析期内有效）。
	// 路径组装时用两者分别拼查询序列与展示/原序列，并置 m_isFuzzyMatch。
	bool is_fuzzy_edge = false;
	coral::phonetics::TextSlice original_phrase;
	// 第二阶段（编辑距离纠错）：编辑边。对原始输入窗口做单次编辑（swap/delete/
	// insert/replace）后能形成合法音节时挂的平行边，同样参与 Yen KSP 统一排序。
	bool is_edit_edge = false;
	bool is_english_edge = false;
	int edit_kind = 0;	// static_cast<int>(coral::phonetics::syntax::CorrectionType)，仅 isEditEdge 时有效
	int edit_index = -1;	// 编辑操作发生在原始输入串中的字符下标

	explicit SyllableEdgePayload(const coral::phonetics::TextSlice& str) : phrase(str) {}
	SyllableEdgePayload(const coral::phonetics::TextSlice& str, const int w) : phrase(str), weight(w) {}
	
	SyllableEdgePayload(const SyllableEdgePayload& other) = default;
	SyllableEdgePayload(SyllableEdgePayload&& other) noexcept = default;
	SyllableEdgePayload& operator=(const SyllableEdgePayload& other) = default;
	SyllableEdgePayload& operator=(SyllableEdgePayload&& other) noexcept = default;
};
} // 命名空间 coral::phonetics 结束（批次 3）


#endif // GRAPHDATA_HPP（图数据头文件）

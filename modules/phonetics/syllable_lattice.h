/*
* @author: huangzhishou
* @date: 2021-5-28 17:57:04
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_SYLLABLE_LATTICE_H_INCLUDED
#define CORAL_MODULES_PHONETICS_SYLLABLE_LATTICE_H_INCLUDED

#include "directed_lattice.hpp"
#include "lattice_payload.hpp"
#include "phonetic_sequence.hpp"
#include "shortest_paths.hpp"


namespace coral::phonetics {
class SyllableLattice;
} // 命名空间 coral::phonetics 结束（批次 2）

namespace coral::phonetics {
class CandidateLattice;
} // 命名空间 coral::phonetics 结束（批次 2）

namespace coral::phonetics {
class LexiconCatalog;
} // 命名空间 coral::phonetics 结束（批次 2）


// ===== 方案B第一阶段：模糊音表的外部只读视图 =====
// 由 CPinyinModel 在解析入口组装并传入。指针为空表示本次解析不启用图内模糊边
// （模糊功能关闭，或 CORAL_FUZZY_IN_GRAPH=0 走旧 fuzzyReplace 路径）。
// 指向的内存属于 CPinyinModel 成员（buildFuzzyMap 产物），跨解析稳定，
// 因此图中模糊边可以安全持有其中的 string_view。
namespace coral::phonetics {
struct FuzzySyllableViews
{
	const std::unordered_map<std::string_view, std::set<std::string_view>>* initials = nullptr;
	const std::unordered_map<std::string_view, std::set<std::string_view>>* finals = nullptr;
	const std::unordered_map<std::string_view, std::string_view>* alls = nullptr;

	[[nodiscard]] bool enabled() const noexcept { return initials != nullptr; }
};
} // 命名空间 coral::phonetics 结束（批次 3）

//基于拓扑排序的最短路径计算
namespace coral::phonetics {
class SyllablePathSearch : public coral::phonetics::RankedPathSearch<SyllablePathSearch, coral::phonetics::SyllableVertex, coral::phonetics::SyllableEdge, coral::phonetics::SyllableLattice>
{
public:
	int get_edge_weight(const coral::phonetics::SyllableEdge& edge) const override;
};
} // 命名空间 coral::phonetics 结束（批次 2）


namespace coral::phonetics {
using SyllableLatticeBase = coral::phonetics::DirectedLattice<coral::phonetics::SyllableVertex, coral::phonetics::SyllableEdge>;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
class SyllableLattice : public coral::phonetics::SyllableLatticeBase
{
public:
	// pyseq 指向 getPinyinSequences 里按最短路径拼好的拼音串，不拥有内存：
	// 每个区间的 key 都是那次拼接结果的子串，因此无需逐个区间再拼一遍字符串
	struct pySeqSearchInfo {
		std::string_view pyseq{};
		int from{ 0 };
		int to{ 0 };
		bool isEnglish = false;
		bool operator==(const pySeqSearchInfo& other) const noexcept {
			return from == other.from && to == other.to && pyseq == other.pyseq && isEnglish == other.isEnglish;
		}
	};
	struct pySeqSearchInfoHasher {
		size_t operator()(const SyllableLattice::pySeqSearchInfo& p) const noexcept {
			size_t h1 = std::hash<std::string_view>()(p.pyseq);
			size_t h2 = std::hash<int>()(p.from);
			size_t h3 = std::hash<int>()(p.to);
			return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (static_cast<size_t>(p.isEnglish) << 3);
		}
	};
	// 解析期去重表：走 arena 省掉每个元素一次节点分配
	using pySeqSearchSet = std::unordered_set<pySeqSearchInfo, pySeqSearchInfoHasher,
		std::equal_to<pySeqSearchInfo>, coral::phonetics::support::PoolAllocator<pySeqSearchInfo>>;

	SyllableLattice() = default;
	~SyllableLattice() = default;
	bool get_pinyin_sequences(
		const string& pinyinString,
		const syllable_strview_map& syllables,
		int maxLength,
		coral::phonetics::ParseSequences& pinyinSequences,
		bool& isFullPinyinMatch,
		bool isDoublepinyinMode,
		coral::phonetics::CandidateLattice& wordLattice,
		coral::phonetics::LexiconCatalog& lexicon,
		bool& sentHitDict,
		const std::unordered_set<std::string_view>& fuzzyOnlySyllables,
		const coral::phonetics::FuzzySyllableViews& fuzzyMaps,
		const bool shouldLimit,
        bool allow_full_hit_pruning = true);

	bool write_to_file(const std::filesystem::path &filepath) const;
	bool export_to_dot(const std::filesystem::path &filepath) const;

	static bool get_sub_pinyin_sequences(
		const coral::phonetics::ParseTextViews& sequence,
		coral::phonetics::ParseTextViews& subSequence,
		int index, bool hasSubPinyin = false);

private:
	bool make_graph(const string_view pinyinString
		, const syllable_strview_map& syllables
		, coral::phonetics::RankedPaths& shortestPaths
		, const coral::phonetics::FuzzySyllableViews& fuzzyMaps
		, const coral::phonetics::LexiconCatalog& lexicon);

	// 方案B第一阶段：为一条已确认的合法音节切分补挂模糊变体平行边。
	// 只在 fuzzInGraphEnabled() 且模糊表非空时生效。返回是否新增了边。
	bool add_fuzzy_variant_edges(
		const string_view originalSlice,
		int fromIndex,
		int toIndex,
		const coral::phonetics::FuzzySyllableViews& fuzzyMaps,
		const syllable_strview_map& syllables);

	// 方案B第二阶段：对原始输入的每个窗口枚举单次编辑（swap/delete/insert/
	// replace），结果为合法音节时挂编辑平行边。仅在 editInGraphEnabled() 且
	// 输入为纯小写字母、不含分隔符、且不存在干净全拼解析时调用。
	// 返回新增边数。
	int add_edit_edges(const string_view pinyinString, const syllable_strview_map& syllables);

	// 判定当前图是否存在"干净解析"（全拼边 + 句首/句尾裸声母，无句中裸声母
	// 与无效字符边），作为编辑纠错边的总闸门。vertexCount 为输入长度+1
	// （调用前提：无分隔符纯小写输入，顶点下标==字符位置）。
	bool has_clean_full_pinyin_parse(int vertexCount) const;
	bool make_doublepinyin_graph(const string_view pinyinString
		, const syllable_strview_map& syllables
		, coral::phonetics::RankedPaths& shortestPaths);

	bool shrink_graph_by_yen_ksp(
		int k,
		int dist,
		coral::phonetics::RankedPaths& shortestPaths,
		std::string_view input = {},
		const coral::phonetics::LexiconCatalog* lexicon = nullptr);
	void add_english_paths(std::string_view input, const coral::phonetics::LexiconCatalog& lexicon, coral::phonetics::RankedPaths& paths);

	int add_temp_edge(
		coral::phonetics::SyllableVertex& from,
		coral::phonetics::SyllableVertex& to,
		vector<coral::phonetics::SyllableEdge>& edgesVec);

	bool get_sub_pinyin_sequences(
		const coral::phonetics::ParseTextViews& sequence,
		coral::phonetics::ParseTextViews& subSequence,
		int index, const vector<int>& condition,
		bool hasSubPinyin = false) const;

	string format_pinyin(const string& pinyinString,
		const syllable_strview_map& syllables,
		unordered_set<string_view>& validSyllables, int& maxPinyinLen);
	void add_additional_path(coral::phonetics::RankedPaths& shortestPaths) const;
	void add_full_jianpin_sequences(const string& pinyinString, const coral::phonetics::RankedPaths& shortestPaths, coral::phonetics::ParseSequences& pinyinSequences, coral::phonetics::CandidateLattice& wordLattice, coral::phonetics::LexiconCatalog& lexiconmanager, bool& sentHitDict, const std::unordered_set<std::string_view>& fuzzyOnlySyllables, const bool shouldLimit);
	void add_split_pinyin(coral::phonetics::RankedPaths& shortestPaths);
private:
	string original_input_;
	set<tuple<int, int, size_t>> existing_edges_;
	std::vector<std::string> eng_persistent_strings_;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // SYLLABLEGRAPH_H

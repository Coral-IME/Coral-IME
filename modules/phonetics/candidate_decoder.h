/* languagemodel.coral 组合解码器：扁平词格 + CSR 分桶 + (position, gState) 组合态 A*。
 * 语言模型状态 gState 随搜索节点传播（参考 proclass_coralpinyin 的 FstDecoder）：
 * start 态一元权重走 uni 直挂，非 start 态走 find(gState, phraseId)，
 * miss 经 backoff 链回退到 start 后取后继一元权重，云词典词不改变 gState。 */
#pragma once
#ifndef CORAL_MODULES_PHONETICS_CANDIDATE_DECODER_H_INCLUDED
#define CORAL_MODULES_PHONETICS_CANDIDATE_DECODER_H_INCLUDED

#include "candidate_lattice.hpp"
#include "context_scorer.h"
#include "candidate_result.h"

#include <vector>

namespace coral::phonetics {
class CandidateDecoder
{
public:
	CandidateDecoder() = default;
	~CandidateDecoder() = default;

	bool init(const coral::phonetics::CandidateLattice& wordLattice, const coral::phonetics::ContextScorer& languageModel);

	bool get_parsed_res(vector<coral::phonetics::CandidateResult>& sents,
		const syllable_strview_map& syllables,
		coral::phonetics::ParseTextSet& outputedSentences,
		const coral::phonetics::ParseSequences& pinyinSequences,
		coral::phonetics::Score& shortestPathCost);

	// 只折减跨次输入的条件影响；0 忽略外部条件，1 保留原始强度。
    void set_condition_id(uint32_t id, float influence = 0.7f) {
        condition_influence_ = influence > 0.0f ? (influence < 1.0f ? influence : 1.0f) : 0.0f;
        condition_id_ = condition_influence_ > 0.0f ? id : kInvalidPhraseId;
    }
	void set_direct_candidate_budget(const int budget) { direct_candidate_budget_ = budget; }
#if CORAL_ENABLE_CHARACTER_MODEL
    void set_character_context(std::string_view text) { character_context_.assign(text); }
    void set_smart_candidates(bool enabled) { smart_candidates_ = enabled; }
#endif
	// 池子由 CPinyinModel 单例持有：解码器自己是 PinyinParseContext 的值成员，逐次解析新建，
	// 池子放在这里活不过一次解析
	void set_candidate_pool(vector<coral::phonetics::CandidateResult>* pool) { solution_pool_ = pool; }
	[[nodiscard]] bool candidates_truncated() const noexcept { return candidates_truncated_; }
	static bool comp(const coral::phonetics::CandidateResult& lhs, const coral::phonetics::CandidateResult& rhs);

private:
	struct SearchNode
	{
		double cost = 0.0;
		int position = 0;
		// languagemodel.coral 语言模型状态；start(0) 表示首词或已回退到一元层
		uint32_t gState = 0;
		int latticeArcIndex = -1;
		int parent = -1;
		uint16_t wordCount = 0;
		bool allBigram = true;
		bool hasEnglish = false;
        bool condition_pending = false;
	};

	struct QueueItem
	{
		double cost = 0.0;
		uint64_t order = 0;
		int nodeIndex = -1;
	};

	struct QueueGreater
	{
		bool operator()(const QueueItem& lhs, const QueueItem& rhs) const
		{
			if (lhs.cost != rhs.cost)
				return lhs.cost > rhs.cost;
			return lhs.order > rhs.order;
		}
	};

	struct StateRow
	{
		uint16_t pops = 0;
		uint8_t bestCount = 0;
		double best[8]{};
	};

    double condition_weight(double ordinary, double conditional) const {
        if (condition_influence_ <= 0.0f) return ordinary;
        if (condition_influence_ >= 1.0f) return conditional;
        return ordinary + condition_influence_ * (conditional - ordinary);
    }

	bool find_best_paths(vector<coral::phonetics::CandidateResult>& pathSolutions, int targetK, coral::phonetics::Score& shortestPathCost, bool requireEnglish = false);
#if CORAL_ENABLE_CHARACTER_MODEL
    void rerank_character_paths(vector<coral::phonetics::CandidateResult>& paths) const;
#endif
	// Solution 的 sentence/pinyin/pinyinview 三个容器每轮解析都要重新 malloc，
	// 而每轮末尾又被 clear 掉。这里把用完的 Solution 连同已分配好的缓冲一起收进池子，
	// 下一轮直接取出来填，堆分配基本只在预热的头几次解析里发生。
	coral::phonetics::CandidateResult take_solution() const;
	void recycle_solution(coral::phonetics::CandidateResult&& solution) const;
	void recycle_solutions(vector<coral::phonetics::CandidateResult>& solutions) const;
	// 回溯路径上的弧并拼出句子文本。终态弹出里重复句子占比不低，先只付拼字符串的代价，
	// 去重通过之后才由 buildPathSolution 构造 Solution 里那两个拼音向量
	bool collect_path_sentence(int nodeIndex, coral::phonetics::ParseIntegers& pathArcs, std::string& sentence) const;
	bool build_path_solution(int nodeIndex, const coral::phonetics::ParseIntegers& pathArcs, coral::phonetics::CandidateResult& solution) const;

	const coral::phonetics::CandidateLattice* lattice_ = nullptr;
	const coral::phonetics::ContextScorer* language_model_ = nullptr;
	// CSR：按起点位置分桶的弧下标，m_csrOffset[i]..m_csrOffset[i+1] 为位置 i 的出弧区间
	std::vector<int, coral::phonetics::support::PoolAllocator<int>> csr_offset_;
	std::vector<int32_t, coral::phonetics::support::PoolAllocator<int32_t>> edge_indices_;
	// A* 启发式：位置 i 到终点仅按词格弧代价的最小剩余代价（反向 DP，不含 bigram）。
	// 无穷大表示该位置无法到达终点，兼作死胡同剪枝。
	std::vector<double, coral::phonetics::support::PoolAllocator<double>> heuristic_;
	std::vector<double, coral::phonetics::support::PoolAllocator<double>> mixed_heuristic_;
	std::vector<char, coral::phonetics::support::PoolAllocator<char>> can_reach_english_;
	std::vector<SearchNode, coral::phonetics::support::PoolAllocator<SearchNode>> search_nodes_;
	// 终态去重用的句子缓冲，复用容量以免每次弹出都重新分配
	std::string sentence_buffer_;
	vector<coral::phonetics::CandidateResult>* solution_pool_ = nullptr;
	std::string extended_match_;
	int direct_candidate_budget_{ kUnlimitedDirectCandidates };
	bool candidates_truncated_ = false;
    bool full_dictionary_hit_ = false;
    uint32_t condition_id_ = kInvalidPhraseId;
    float condition_influence_ = 0.7f;
#if CORAL_ENABLE_CHARACTER_MODEL
    std::string character_context_;
    bool smart_candidates_ = true;
#endif
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // LANGUAGEDECODER_H

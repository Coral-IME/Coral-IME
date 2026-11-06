/* 对扁平词格与 languagemodel.coral 进行惰性 Top-K 组合（组合解码）。*/
#include "pch.h"
#include "phonetic_types.h"
#include "candidate_decoder.h"
#include "support/indexed_string_set.hpp"
#include "support/parse_metrics.hpp"
#include "syllable_helpers.h"
#include "coral/language_model/character_rerank.h"
#include "coral/composition/candidate_capacity.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <cstdlib>
#include <fstream>
#include <iomanip>

namespace
{
	constexpr int kPathCandidateLimit = 32;
	constexpr int kStateBestCap = 8;
    // 仅汉字参与逐字对齐；英文、符号和无效 UTF-8 不能用于末尾奖励校验。
    bool han_cells(std::string_view text, std::vector<std::string_view>& cells) {
        cells.clear();
        for (size_t at = 0; at < text.size();) {
            const auto lead = static_cast<unsigned char>(text[at]);
            const size_t size = lead >= 0xf0 && lead <= 0xf4 ? 4 : lead >= 0xe0 && lead <= 0xef ? 3 : 0;
            if (!size || at + size > text.size()) return false;
            unsigned value = lead & (size == 4 ? 7 : 15);
            for (size_t i = 1; i < size; ++i) {
                const auto next = static_cast<unsigned char>(text[at + i]);
                if ((next & 0xc0) != 0x80) return false;
                value = (value << 6) | (next & 63);
            }
            if (size == 4 && value < 0x10000) return false;
            if (!((value >= 0x3400 && value <= 0x4dbf) || (value >= 0x4e00 && value <= 0x9fff) ||
                (value >= 0xf900 && value <= 0xfaff) || (value >= 0x20000 && value <= 0x323af))) return false;
            cells.push_back(text.substr(at, size)); at += size;
        }
        return !cells.empty();
    }

    template<class Readings>
    bool align_han_cells(std::string_view word, const Readings& readings, std::string_view input,
        int from, int to) {
        std::vector<std::string_view> cells;
        if (!han_cells(word, cells) || cells.size() != readings.size() || from < 0 ||
            to < from || static_cast<size_t>(to) > input.size()) return false;
        size_t at = static_cast<size_t>(from);
        for (const std::string_view reading : readings) {
            while (at < static_cast<size_t>(to) && input[at] == '\'') ++at;
            if (reading.empty()) return false;
            // 部分展示串在音节内仍带分隔符，逐字核对原始覆盖范围。
            for (char ch : reading) {
                if (ch == '\'') continue;
                if (at >= static_cast<size_t>(to) || input[at++] != ch) return false;
            }
        }
        while (at < static_cast<size_t>(to) && input[at] == '\'') ++at;
        return at == static_cast<size_t>(to);
    }

    struct TerminalRule { std::string_view suffix, reading; };
    constexpr TerminalRule kTerminalRules[] = {
        {"啊", "a"}, {"吗", "ma"}, {"吧", "ba"},
        {"呢", "ne"}, {"呀", "ya"}, {"啦", "la"}
    };

    double terminal_arc_bonus(const coral::phonetics::CandidateArc& arc,
        const coral::phonetics::CandidateLattice& lattice, double bonus) {
        if (bonus <= 0 || arc.from < 0 || arc.from >= arc.to ||
            arc.to != lattice.get_vertex_count() - 1 || !arc.data.is_usable()) return 0;
        const auto& data = arc.data;
        const auto& sequence = data.pinyin_sequence();
        if (!data.dictionary_match || data.is_corrected || data.english_context_only ||
            sequence.is_match_english_vocab_ || sequence.is_fuzzy_match_ || sequence.is_edit_corrected_) return 0;
        const auto& readings = sequence.get_ui_show_pinyin_sequence();
        if (readings.empty()) return 0;
        for (const auto& rule : kTerminalRules) {
            if (readings.back() != rule.reading || data.phrase.size() < rule.suffix.size() ||
                data.phrase.substr(data.phrase.size() - rule.suffix.size()) != rule.suffix) continue;
            if (!align_han_cells(data.phrase, readings, lattice.input_pinyin(),
                lattice.input_offset(arc.from), lattice.input_offset(arc.to))) return 0;
            // 只在整句最后一条精确中文弧上计奖，整词和拆词获得相同的一次奖励。
            return bonus;
        }
        return 0;
    }

    double model_input_penalty(const coral::phonetics::CandidateEdgePayload& data,
        const coral::phonetics::ContextScorer& model) {
        const double reading_prior = model.reading_log_prior(data.phrase_id);
        const auto& sequence = data.pinyin_sequence();
        if (sequence.is_fuzzy_match_)
            return reading_prior + (sequence.sub_sequences_.empty() ? kFuzzyPenaltyQuan : kFuzzyPenaltyJian);
        return reading_prior + (sequence.is_edit_corrected_ ? kEditPenalty : 0.0);
    }

    bool is_full_dictionary_arc(const coral::phonetics::CandidateArc& arc, int end) {
        const auto& data = arc.data;
        if (arc.from != 0 || arc.to != end || !data.is_usable() || data.english_context_only ||
            !data.dictionary_match || data.phrase_id == kExtendedMatchId || data.is_corrected)
            return false;
        const auto& sequence = data.pinyin_sequence();
        return !sequence.is_match_english_vocab_ && !sequence.is_fuzzy_match_ && !sequence.is_edit_corrected_;
    }

	uint64_t makeStateKey(const int position, const uint32_t gState)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(position)) << 32) | gState;
	}

	// 状态表以 (position, gState) 组合出的 uint64 为键，value 保存每个状态的 Top-K 行。
	//
	// 相比 std::unordered_map，避免了逐节点堆分配与桶数组的缓存不友好问题。
	// 初始容量按 arcs*2 估计，按需扩容，全部内存来自解析期 arena。
	// 插槽采用线性探测；空插槽以哨兵 key 标记。
	// 初始 16 个插槽，容量始终为 2 的幂。
	//
	// 读写统一走 operator[]：不存在则插入新行，存在则返回已有行，
	// 因此不会出现重复的状态行。
	template <typename Row>
	class ArenaStateTable
	{
	public:
		explicit ArenaStateTable(const size_t expectedStates)
		{
			row_capacity_ = 64;
			while (row_capacity_ < expectedStates)
				row_capacity_ <<= 1;
			rows_ = allocate<Row>(row_capacity_);
			allocateSlots(row_capacity_ * 2);
		}

		~ArenaStateTable()
		{
			auto& arena = coral::phonetics::support::ParseArena::instance();
			arena.deallocate(slots_, slot_capacity_ * sizeof(Slot));
			arena.deallocate(rows_, row_capacity_ * sizeof(Row));
		}

		ArenaStateTable(const ArenaStateTable&) = delete;
		ArenaStateTable& operator=(const ArenaStateTable&) = delete;

		[[nodiscard]] Row& operator[](const uint64_t key)
		{
			uint32_t slot = slot_of(key);
			while (slots_[slot].key != kEmptyKey)
			{
				if (slots_[slot].key == key)
					return rows_[slots_[slot].rowIndex];
				slot = (slot + 1) & slot_mask_;
			}
			if (row_count_ == row_capacity_)
				growRows();
			const uint32_t rowIndex = row_count_++;
			rows_[rowIndex] = Row{};
			slots_[slot] = Slot{ key, rowIndex };
			if ((row_count_ + 1) * 2 > slot_capacity_)
				rehash(slot_capacity_ * 2);
			return rows_[rowIndex];
		}

	private:
		// position 恒为非负，32 位全 1 不会是合法键，可安全用作空槽标记。
		static constexpr uint64_t kEmptyKey = (std::numeric_limits<uint64_t>::max)();

		struct Slot
		{
			uint64_t key = kEmptyKey;
			uint32_t rowIndex = 0;
		};

		template <typename T>
		static T* allocate(const size_t count)
		{
			return static_cast<T*>(coral::phonetics::support::ParseArena::instance().allocate(count * sizeof(T), alignof(T)));
		}

		[[nodiscard]] uint32_t slot_of(const uint64_t key) const noexcept
		{
			// splitmix64 把 position 与 gState 的变化均匀散布到各 位级。
			uint64_t hash = key;
			hash ^= hash >> 33;
			hash *= 0xff51afd7ed558ccdULL;
			hash ^= hash >> 33;
			return static_cast<uint32_t>(hash) & slot_mask_;
		}

		void allocateSlots(const size_t capacity)
		{
			slots_ = allocate<Slot>(capacity);
			slot_capacity_ = capacity;
			slot_mask_ = static_cast<uint32_t>(capacity - 1);
			std::fill_n(slots_, capacity, Slot{});
		}

		void rehash(const size_t capacity)
		{
			Slot* oldSlots = slots_;
			const size_t oldCapacity = slot_capacity_;
			allocateSlots(capacity);
			for (size_t i = 0; i < oldCapacity; ++i)
			{
				if (oldSlots[i].key == kEmptyKey)
					continue;
				uint32_t slot = slot_of(oldSlots[i].key);
				while (slots_[slot].key != kEmptyKey)
					slot = (slot + 1) & slot_mask_;
				slots_[slot] = oldSlots[i];
			}
			coral::phonetics::support::ParseArena::instance().deallocate(oldSlots, oldCapacity * sizeof(Slot));
		}

		void growRows()
		{
			Row* oldRows = rows_;
			const size_t oldCapacity = row_capacity_;
			row_capacity_ = oldCapacity * 2;
			rows_ = allocate<Row>(row_capacity_);
			std::copy_n(oldRows, row_count_, rows_);
			coral::phonetics::support::ParseArena::instance().deallocate(oldRows, oldCapacity * sizeof(Row));
		}

		Slot* slots_ = nullptr;
		Row* rows_ = nullptr;
		size_t slot_capacity_ = 0;
		size_t row_capacity_ = 0;
		uint32_t slot_mask_ = 0;
		uint32_t row_count_ = 0;
	};

	// 与 CLanguageDecoder::comp 的语义保持一致：本函数只重排，不改变解集合。
	// 为避免直接排序体积较大的 Solution，先构建轻量键向量再排序。
	// 键依次比较输入覆盖长度、正常输入优先级、概率与原始顺序。
	void rankSolutions(vector<coral::phonetics::CandidateResult>& solutions)
	{
		const size_t count = solutions.size();
		if (count < 2)
			return;

		struct RankKey
		{
			size_t pinyinLength;
            bool supplement;
            double probability;
            int learned_tie;
			uint32_t origin;
		};
		std::vector<RankKey, coral::phonetics::support::PoolAllocator<RankKey>> keys;
		keys.reserve(count);
		for (size_t i = 0; i < count; ++i)
		{
			const coral::phonetics::CandidateResult& solution = solutions[i];
			size_t pinyinLength = 0;
			for (const auto& item : solution.pinyin)
				pinyinLength += item.size();
            keys.push_back(RankKey{ pinyinLength, solution.is_correction_supplement, solution.ordering_score(),
                solution.learned_order_tie < 0 ? (std::numeric_limits<int>::max)() : solution.learned_order_tie,
                static_cast<uint32_t>(i) });
		}
		std::sort(keys.begin(), keys.end(), [](const RankKey& lhs, const RankKey& rhs) {
			if (lhs.pinyinLength != rhs.pinyinLength)
				return lhs.pinyinLength > rhs.pinyinLength;
            if (lhs.supplement != rhs.supplement) return !lhs.supplement;
			if (lhs.probability != rhs.probability)
				return lhs.probability > rhs.probability;
            if (lhs.learned_tie != rhs.learned_tie) return lhs.learned_tie < rhs.learned_tie;
			return lhs.origin < rhs.origin;	 // 原始顺序作末级键，等价于 stable_sort。
		});

		vector<coral::phonetics::CandidateResult> ordered;
		ordered.reserve(count);
		for (const RankKey& key : keys)
			ordered.push_back(std::move(solutions[key.origin]));
		solutions.swap(ordered);
	}
}

bool coral::phonetics::CandidateDecoder::init(const coral::phonetics::CandidateLattice& wordLattice, const coral::phonetics::ContextScorer& languageModel)
{
	lattice_ = &wordLattice;
	language_model_ = &languageModel;
    ranking_features_enabled_ = false;
#if CORAL_ENABLE_CHARACTER_MODEL
#if defined(_WIN32)
    const auto* ranking_trace_path = _wgetenv(L"CORAL_RANKING_TRACE");
#else
    const auto* ranking_trace_path = std::getenv("CORAL_RANKING_TRACE");
#endif
    ranking_features_enabled_ = language_model_->candidate_ranker() || (ranking_trace_path && *ranking_trace_path);
#endif
	extended_match_.assign(wordLattice.extended_match_.data(), wordLattice.extended_match_.size());
	candidates_truncated_ = false;
    full_dictionary_hit_ = false;

	const int vertexCount = wordLattice.get_vertex_count();
	if (vertexCount < 2)
		return false;

	// 构建 CSR 出边索引：offset 前缀和给出每个顶点出边区间，可 O(1) 定位且遍历缓存友好。
	const std::vector<coral::phonetics::CandidateArc>& arcs = wordLattice.get_arc_array();
	csr_offset_.assign(static_cast<size_t>(vertexCount) + 1, 0);
	for (const coral::phonetics::CandidateArc& arc : arcs)
	{
		if (arc.from < 0 || arc.from >= vertexCount)
			continue;
		++csr_offset_[static_cast<size_t>(arc.from) + 1];
	}
	for (int i = 0; i < vertexCount; ++i)
		csr_offset_[static_cast<size_t>(i) + 1] += csr_offset_[static_cast<size_t>(i)];
	edge_indices_.resize(csr_offset_[static_cast<size_t>(vertexCount)]);
	{
		std::vector<int, coral::phonetics::support::PoolAllocator<int>> writeHead(csr_offset_.begin(),
			csr_offset_.begin() + vertexCount);
		for (size_t i = 0; i < arcs.size(); ++i)
		{
			const coral::phonetics::CandidateArc& arc = arcs[i];
			if (arc.from < 0 || arc.from >= vertexCount)
				continue;
			edge_indices_[static_cast<size_t>(writeHead[static_cast<size_t>(arc.from)]++)] =
				static_cast<int32_t>(i);
		}
	}

    terminal_arc_bonuses_.assign(arcs.size(), 0);
    double terminal_shift = 0;
    for (size_t i = 0; i < arcs.size(); ++i) {
        terminal_arc_bonuses_[i] = terminal_arc_bonus(arcs[i], wordLattice, terminal_bonus_);
        terminal_shift = std::max(terminal_shift, terminal_arc_bonuses_[i]);
    }

    // 全串精确命中词库时只组装词库候选，不构建语言模型启发值或搜索组句路径。
    full_dictionary_hit_ = std::any_of(arcs.begin(), arcs.end(), [&](const auto& arc) {
        return is_full_dictionary_arc(arc, vertexCount - 1);
    });
    // 可信解释必须由未改写输入的词库弧覆盖全串，原字母回退不能成为保护依据。
    has_edit_corrections_ = std::any_of(arcs.begin(), arcs.end(), [](const auto& arc) {
        return arc.data.is_usable() && (arc.data.is_corrected || arc.data.pinyin_sequence().is_edit_corrected_);
    });
    std::vector<char, coral::phonetics::support::PoolAllocator<char>> normal(vertexCount, 0);
    normal[0] = 1;
    for (int from = 0; from < vertexCount - 1; ++from) {
        if (!normal[from]) continue;
        for (int at = csr_offset_[from]; at < csr_offset_[from + 1]; ++at) {
            const auto& arc = arcs[edge_indices_[at]];
            const auto& data = arc.data;
            if (!data.is_usable()) continue;
            const auto& sequence = data.pinyin_sequence();
            if (arc.to <= from || arc.to >= vertexCount || !data.is_usable() ||
                !data.dictionary_match || data.phrase_id == kExtendedMatchId || data.is_corrected ||
                data.english_context_only || sequence.is_edit_corrected_ || sequence.is_match_english_vocab_)
                continue;
            normal[arc.to] = 1;
        }
    }
    normal_dictionary_path_ = normal.back() != 0;
    if (full_dictionary_hit_) {
        heuristic_.clear();
        mixed_heuristic_.clear();
        can_reach_english_.clear();
        return true;
    }

	// A* 启发值来自与 FstDecoder::computeCsrHeuristic 相同的后向 DP：
	// 自终点向前松弛得到 h[i]（到终点的可达下界）；混合检索额外纳入 bigram 最优权重。
	// 权重取负得到非负步长，保证 h 可采纳，f = g + h 的展开顺序有效。
	constexpr double kInf = std::numeric_limits<double>::infinity();
	const auto* pair_bonus = languageModel.pair_bonus_model();
    search_cost_shift_ = (pair_bonus ? pair_bonus->max_bonus() : 0) + terminal_shift;
    pair_bonus_arc_states_.assign(pair_bonus ? arcs.size() : 0, 0);
    if (pair_bonus) for (size_t i = 0; i < arcs.size(); ++i) {
        const auto& data = arcs[i].data;
        if (!data.pinyin_sequence().is_match_english_vocab_ && !data.english_context_only)
            pair_bonus_arc_states_[i] = pair_bonus->word_state(data.phrase);
    }
	can_reach_english_.assign(vertexCount, 0);
	for (bool mixed : {false, true}) {
		auto& heuristic = mixed ? mixed_heuristic_ : heuristic_;
		heuristic.assign(vertexCount, kInf);
		heuristic.back() = 0.0;
		for (int position = vertexCount - 2; position >= 0; --position) {
			const int begin = csr_offset_[position], end = csr_offset_[position + 1];
			for (int index = begin; index < end; ++index) {
				const auto& arc = arcs[edge_indices_[index]];
				if (arc.to <= position || arc.to >= vertexCount || !arc.data.is_usable()) continue;
				const bool english = arc.data.pinyin_sequence().is_match_english_vocab_;
				if (!mixed && (english || arc.data.english_context_only)) continue;
				if (!std::isfinite(heuristic[arc.to])) continue;
				const auto* model = languageModel.gbin();
				// 混合检索和启用上下文的中文检索均使用含条件加成的下界，
                // 避免启发值高估而过早丢弃较优路径。
				double weight = (mixed || condition_id_ != kInvalidPhraseId) && model != nullptr
					? std::max(arc.data.weight, model->best_weight(arc.data.phrase_id)) : arc.data.weight;
                if (const auto* surface = languageModel.surface_model()) {
                    const double bound = surface->phrase_upper_bound(arc.data.phrase_id);
                    if (!english && std::isfinite(bound)) weight = bound + model_input_penalty(arc.data, languageModel);
                }
				double step = std::max(0.0, -static_cast<double>(weight));
                if (pair_bonus) {
                    step = std::max(0.0, -static_cast<double>(arc.data.weight)) -
                        pair_bonus->best_bonus(pair_bonus_arc_states_[edge_indices_[index]]);
                }
                // 末尾奖励也进入可采纳下界；统一势差使所有搜索边保持非负。
                step += search_cost_shift_ * (arc.to - position) - terminal_arc_bonuses_[edge_indices_[index]];
				heuristic[position] = std::min(heuristic[position], step + heuristic[arc.to]);
				if (mixed && (english || can_reach_english_[arc.to])) can_reach_english_[position] = 1;
			}
		}
	}
	return true;
}

bool coral::phonetics::CandidateDecoder::collect_path_sentence(const int nodeIndex, coral::phonetics::ParseIntegers& pathArcs, std::string& sentence) const
{
	if (lattice_ == nullptr || nodeIndex < 0)
		return false;

	const auto& arcs = lattice_->get_arc_array();
	pathArcs.clear();
	for (int current = nodeIndex; current >= 0;)
	{
		const SearchNode& node = search_nodes_[current];
		if (node.latticeArcIndex >= 0)
			pathArcs.push_back(node.latticeArcIndex);
		current = node.parent;
	}
	std::reverse(pathArcs.begin(), pathArcs.end());
	if (pathArcs.empty())
		return false;

	sentence.clear();
	for (const int arcIndex : pathArcs)
	{
		if (arcIndex < 0 || arcIndex >= static_cast<int>(arcs.size()))
			return false;
		const coral::phonetics::CandidateEdgePayload& data = arcs[static_cast<size_t>(arcIndex)].data;
		if (!data.is_usable())
			return false;
		sentence += data.phrase;
	}
	return !sentence.empty();
}

bool coral::phonetics::CandidateDecoder::build_path_solution(const int nodeIndex, const coral::phonetics::ParseIntegers& pathArcs, coral::phonetics::CandidateResult& solution) const
{
	const auto& arcs = lattice_->get_arc_array();
	int pinyinCharLength = 0;
    const auto* pair_model = ranking_features_enabled_ ? language_model_->pair_bonus_model() : nullptr;
    uint32_t previous_pair = pair_model ? language_model_->pair_bonus_state(condition_id_) : 0;
    if (pair_model && !previous_pair) previous_pair = pair_model->word_state("<s>");
    solution.ranking_path = {};
    solution.ranking_path.pair_available = pair_model != nullptr;
	{
		// 先统计整条路径的音节数，避免向量多次扩容；
		// 随后的 push_back 便不会触发重新分配。
		size_t syllableCount = 0;
		for (const int arcIndex : pathArcs)
			syllableCount += arcs[static_cast<size_t>(arcIndex)].data.pinyin_sequence()
				.get_ui_show_pinyin_sequence().size();
		solution.pinyinview.reserve(syllableCount);
		solution.pinyin.reserve(syllableCount);
		solution.sentence.reserve(sentence_buffer_.size());
	}
	for (const int arcIndex : pathArcs)
	{
		const coral::phonetics::CandidateEdgePayload& data = arcs[static_cast<size_t>(arcIndex)].data;
		solution.sentence += data.phrase;
        solution.is_corrected = solution.is_corrected || data.is_corrected || data.pinyin_sequence().is_edit_corrected_;
        if (ranking_features_enabled_) {
            auto& stats = solution.ranking_path;
            const auto characters = std::count_if(data.phrase.begin(), data.phrase.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
            ++stats.words;
            stats.single_words += characters == 1;
            stats.longest_word = std::max(stats.longest_word, double(characters));
            stats.corrected = stats.corrected || data.is_corrected || data.pinyin_sequence().is_edit_corrected_ || data.pinyin_sequence().is_fuzzy_match_;
            if (pair_model) {
                const auto next_pair = pair_model->word_state(data.phrase);
                const double bonus = pair_model->bonus(previous_pair, next_pair);
                stats.pair_bonus += bonus;
                stats.pair_hits += bonus > 0;
                stats.covered_words += next_pair != 0;
                previous_pair = next_pair;
            }
        }
		const auto& uiPinyin = data.pinyin_sequence().get_ui_show_pinyin_sequence();
		solution.word_size += data.pinyin_sequence().get_pinyin_length();
		if (pathArcs.size() == 1 && data.pinyin_sequence().is_match_english_vocab_)
			solution.transform_flags = solution.transform_flags | MiddlewareType::NoStoreUserLexicon;
		for (const std::string_view pinyin : uiPinyin)
		{
			solution.pinyinview.push_back(pinyin);
			solution.pinyin.emplace_back(pinyin);
			pinyinCharLength += static_cast<int>(pinyin.size());
		}
	}

	const SearchNode& leaf = search_nodes_[nodeIndex];
	solution.probability = static_cast<coral::phonetics::Score>(-leaf.cost + search_cost_shift_ * leaf.position);
	solution.set_probability_with_weight(
		static_cast<coral::phonetics::Score>(1.0f / (1.0f + static_cast<coral::phonetics::Score>(pinyinCharLength))));
    solution.is_correction_supplement = normal_dictionary_path_ && solution.is_corrected;
	return !solution.sentence.empty();
}

namespace
{
	// 解对象池容量：超出后不再回收，防止池无限增长。
	constexpr size_t kSolutionPoolCapacity = 512;
}

coral::phonetics::CandidateResult coral::phonetics::CandidateDecoder::take_solution() const
{
	if (solution_pool_ == nullptr || solution_pool_->empty())
		return coral::phonetics::CandidateResult{};
	coral::phonetics::CandidateResult solution = std::move(solution_pool_->back());
	solution_pool_->pop_back();
	return solution;
}

void coral::phonetics::CandidateDecoder::recycle_solution(coral::phonetics::CandidateResult&& solution) const
{
	if (solution_pool_ == nullptr || solution_pool_->size() >= kSolutionPoolCapacity)
		return;
	// 只回收仍带缓冲的 move 解：
	// 已退化的 vector（move 后的空壳）与短字符串（SSO）没有可复用堆内存，capacity 为 0 时直接丢弃。
	if (solution.pinyin.capacity() == 0 && solution.pinyinview.capacity() == 0)
		return;
	solution.reset();
	solution_pool_->push_back(std::move(solution));
}

void coral::phonetics::CandidateDecoder::recycle_solutions(vector<coral::phonetics::CandidateResult>& solutions) const
{
	for (coral::phonetics::CandidateResult& solution : solutions)
		recycle_solution(std::move(solution));
	solutions.clear();
}

bool coral::phonetics::CandidateDecoder::find_best_paths(
	vector<coral::phonetics::CandidateResult>& pathSolutions,
	const int targetK,
	coral::phonetics::Score& shortestPathCost,
	const bool requireEnglish,
    const bool normal_only)
{
	recycle_solutions(pathSolutions);
	shortestPathCost = kMaxWeight;
	if (lattice_ == nullptr || language_model_ == nullptr || targetK <= 0)
		return false;

	const auto& arcs = lattice_->get_arc_array();
	const int vertexCount = lattice_->get_vertex_count();
	if (vertexCount < 2)
		return false;
	const int targetPosition = vertexCount - 1;
	const auto& heuristic = requireEnglish ? mixed_heuristic_ : heuristic_;
	if (requireEnglish && !can_reach_english_[0]) return false;
	const auto stateKey = [](const SearchNode& node) {
		// 位置恒为非负 int，组合键最高位原本空闲，用于区分尚未消费的外部条件。
        return makeStateKey(node.position, node.gState | (node.hasEnglish ? 0x80000000u : 0u)) |
            (node.condition_pending ? (uint64_t{1} << 63) : 0);
	};

	// languagemodel.coral 缺失时退化为纯词格检索，所有指针判空兜底。
	const coral::phonetics::BigramTable* g = language_model_->gbin();
    const auto* surface = language_model_->surface_model();
    const auto* pair_bonus = language_model_->pair_bonus_model();
	const coral::storage::BigramUnigramRecord* uni = (g != nullptr) ? g->uni() : nullptr;
	const uint32_t uni_size = (g != nullptr) ? g->uni_size() : 0;
	const uint32_t gStart = (g != nullptr) ? static_cast<uint32_t>(g->start()) : 0u;

	search_nodes_.clear();
	search_nodes_.reserve(std::max<size_t>(256, arcs.size() * 2));
	using QueueStorage = std::vector<QueueItem, coral::phonetics::support::PoolAllocator<QueueItem>>;
	std::priority_queue<QueueItem, QueueStorage, QueueGreater> queue;
	// 状态表按边数估算初始容量，避免哈希表频繁 rehash。
	ArenaStateTable<StateRow> states(std::min<size_t>(arcs.size() + 1, 1024));

	if (!std::isfinite(heuristic[0]))
		return false;
	// 已接受句子集合用于去重，避免重复的 Solution 进入输出。
	coral::phonetics::ParseTextSet acceptedSentences(static_cast<size_t>(targetK));
	coral::phonetics::ParseIntegers pathArcs;
	search_nodes_.push_back(SearchNode{});
    if (condition_id_ < uni_size && uni[condition_id_].next >= 0) {
        search_nodes_.back().gState = static_cast<uint32_t>(uni[condition_id_].next);
        search_nodes_.back().condition_pending = true;
    }
    if (surface) {
        search_nodes_.back().gState = surface->phrase_end(condition_id_);
        search_nodes_.back().condition_pending = search_nodes_.back().gState != 0;
    }
    if (pair_bonus) {
        search_nodes_.back().gState = language_model_->pair_bonus_state(condition_id_);
        search_nodes_.back().condition_pending = search_nodes_.back().gState != 0;
    }
	// 初始节点入队：f = g + h，起点 g = 0。
	queue.push(QueueItem{ heuristic[0], 0, 0 });
	uint64_t order = 1;
	const int bestLimit = std::min(targetK, kStateBestCap);

	auto consider = [&](SearchNode&& candidate) {
		// 启发值为无穷大（不可达）的位置直接剪枝，不展开无效分支。
		if (!std::isfinite(heuristic[static_cast<size_t>(candidate.position)]))
			return;
		if (requireEnglish && !candidate.hasEnglish && !can_reach_english_[candidate.position]) return;
		const uint64_t key = stateKey(candidate);
		StateRow& row = states[key];
		if (row.bestCount >= bestLimit && candidate.cost >= row.best[row.bestCount - 1])
			return;

		int insertAt = row.bestCount;
		while (insertAt > 0 && row.best[insertAt - 1] > candidate.cost)
		{
			if (insertAt < bestLimit)
				row.best[insertAt] = row.best[insertAt - 1];
			--insertAt;
		}
		if (insertAt < bestLimit)
			row.best[insertAt] = candidate.cost;
		if (row.bestCount < bestLimit)
			++row.bestCount;

		PERF_INC(decoderPushes);
		const int index = static_cast<int>(search_nodes_.size());
		search_nodes_.push_back(std::move(candidate));
		// 以 A* 优先级入队：f = g + h(position)。
		queue.push(QueueItem{
			search_nodes_.back().cost + heuristic[static_cast<size_t>(search_nodes_.back().position)],
			order++, index });
	};

	while (!queue.empty() && static_cast<int>(pathSolutions.size()) < targetK)
	{
		const QueueItem currentItem = queue.top();
		queue.pop();
		PERF_INC(decoderPops);
		const SearchNode current = search_nodes_[currentItem.nodeIndex];

		if (current.wordCount != 0)
		{
			StateRow& row = states[stateKey(current)];
			if (row.pops >= static_cast<uint16_t>(targetK))
				continue;
			++row.pops;
		}

		if (current.position == targetPosition)
		{
			if (requireEnglish && !current.hasEnglish) continue;
			if (shortestPathCost == kMaxWeight)
				shortestPathCost = static_cast<coral::phonetics::Score>(current.cost - search_cost_shift_ * targetPosition);
			if (!collect_path_sentence(currentItem.nodeIndex, pathArcs, sentence_buffer_))
				continue;
			if (!acceptedSentences.insert(sentence_buffer_).second)
				continue;
			coral::phonetics::CandidateResult solution = take_solution();
			if (!build_path_solution(currentItem.nodeIndex, pathArcs, solution))
			{
				recycle_solution(std::move(solution));
				continue;
			}
			PERF_INC(decoderSolutions);
			pathSolutions.push_back(std::move(solution));
			continue;
		}

		if (current.position < 0 || current.position >= targetPosition)
			continue;
		const int begin = csr_offset_[static_cast<size_t>(current.position)];
		const int end = csr_offset_[static_cast<size_t>(current.position) + 1];
		const bool atStart = current.gState == gStart;
		for (int k = begin; k < end; ++k)
		{
			const int arcIndex = edge_indices_[static_cast<size_t>(k)];
			if (arcIndex < 0 || arcIndex >= static_cast<int>(arcs.size()))
				continue;
			const auto& arc = arcs[static_cast<size_t>(arcIndex)];
			if (arc.to <= current.position || arc.to > targetPosition)
				continue;

			const coral::phonetics::CandidateEdgePayload& data = arc.data;
			if (!data.is_usable())
				continue;
			const bool english = data.pinyin_sequence().is_match_english_vocab_;
			if (!requireEnglish && (english || data.english_context_only)) continue;
            if (normal_only && (data.is_corrected || data.pinyin_sequence().is_edit_corrected_)) continue;

#ifdef _MSC_VER
			// 预取下一跳的一元记录，减少 FstDecoder 访问 uni 表的缓存缺失。
			if (uni != nullptr && atStart && k + 1 < end)
			{
				const int nextArcIndex = edge_indices_[static_cast<size_t>(k + 1)];
				if (nextArcIndex >= 0 && nextArcIndex < static_cast<int>(arcs.size()))
				{
					const uint32_t nsym = arcs[static_cast<size_t>(nextArcIndex)].data.phrase_id;
					if (nsym < uni_size)
						_mm_prefetch(reinterpret_cast<const char*>(&uni[nsym]), _MM_HINT_T0);
				}
			}
#endif

			// ---- languagemodel.coral 参与评分：步长代价与 gState 迁移 ----
			// 语言模型权重以 log 概率存储，代价 = -weight；步长必须非负，否则会破坏 Dijkstra/A* 前提。
			double stepCost = 0.0;
			uint32_t nextGState = gStart;
			bool bigramHit = false;
            coral::language_model::PhraseScore surface_score;
            const bool inModel = surface ? (!english && surface->score_phrase(current.gState,
                data.phrase_id, surface_score)) : (g != nullptr && uni != nullptr &&
                data.phrase_id < uni_size && uni[data.phrase_id].next >= 0);
            if (pair_bonus) {
                if (data.weight == kLogInfiniteSimal) continue;
                nextGState = pair_bonus_arc_states_[arcIndex];
                const auto previous = current.wordCount == 0 && !current.condition_pending ?
                    pair_bonus->word_state("<s>") : current.gState;
                const double bonus = pair_bonus->bonus(previous, nextGState);
                const double influence = current.condition_pending ? condition_influence_ : 1.0;
                stepCost = std::max(0.0, -static_cast<double>(data.weight)) - influence * bonus;
                bigramHit = bonus > 0;
            }
            else if (surface) {
                if (inModel) {
                    stepCost = std::max(0.0, -surface_score.log_probability - model_input_penalty(data, *language_model_));
                    nextGState = surface_score.next_state;
                    bigramHit = surface_score.first_arc_hit;
                } else {
                    if (data.weight == kLogInfiniteSimal) continue;
                    stepCost = std::max(0.0, -static_cast<double>(data.weight));
                    // 模型外词、混输和未知读音路径均消耗边界，禁止跨过它沿用旧前词。
                    nextGState = 0;
                }
            }
			else if (!inModel)
			{
				// 词条未收录 / 模型缺失时退回弧自带权重，gState 视语言重置或保持。
				// 此处不做二元查询；真正的 getBigram 发生在上下文评分阶段，miss 时回退。
				if (data.weight == kLogInfiniteSimal)
					continue;
				stepCost = std::max(0.0, -static_cast<double>(data.weight));
				// 未知英文会破坏邻接关系。保持既有的中文/OOV 行为。
				nextGState = english ? gStart : current.gState;
			}
			else if (atStart)
			{
				// 句首节点用一元权重计步，data.weight 只用于校验。
				stepCost = std::max(0.0, -static_cast<double>(uni[data.phrase_id].weight));
				nextGState = static_cast<uint32_t>(uni[data.phrase_id].next);
			}
			else
			{
				int32_t nxt = 0;
				float gw = 0.f;
				if (g->find(static_cast<int32_t>(current.gState), data.phrase_id, nxt, gw))
				{
					stepCost = std::max(0.0, -static_cast<double>(gw));
					nextGState = static_cast<uint32_t>(nxt);
					bigramHit = true;
				}
				else
				{
					// miss 回退到后继一元（与旧 wsfa miss 语义一致）：
					// 不加 backoffWeight 罚分。m2 的 prob 非归一化条件概率，
					// backoffWeight 会被夹紧到 log(1e-6)=-13.82，对 champion 词
					// 误罚过重；删去后词对靠主词典一元排序即可命中。
					const double w = static_cast<double>(uni[data.phrase_id].weight);
					if (!std::isfinite(w))
					{
						if (data.weight == kLogInfiniteSimal)
								continue;
						stepCost = std::max(0.0, -static_cast<double>(data.weight));
						nextGState = gStart;
					}
					else
					{
						stepCost = std::max(0.0, -w);
						nextGState = static_cast<uint32_t>(uni[data.phrase_id].next);
					}
				}
			}

            // 外部条件只在首次有效模型转移时折减；本次组串内部的二元评分不变。
            // 未入模型的中文仍沿用既有状态，因此边界标记也保留到真正消费条件时。
            if (!pair_bonus && current.condition_pending && inModel) {
                double ordinary_cost;
                if (surface) {
                    coral::language_model::PhraseScore initial;
                    surface->score_phrase(0, data.phrase_id, initial);
                    ordinary_cost = -initial.log_probability - model_input_penalty(data, *language_model_);
                } else ordinary_cost = std::max(0.0, -static_cast<double>(uni[data.phrase_id].weight));
                stepCost = condition_weight(ordinary_cost, stepCost);
            }
            stepCost += search_cost_shift_ * (arc.to - current.position) - terminal_arc_bonuses_[arcIndex];
			SearchNode next;
            next.condition_pending = !pair_bonus && !surface && current.condition_pending && !inModel && !english;
			next.hasEnglish = current.hasEnglish || english;
			next.cost = current.cost + stepCost;
			next.position = arc.to;
			next.gState = nextGState;
			next.latticeArcIndex = arcIndex;
			next.parent = currentItem.nodeIndex;
			next.wordCount = static_cast<uint16_t>(std::min<int>(current.wordCount + 1,
				(std::numeric_limits<uint16_t>::max)()));
			next.allBigram = current.wordCount == 0 ? true : current.allBigram && bigramHit;
			consider(std::move(next));
		}
	}

	rankSolutions(pathSolutions);
	return shortestPathCost != kMaxWeight;
}

bool coral::phonetics::CandidateDecoder::comp(const coral::phonetics::CandidateResult& lhs, const coral::phonetics::CandidateResult& rhs)
{
	const auto pinyinLength = [](const auto& pinyin) {
		return std::accumulate(pinyin.begin(), pinyin.end(), size_t{ 0 },
			[](const size_t sum, const auto& item) { return sum + item.size(); });
	};
	const size_t lhsLength = pinyinLength(lhs.pinyin);
	const size_t rhsLength = pinyinLength(rhs.pinyin);
	if (lhsLength != rhsLength)
		return lhsLength > rhsLength;
    if (lhs.is_correction_supplement != rhs.is_correction_supplement)
        return !lhs.is_correction_supplement;
    if (lhs.ordering_score() != rhs.ordering_score()) return lhs.ordering_score() > rhs.ordering_score();
    const int left = lhs.learned_order_tie < 0 ? (std::numeric_limits<int>::max)() : lhs.learned_order_tie;
    const int right = rhs.learned_order_tie < 0 ? (std::numeric_limits<int>::max)() : rhs.learned_order_tie;
    return left < right;
}

#if CORAL_ENABLE_CHARACTER_MODEL
void coral::phonetics::CandidateDecoder::rerank_character_paths(
    vector<coral::phonetics::CandidateResult>& paths) const
{
    if (!smart_candidates_ || paths.empty()) return;
    const auto eligible = [](std::string_view text) {
        // 词格文本是 UTF-8，统计非续字节即可按 Unicode 字符计数，增补平面汉字也只算一字。
        const auto count = std::count_if(text.begin(), text.end(), [](unsigned char byte) {
            return (byte & 0xc0) != 0x80;
        });
        return count > CORAL_CHARACTER_RERANK_THRESHOLD;
    };
    // 原首选较短时整组跳过，避免简短输入因存在长备选而触发神经推理。
    if (!eligible(paths.front().sentence)) return;
    const auto* model = language_model_->character_model();
#if defined(_WIN32)
    const auto* trace_path = _wgetenv(L"CORAL_CHARACTER_TRACE");
    const auto* ranking_trace_path = _wgetenv(L"CORAL_RANKING_TRACE");
#else
    const auto* trace_path = std::getenv("CORAL_CHARACTER_TRACE");
    const auto* ranking_trace_path = std::getenv("CORAL_RANKING_TRACE");
#endif
    const auto* ranker = language_model_->candidate_ranker();
    if ((!model || language_model_->character_weight() == 0) && (!trace_path || !*trace_path) &&
        !ranker && (!ranking_trace_path || !*ranking_trace_path)) return;
    std::vector<std::string_view> texts;
    std::vector<size_t> indexes;
    // 搜索保留原有 32 路，只对基础排序前 K 路进行神经评分；尾部路径不补位。
    const size_t scored_limit = std::min(paths.size(), size_t{CORAL_CHARACTER_RERANK_PATHS});
    for (size_t i = 0; i < scored_limit; ++i) {
        if (!eligible(paths[i].sentence)) continue;
        texts.push_back(paths[i].sentence);
        indexes.push_back(i);
    }
    // 只有参考首选本身满足门槛时没有可重排对象，也无需调用模型。
    if (texts.size() < 2) return;
    std::vector<double> selected_scores;
    const bool scored = model && coral::language_model::CharacterRerankScope::score(
        language_model_->shared_character_model(), character_context_, texts, selected_scores);
    std::vector<double> scores(paths.size(), std::numeric_limits<double>::quiet_NaN());
    if (scored) {
        for (size_t i = 0; i < indexes.size(); ++i) scores[indexes[i]] = selected_scores[i];
    }
    const double reference = scored ? scores.front() : std::numeric_limits<double>::quiet_NaN();
    std::ofstream trace;
    if (trace_path && *trace_path) trace.open(std::filesystem::path(trace_path), std::ios::app);
    std::vector<coral::language_model::CandidateRankingInput> ranking_inputs;
    const auto characters = [](std::string_view text) {
        return std::count_if(text.begin(), text.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
    };
    for (size_t i = 0; i < paths.size(); ++i) {
        auto& path = paths[i];
        const double base = path.probability;
        double bonus = 0;
        if (scored && std::isfinite(reference) && std::isfinite(scores[i])) {
            const auto length = std::accumulate(path.pinyin.begin(), path.pinyin.end(), size_t{0},
                [](size_t n, const auto& syllable) { return n + syllable.size(); });
            // 与原首选比较，只增加有神经证据的候选；沿用基础分数的拼音长度尺度。
            bonus = language_model_->character_weight() * std::min(16.0, std::max(0.0, scores[i] - reference)) /
                (1.0 + length);
        }
        if (trace) trace << lattice_->input_pinyin() << '\t' << character_context_ << '\t' << i << '\t'
            << path.sentence << '\t' << std::setprecision(12) << path.probability << '\t' << scores[i] << '\t' << bonus << '\n';
        path.probability += static_cast<coral::phonetics::Score>(bonus);
        if (i < scored_limit && (ranker || (ranking_trace_path && *ranking_trace_path))) {
            const auto length = std::accumulate(path.pinyin.begin(), path.pinyin.end(), size_t{0},
                [](size_t n, const auto& syllable) { return n + syllable.size(); });
            ranking_inputs.push_back({base, scores[i], path.probability,
                static_cast<size_t>(characters(path.sentence)), length, path.ranking_path});
        }
    }
    if (!ranking_inputs.empty()) {
        const auto features = coral::language_model::make_ranking_features(ranking_inputs, characters(character_context_));
        if (ranking_trace_path && *ranking_trace_path) {
            std::ofstream feature_trace(std::filesystem::path(ranking_trace_path), std::ios::app);
            for (size_t i = 0; i < features.size(); ++i)
                coral::language_model::write_ranking_trace(feature_trace, lattice_->input_pinyin(), character_context_,
                    i, paths[i].sentence, features[i]);
        }
        if (ranker && scored && std::any_of(ranking_inputs.begin(), ranking_inputs.end(),
            [](const auto& input) { return std::isfinite(input.neural); })) {
            std::vector<double> predictions(features.size());
            bool valid = true;
            for (size_t i = 0; i < features.size(); ++i) valid = ranker->predict(features[i], predictions[i]) && valid;
            if (valid) {
                // 同拼音覆盖长度的候选之间置换原有排序槽，保持与混输、直选候选的分数量纲。
                std::vector<bool> assigned(features.size(), false);
                for (size_t i = 0; i < features.size(); ++i) {
                    if (assigned[i]) continue;
                    std::vector<size_t> group;
                    std::vector<double> slots;
                    for (size_t j = i; j < features.size(); ++j) {
                        if (ranking_inputs[j].letters != ranking_inputs[i].letters) continue;
                        assigned[j] = true; group.push_back(j); slots.push_back(paths[j].probability);
                    }
                    std::stable_sort(group.begin(), group.end(), [&](size_t a, size_t b) { return predictions[a] > predictions[b]; });
                    std::sort(slots.begin(), slots.end(), std::greater<double>());
                    for (size_t j = 0; j < group.size(); ++j) {
                        paths[group[j]].learned_order_score = slots[j];
                        paths[group[j]].learned_order_tie = static_cast<int>(j);
                    }
                }
            }
        }
    }
    rankSolutions(paths);
}

#endif

bool coral::phonetics::CandidateDecoder::get_parsed_res(
	vector<coral::phonetics::CandidateResult>& sents,
	const syllable_strview_map& /*syllables*/,
	coral::phonetics::ParseTextSet& outputedSentences,
	const coral::phonetics::ParseSequences& /*pinyinSequences*/,
	coral::phonetics::Score& shortestPathCost)
{
	if (lattice_ == nullptr || language_model_ == nullptr)
		return false;
	const auto& arcs = lattice_->get_arc_array();
	const int vertexCount = lattice_->get_vertex_count();
	if (vertexCount < 2)
		return false;

	// 先回收输入的 Solution 缓冲，避免路径解与混合解叠加导致内存翻倍。
	recycle_solutions(sents);

	vector<coral::phonetics::CandidateResult> pathSolutions;
    std::string best_mixed_sentence;
    shortestPathCost = kMaxWeight;
    if (!full_dictionary_hit_) {
        find_best_paths(pathSolutions, kPathCandidateLimit, shortestPathCost);
        if (has_edit_corrections_ && normal_dictionary_path_) {
            // 独立搜索正常组句，防止纠错占满解码器的候选与状态预算。
            vector<coral::phonetics::CandidateResult> normal_solutions;
            coral::phonetics::Score normal_cost = kMaxWeight;
            find_best_paths(normal_solutions, kPathCandidateLimit, normal_cost, false, true);
            for (auto& solution : normal_solutions) {
                const auto existing = std::find_if(pathSolutions.begin(), pathSolutions.end(), [&](const auto& item) {
                    return item.sentence == solution.sentence;
                });
                if (existing == pathSolutions.end()) pathSolutions.push_back(std::move(solution));
                else if (existing->is_corrected) *existing = std::move(solution);
            }
            if (normal_cost != kMaxWeight) shortestPathCost = normal_cost;
            rankSolutions(pathSolutions);
        }
#if CORAL_ENABLE_CHARACTER_MODEL
        rerank_character_paths(pathSolutions);
#endif
        vector<coral::phonetics::CandidateResult> mixedSolutions;
        coral::phonetics::Score mixedCost = kMaxWeight;
        find_best_paths(mixedSolutions, kPathCandidateLimit, mixedCost, true);
        shortestPathCost = std::min(shortestPathCost, mixedCost);
        // 混合检索独立排序，中文组句较优时也保留最佳混输；单个英文词由补全路径负责。
        best_mixed_sentence = !mixedSolutions.empty() && mixedSolutions.front().pinyin.size() > 1
            ? mixedSolutions.front().sentence : std::string{};
        for (auto& solution : mixedSolutions) pathSolutions.push_back(std::move(solution));
        rankSolutions(pathSolutions);
    }

	// 首词直选 = 起点出边中权值 0 的词条，直接取自 CSR 出边表。
	const int startBegin = csr_offset_[0];
	const int startEnd = csr_offset_[1];
	struct DirectRef
	{
		int arcIndex;
		std::string_view phrase;
		int pinyinCharLen;
		coral::phonetics::Score probability;
		bool forced;
	};
	std::vector<DirectRef, coral::phonetics::support::PoolAllocator<DirectRef>> directs;
	directs.reserve(static_cast<size_t>(startEnd - startBegin));
	const auto pinyinWeightOf = [](const int length) {
		return static_cast<coral::phonetics::Score>(1.0f / (1.0f + static_cast<coral::phonetics::Score>(length)));
	};
	{
		auto dedup = coral::phonetics::support::make_indexed_string_set(static_cast<size_t>(startEnd - startBegin),
			[&directs](const int32_t index) -> std::string_view { return directs[index].phrase; });
		using Dedup = decltype(dedup);
		for (int k = startBegin; k < startEnd; ++k)
		{
			const int arcIndex = edge_indices_[static_cast<size_t>(k)];
			if (arcIndex < 0 || arcIndex >= static_cast<int>(arcs.size()))
				continue;
			const coral::phonetics::CandidateEdgePayload& data = arcs[static_cast<size_t>(arcIndex)].data;
			if (!data.is_usable())
				continue;
            // 无主词典 ID 的用户词仍是真实命中，音节/英文默认回退则只能参与拼接路径。
			if (data.english_context_only || (data.phrase_id == kInvalidPhraseId && !data.dictionary_match))
				continue;
            if (data.phrase_id == kExtendedMatchId) continue;

            const auto& sequence = data.pinyin_sequence();
            if (full_dictionary_hit_ && !sequence.is_edit_corrected_ && (sequence.is_fuzzy_match_ || data.is_corrected))
                continue;
			const auto& uiPinyin = data.pinyin_sequence().get_ui_show_pinyin_sequence();
			if (uiPinyin.empty())
				continue;
			const auto probe = dedup.probe(data.phrase);
			if (probe.existing != Dedup::kNotFound)
				continue;
			const int pinyinLength = std::accumulate(uiPinyin.begin(), uiPinyin.end(), 0,
				[](const int sum, const std::string_view pinyin) {
					return sum + static_cast<int>(pinyin.size());
				});
            double direct_weight = data.weight;
            if (!full_dictionary_hit_) {
                direct_weight = condition_id_ == kInvalidPhraseId ? data.weight :
                    condition_weight(data.weight, std::max(data.weight,
                        language_model_->get_bigram(condition_id_, data.phrase_id)));
                if (const auto* surface = language_model_->surface_model()) {
                    coral::language_model::PhraseScore ordinary, conditional;
                    if (!data.pinyin_sequence().is_match_english_vocab_ &&
                        surface->score_phrase(0, data.phrase_id, ordinary)) {
                        const auto context = surface->phrase_end(condition_id_);
                        surface->score_phrase(context, data.phrase_id, conditional);
                        direct_weight = condition_weight(ordinary.log_probability, conditional.log_probability) +
                            model_input_penalty(data, *language_model_);
                    }
                }
                if (const auto* bonus = language_model_->pair_bonus_model()) {
                    const auto external = language_model_->pair_bonus_state(condition_id_);
                    const auto previous = external ? external : bonus->word_state("<s>");
                    direct_weight = data.weight + (external ? condition_influence_ : 1.0) * bonus->bonus(previous,
                        data.pinyin_sequence().is_match_english_vocab_ || data.english_context_only ?
                            0 : bonus->word_state(data.phrase));
                }
            }
            direct_weight += terminal_arc_bonuses_[arcIndex];
            if (full_dictionary_hit_ && is_full_dictionary_arc(arcs[static_cast<size_t>(arcIndex)], vertexCount - 1))
                shortestPathCost = std::min(shortestPathCost, static_cast<coral::phonetics::Score>(-direct_weight));
			directs.push_back(DirectRef{ arcIndex, data.phrase, pinyinLength,
                static_cast<coral::phonetics::Score>(direct_weight) * pinyinWeightOf(pinyinLength), false });
			dedup.commit(probe, static_cast<int32_t>(directs.size()) - 1);
		}
		for (const auto& path : pathSolutions)
		{
			const auto probe = dedup.probe(path.sentence);
			if (probe.existing != Dedup::kNotFound)
				directs[probe.existing].forced = true;
		}
	}

	const auto directRankLess = [&](const int lhs, const int rhs) {
		const DirectRef& a = directs[lhs];
		const DirectRef& b = directs[rhs];
		if (a.pinyinCharLen != b.pinyinCharLen)
			return a.pinyinCharLen > b.pinyinCharLen;
        const bool a_supplement = normal_dictionary_path_ &&
            (arcs[a.arcIndex].data.is_corrected || arcs[a.arcIndex].data.pinyin_sequence().is_edit_corrected_);
        const bool b_supplement = normal_dictionary_path_ &&
            (arcs[b.arcIndex].data.is_corrected || arcs[b.arcIndex].data.pinyin_sequence().is_edit_corrected_);
        if (a_supplement != b_supplement) return !a_supplement;
		if (a.probability != b.probability)
			return a.probability > b.probability;
		return lhs < rhs;
	};
	coral::phonetics::ParseIntegers picked(directs.size());
	std::iota(picked.begin(), picked.end(), 0);
	candidates_truncated_ = direct_candidate_budget_ >= 0 &&
		picked.size() > static_cast<size_t>(direct_candidate_budget_);
	if (candidates_truncated_)
	{
		const auto cut = picked.begin() + direct_candidate_budget_;
		std::nth_element(picked.begin(), cut, picked.end(), directRankLess);
		picked.erase(std::remove_if(cut, picked.end(),
			[&directs](const int index) { return !directs[index].forced; }), picked.end());
		std::sort(picked.begin(), picked.end());
	}

	// 直连弧都是词库真实命中（完整词或从输入开头命中的前缀词），需要按原规则保留；
	// 模型拼句按未展开窗口的容量取前若干条，短句可多取，词库命中仍全部保留。
	// 最佳混输沿用优先保留规则，但同样必须占用这一行的容量。
	sents.reserve(picked.size() + std::min<size_t>(pathSolutions.size(),
        coral::composition::CandidateRowMetrics::kCollapsedMaxColumns));
	{
		auto dedup = coral::phonetics::support::make_indexed_string_set(picked.size() + pathSolutions.size(),
			[&sents](const int32_t index) -> std::string_view { return sents[index].sentence; });
		using Dedup = decltype(dedup);
		for (const int directIndex : picked)
		{
			const DirectRef& ref = directs[directIndex];
			const coral::phonetics::CandidateEdgePayload& data = arcs[static_cast<size_t>(ref.arcIndex)].data;
			const auto& uiPinyin = data.pinyin_sequence().get_ui_show_pinyin_sequence();
			coral::phonetics::CandidateResult solution = take_solution();
			solution.pinyin.reserve(uiPinyin.size());
			solution.pinyinview.reserve(uiPinyin.size());
			solution.pinyin.insert(solution.pinyin.end(), uiPinyin.begin(), uiPinyin.end());
			solution.pinyinview.insert(solution.pinyinview.end(), uiPinyin.begin(), uiPinyin.end());
			solution.sentence = data.phrase;
			solution.probability = ref.probability;
			solution.word_size = data.pinyin_sequence().get_pinyin_length();
			solution.is_corrected = data.is_corrected || data.pinyin_sequence().is_edit_corrected_;
            solution.is_correction_supplement = normal_dictionary_path_ && solution.is_corrected;
			if (data.pinyin_sequence().is_match_english_vocab_)
				solution.transform_flags = solution.transform_flags | MiddlewareType::NoStoreUserLexicon;
			const auto probe = dedup.probe(solution.sentence);
			sents.push_back(std::move(solution));
			dedup.commit(probe, static_cast<int32_t>(sents.size()) - 1);
		}
        coral::composition::ComposedCandidateCapacity composed_capacity;
        const CandidateResult* first_composed = nullptr;
        for (const auto& path : pathSolutions) {
            if (!path.sentence.empty() && path.probability != kLogInfiniteSimal &&
                dedup.probe(path.sentence).existing == Dedup::kNotFound) {
                first_composed = &path;
                composed_capacity.try_append(path.sentence);
                break;
            }
        }
        bool reserved_mixed = false;
        if (first_composed && !best_mixed_sentence.empty() && first_composed->sentence != best_mixed_sentence &&
            dedup.probe(best_mixed_sentence).existing == Dedup::kNotFound) {
            auto with_mixed = composed_capacity;
            if (with_mixed.try_append(best_mixed_sentence)) {
                composed_capacity = with_mixed;
                reserved_mixed = true;
            }
        }
		bool appendedBestComposed = false;
		for (auto& solution : pathSolutions)
		{
			if (solution.sentence.empty() || solution.probability == kLogInfiniteSimal)
				continue;
			const auto probe = dedup.probe(solution.sentence);
			if (probe.existing == Dedup::kNotFound)
			{
				if (&solution != first_composed && !(reserved_mixed && solution.sentence == best_mixed_sentence) &&
                    !composed_capacity.try_append(solution.sentence))
					continue;
				solution.is_additional_mixed = appendedBestComposed;
				sents.push_back(std::move(solution));
				dedup.commit(probe, static_cast<int32_t>(sents.size()) - 1);
				appendedBestComposed = true;
			}
			else if (comp(solution, sents[probe.existing]))
				sents[probe.existing] = std::move(solution);
		}
	}

	// 回收 sents 中的解对象，控制输出阶段的内存峰值。
	recycle_solutions(pathSolutions);

	rankSolutions(sents);
	outputedSentences.reserve(outputedSentences.size() + directs.size() + sents.size());
	for (const DirectRef& direct : directs)
		outputedSentences.emplace(direct.phrase);
	for (const coral::phonetics::CandidateResult& solution : sents)
		if (!solution.sentence.empty())
			outputedSentences.insert(solution.sentence);

	if (full_dictionary_hit_ || extended_match_.empty() || sents.empty() || sents.front().sentence == extended_match_ ||
		sents.front().pinyin.size() < kMinPinyinLengthForExtendedMatch ||
		outputedSentences.count(extended_match_))
		return !sents.empty();

	coral::phonetics::CandidateResult extended;
	extended.sentence = extended_match_;
	extended.pinyin = sents.front().pinyin;
	extended.probability = sents.front().probability;
	extended.word_size = sents.front().word_size;
	extended.transform_flags = extended.transform_flags | MiddlewareType::NoStoreUserLexicon;
	extended.is_corrected = false;
	sents.insert(sents.begin() + 1, std::move(extended));
	outputedSentences.insert(extended_match_);
	return true;
}

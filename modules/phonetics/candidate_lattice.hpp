/*
* @brief: 扁平词格（word lattice），替代原 CLexiconGraph 的 CGraphT 顶点+邻接表重图。
*         弧数据一次性收集为扁平数组，解码器（CLanguageDecoder）以 CSR 按位置
*         自行分桶组织，纯 WSFA 组合态 (position, lastPhraseId) 推进搜索，
*         参考 proclass_coralpinyin 的 LatticeArc + FstDecoder 架构。
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_CANDIDATE_LATTICE_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_CANDIDATE_LATTICE_HPP_INCLUDED

#include <deque>

#include "phonetic_types.h"
#include "lattice_payload.hpp"
#include "lexicon_catalog.h"
#include "phonetic_sequence.hpp"
#include "../runtime/runtime_context.h"	// 配置快照（limit_count / isShowInfos）
#include "syllable_catalog.h"
#include "script_conversion.h"
#include "support/parse_metrics.hpp"

// 词格弧：from/to 为音节图顶点位置（字符位置），data 为词条数据。
namespace coral::phonetics {
struct CandidateArc
{
	int from = -1;
	int to = -1;
	coral::phonetics::CandidateEdgePayload data;

	CandidateArc() = default;
	explicit CandidateArc(const int fromIndex, const int toIndex)
		: from(fromIndex), to(toIndex) {}
	CandidateArc(const CandidateArc&) = delete;
	CandidateArc(CandidateArc&&) noexcept = default;
	CandidateArc& operator=(const CandidateArc&) = delete;
	CandidateArc& operator=(CandidateArc&&) noexcept = default;
};
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
class CandidateLattice
{
public:
	CandidateLattice() = default;
	~CandidateLattice() = default;
	std::string_view extended_match_;

	// 逐 span 的取词上限，负数表示不设限。
	//
	// 旧模型取这个上限的依据是直连候选的组装预算：getParsedRes 按 directRankLess 挑前 budget 条，
	// 该比较器先比拼音字符数、再比 probability，而同一 span 内的弧拼音字符数相同、probability
	// 与词典权重同序，所以每个 span 里排在 budget 名之后的词条不可能进入最终的前 budget 条。
	// 用户翻页越过这个深度时 getParseMode 会转入 Reparse，那一轮预算是 -1，这里也随之不设限，
	// 长尾候选照常完整召回。
	// 新概率模型与词典权重不同序，上述同序论证不适用；当前仍保留上限，须结合评分校准另行验收。
	void set_span_fetch_cap(const int cap) { span_fetch_cap_ = cap; }

	bool init_graph(
		int vertexCount,
		const string_view pinyin,
		coral::phonetics::LexiconCatalog& lexicon)
	{
#if LANGENGINE_PERF_COUNTERS
		dup_probe_.clear();
#endif
		default_phrases_.clear();
		english_persistent_strings_.clear();
		shared_sequences_.clear();
		lexicon.reset_storage();
		// TODO：调整条件输入的启用条件和权重计算方式。比如在较长时才启用，或者确保匹配的词语不能超过如 的 了 等常用词的权重
		// 避免用户的简拼体验被感染。    如：  森林 + l， 此时首位应该还是 了 ， 而不是 森林狼 的 狼
		max_pinyin_ = pinyin;
		vertex_count_ = vertexCount;
		arcs_.clear();
		// 弧数随输入长度快速增长，按顶点数给一个够用的下限，省掉前若干次扩容搬移
		arcs_.reserve(std::max<size_t>(256, 64 * static_cast<size_t>(vertexCount)));
		return true;
	}

	// spanHitDictPhrases：本次查询是覆盖整个输入（from==0 && to==末顶点）的非模糊序列，
	// 且词典返回了真实词条（不含英文回退、默认词、EXTENDED_MATCH 扩展匹配）时置 true。
	// 仅旧模型可沿用全句命中剪枝。概率模型的组合路径可能优于完整词条，调用方须禁用该捷径。
	bool process_sequence(coral::phonetics::PhoneticSequence& sequence,
		coral::phonetics::LexiconCatalog& lexicon,
		const std::unordered_set<std::string_view>& fuzzyOnlySyllables,
		bool shouldLimit,
		bool& sentHitDict,
		bool& isHitEnglish,
		bool& spanHitDictPhrases,
		bool english_context_only = false)
	{
		const size_t firstArc = arcs_.size();
		spanHitDictPhrases = false;
		const coral::ConfigSnapshot& config = coral::Engine::compatInstance().config();
		coral::phonetics::ParsePhrases phrases;
		bool hasSubSequence = !sequence.sub_sequences_.empty();
			// 混拼的情况下，查询子码，同时通过主码做比对，判断是否匹配
			// 例如主码 z'guo'r （中国人）子码 z'g'r, 子码取到 zhuan'gao'ren（撰稿人）需要剔除
		// 缓冲复用成员：这个函数每个 span 调一次，长输入下一次解析要来几十趟
		std::string& currentPinyin = current_pinyin_;
		currentPinyin.clear();
		for (const auto& py : sequence.sequences_)
		{
			if (!sequence.is_match_english_vocab_ && fuzzyOnlySyllables.count(py))
				return false;
			currentPinyin += py;
		}
		if (sequence.is_match_english_vocab_) {
			const bool full_input = sequence.from_ == 0 && sequence.to_ == vertex_count_ - 1;
			if (!full_input && !coral::phonetics::LatinLexicon::allows_mixed_word(currentPinyin))
				return true;
			coral::phonetics::LexiconPhrase english;
			if (!lexicon.get_eng_word_info(currentPinyin, english)) return true;
			std::string display(english.phrase);
			for (size_t i = 0; i < currentPinyin.size() && i < display.size(); ++i)
				if (currentPinyin[i] >= 'A' && currentPinyin[i] <= 'Z' && display[i] >= 'a' && display[i] <= 'z')
					display[i] += 'A' - 'a';
			if (display != english.phrase) {
				english_persistent_strings_.push_back(std::move(display));
				english.phrase = english_persistent_strings_.back();
			}
			const auto& shared = share_sequence(sequence);
			arcs_.emplace_back(sequence.from_, sequence.to_);
			arcs_.back().data.emplace(english.phrase, english.id, static_cast<int>(arcs_.size()), shared, english.phrase_weight);
			isHitEnglish = true;
			return true;
		}
		int limit_count = -1;
		if (shouldLimit)	// 如果是输入的完整拼音串，则不限制数量
			limit_count = sequence.get_limit();
		if (span_fetch_cap_ > 0 && (limit_count < 0 || limit_count > span_fetch_cap_))
			limit_count = span_fetch_cap_;
		lexicon.collect_candidates(sequence,
			phrases,
			limit_count,
			config.isShowInfos(),
			false,
			sequence.is_abbreviatehas_full_,
			(currentPinyin.size() == max_pinyin_.size()));
		if (!process_phrases(phrases,
			sequence,
			lexicon,
			hasSubSequence,
			config.limit_count(),
			vertex_count_,
			config.isShowInfos(),
			fuzzyOnlySyllables,
			currentPinyin,
			sentHitDict,
			isHitEnglish,
			spanHitDictPhrases)) {
			return false;
		}
		if (english_context_only)
			for (size_t i = firstArc; i < arcs_.size(); ++i) arcs_[i].data.english_context_only = true;
		return true;
	}

	[[nodiscard]] bool process_phrases(const coral::phonetics::ParsePhrases& phrases,
		coral::phonetics::PhoneticSequence& sequence,
		const coral::phonetics::LexiconCatalog& lexicon,
		bool hasSubSequence,
		int limit,
		int vertexCount,
		bool isShowInfos,
		const std::unordered_set<std::string_view>& fuzzyOnlySyllables,
		const std::string& currentPinyin,
		bool& sentHitDict,
		bool& isHitEnglish,
		bool& spanHitDictPhrases)
	{
		if (phrases.empty())
		{
			if (!sequence.is_fuzzy_match_ && !sequence.is_edit_corrected_
				&& (sequence.from_ != 0 || sequence.to_ != vertexCount - 1))
			{
				if (coral::phonetics::LatinLexicon::allows_mixed_word(currentPinyin)
					&& lexicon.get_english_lexicon().is_word_in_lexicon(currentPinyin))
				{
					english_persistent_strings_.push_back(currentPinyin);
					sequence.is_match_english_vocab_ = true;
					sequence.sequences_.clear();
					sequence.sequences_.push_back(english_persistent_strings_.back());
				}
			}

			if (sequence.sequences_.size() != 1)
				return true;
			std::string_view defaultPhrase;
			if (!sequence.original_sequences_.empty()) {
				defaultPhrase = sequence.original_sequences_[0];
			} else {
				defaultPhrase = sequence.sequences_[0];
			}
			int from_vertex_index = sequence.from_;
			int to_vertex_index = sequence.to_;
			if (from_vertex_index < 0 || from_vertex_index >= vertex_count_ ||
				to_vertex_index < 0 || to_vertex_index >= vertex_count_)
				return false;

			coral::phonetics::LexiconPhrase info;
			const bool full_input = sequence.from_ == 0 && sequence.to_ == vertexCount - 1;
			auto found = (full_input || coral::phonetics::LatinLexicon::allows_mixed_word(defaultPhrase))
				&& lexicon.get_eng_word_info(defaultPhrase, info);
			if (found && sequence.from_ == 0 && sequence.to_ == vertexCount - 1) {
				return true;
			}
			if (default_phrases_.empty()) {
				default_phrases_.reserve(100);
			}
			auto realPhrase = info.phrase;
			auto weight = info.phrase_weight;
			if (!found || sequence.is_fuzzy_match_ || sequence.is_edit_corrected_)
			{
				weight = kDefaultLexiconEdgeWeight;
				realPhrase = defaultPhrase;
			}
			else
			{
				isHitEnglish = true;
#ifdef _DEBUG
				std::cout << "[Eng]: find default english word " << realPhrase << " with weight " << weight << std::endl;
#endif
			}

			default_phrases_.push_back(realPhrase);
			// 序列在上面的英文回退分支里可能被改写，因此到这里才取共享副本
			const coral::phonetics::PhoneticSequence& sharedSequence = share_sequence(sequence);
			// 给默认边更低的优先级（更差的权重 -30.0），但仍保持可达
			arcs_.emplace_back(from_vertex_index, to_vertex_index);
			if (isShowInfos)
				arcs_.back().data.emplace(default_phrases_.back(), found && !sequence.is_fuzzy_match_ && !sequence.is_edit_corrected_ ? info.id : kInvalidPhraseId, static_cast<int>(arcs_.size()), sharedSequence, weight, false);
			else
				arcs_.back().data.emplace(default_phrases_.back(), found && !sequence.is_fuzzy_match_ && !sequence.is_edit_corrected_ ? info.id : kInvalidPhraseId, static_cast<int>(arcs_.size()), sharedSequence, weight);
			return true;
		}
		int num = 0;
		// 全串命中信号只对覆盖整个输入的非模糊查询有效
		const bool isFullSpanQuery = (sequence.from_ == 0 && sequence.to_ == vertexCount - 1
				&& !sequence.is_fuzzy_match_ && !sequence.is_edit_corrected_);
		// 本次调用产出的所有弧共享同一份拼音序列快照
		const coral::phonetics::PhoneticSequence& sharedSequence = share_sequence(sequence);

		for (auto phraseIter = phrases.begin(); phraseIter != phrases.end(); ++phraseIter)
		{
			int from_vertex_index = sequence.from_;
			int to_vertex_index = sequence.to_;
			// 方案B第二阶段：全句命中信号排除模糊/编辑序列，保持与非纠错模式同口径，
			// 避免编辑纠错的全串命中让 sentHitDict 置位、静默关闭旧纠错门控
			bool isFullSentenceMatch = (from_vertex_index == 0 && to_vertex_index == vertexCount - 1
				&& !sequence.is_fuzzy_match_ && !sequence.is_edit_corrected_);
			if (!hasSubSequence && phraseIter->id == kExtendedMatchId)
			{
				extended_match_ = phraseIter->phrase;
				continue;
			}
			if (sequence.from_ && num > limit)
				break;
			if (phraseIter->id == kExtendedMatchId)
			{
				// 非条件词、扩展搜索的项已经在词典搜索阶段筛选过了
				if (!coral::phonetics::syntax::compare_phrase_pinyin(sequence.sequences_, phraseIter->phrase_pinyin, phraseIter->id == kExtendedMatchId))
					continue;
			}
			if (from_vertex_index < 0 || from_vertex_index >= vertex_count_ ||
				to_vertex_index < 0 || to_vertex_index >= vertex_count_)
				return false;

			// 模糊音需要进行惩罚，其中全拼进行轻微的惩罚，简拼进行严重的惩罚
			coral::phonetics::Score penalty = 0;
			if (sequence.is_fuzzy_match_)
			{
				penalty = sequence.sub_sequences_.empty() ? kFuzzyPenaltyQuan : kFuzzyPenaltyJian;
			}
			else if (sequence.is_edit_corrected_)
			{
				// 方案B第二阶段：编辑纠错命中的词条加惩罚，同等词频下精确匹配优先
				penalty = kEditPenalty;
			}

			sentHitDict = sentHitDict || isFullSentenceMatch;
			num++;
			// 扩展匹配不是词典词条，混拼全串查询时它也会建弧，这里排除掉，
			// 保证剪枝只在真正命中词典完整词时触发
			if (isFullSpanQuery && phraseIter->id != kExtendedMatchId)
				spanHitDictPhrases = true;

			PERF_INC(arcsCreated);
#if LANGENGINE_PERF_COUNTERS
			{
				const size_t base = std::hash<std::string_view>{}(phraseIter->phrase) ^
					(static_cast<size_t>(from_vertex_index) << 40) ^ (static_cast<size_t>(to_vertex_index) << 52);
				if (!dup_probe_.insert(base).second)
					PERF_INC(arcDupSamePhrase);
				uint32_t weightBits = 0;
				const coral::phonetics::Score w = phraseIter->phrase_weight + penalty;
				std::memcpy(&weightBits, &w, sizeof(weightBits));
				if (!dup_probe_.insert(base ^ (static_cast<size_t>(weightBits) << 8) ^ 0x5bf03635u).second)
					PERF_INC(arcDupSameAll);
			}
#endif
			arcs_.emplace_back(from_vertex_index, to_vertex_index);
			arcs_.back().data.emplace(phraseIter->phrase, phraseIter->id, static_cast<int>(arcs_.size()), sharedSequence,
				phraseIter->phrase_weight + penalty, phraseIter->is_corrected);
            arcs_.back().data.dictionary_match = phraseIter->id != kExtendedMatchId;
		}
		return true;
	}

    [[nodiscard]] std::string_view input_pinyin() const { return max_pinyin_; }
	[[nodiscard]] int get_vertex_count() const
	{
		return vertex_count_;
	}

	[[nodiscard]] const std::vector<coral::phonetics::CandidateArc>& get_arc_array() const
	{
		return arcs_;
	}

	bool write_to_file(const std::filesystem::path &filepath) const
	{
		std::error_code ec;
		std::filesystem::create_directories(filepath.parent_path(), ec);
		if (ec)
			return false;

		std::ostringstream buffer;
		buffer << std::setprecision(std::numeric_limits<coral::phonetics::Score>::max_digits10);

		buffer << "[BASIC_INFO]\n";
		buffer << "vertex_count=" << vertex_count_ << '\n';
		buffer << "arc_count=" << arcs_.size() << '\n';
		buffer << "extended_match=" << extended_match_ << '\n';
		buffer << '\n';

		buffer << "[MAX_PINYIN]\n";
		buffer << max_pinyin_ << '\n';

		buffer << "[DEFAULT_PHRASES]\n";
		for (const auto &phrase : default_phrases_)
			buffer << phrase << '\n';
		buffer << '\n';

		// 弧按起点位置分组输出（原 VERTICES 邻接表信息由此派生）
		std::vector<std::vector<int>> out_arcs(static_cast<size_t>(vertex_count_));
		std::vector<std::vector<int>> in_arcs(static_cast<size_t>(vertex_count_));
		for (size_t i = 0; i < arcs_.size(); ++i)
		{
			out_arcs[static_cast<size_t>(arcs_[i].from)].push_back(static_cast<int>(i));
			in_arcs[static_cast<size_t>(arcs_[i].to)].push_back(static_cast<int>(i));
		}

		buffer << "[VERTICES]\n";
		for (size_t i = 0; i < static_cast<size_t>(vertex_count_); ++i)
		{
			buffer << "vertex_" << i << ": out_arcs=[";
			for (size_t j = 0; j < out_arcs[i].size(); ++j)
			{
				if (j > 0)
					buffer << ',';
				buffer << out_arcs[i][j];
			}
			buffer << "], in_arcs=[";
			for (size_t j = 0; j < in_arcs[i].size(); ++j)
			{
				if (j > 0)
					buffer << ',';
				buffer << in_arcs[i][j];
			}
			buffer << "]\n";
		}
		buffer << '\n';

		buffer << "[ARCS]\n";
		for (size_t i = 0; i < arcs_.size(); ++i)
		{
			const auto &arc = arcs_[i];
			const auto &pySeq = arc.data.pinyin_sequence();

			buffer << "arc_" << i << ": from=" << arc.from << ", to=" << arc.to << ", phrase=\""
				   << std::string(arc.data.phrase.data(), arc.data.phrase.size()) << "\""
				   << ", phrase_id=" << arc.data.phrase_id << ", id=" << arc.data.id
				   << ", weight=" << arc.data.weight << '\n';

			buffer << "  pinyin: from=" << pySeq.from_ << ", to=" << pySeq.to_ << ", nodes=" << pySeq.path_nodes_
				   << ", english=" << (pySeq.is_match_english_vocab_ ? 1 : 0)
				   << ", fuzzy=" << (pySeq.is_fuzzy_match_ ? 1 : 0) << '\n';

			auto write_sequence_list = [&buffer](const std::string &name, const coral::phonetics::ParseTextViews &sequences)
			{
				buffer << "  " << name << ": [";
				for (size_t j = 0; j < sequences.size(); ++j)
				{
					if (j > 0)
						buffer << ", ";
					buffer << "\"" << sequences[j] << "\"";
				}
				buffer << "]\n";
			};

			write_sequence_list("sequences", pySeq.sequences_);
			write_sequence_list("sub_sequences", pySeq.sub_sequences_);
			write_sequence_list("show_sequences", pySeq.show_sequences_);
			write_sequence_list("original_sequences", pySeq.original_sequences_);
			buffer << '\n';
		}

		std::ofstream file{filepath};
		if (!file.is_open())
			return false;

		file << buffer.str();
		return true;
	}

	bool export_to_dot(const std::filesystem::path &filepath) const
	{
#ifdef _DEBUG
		std::error_code ec;
		std::filesystem::create_directories(filepath.parent_path(), ec);
		if (ec)
			return false;

		std::ostringstream buffer;
		buffer << std::setprecision(6);

		buffer << "digraph LexiconGraph {\n";
		buffer << "    charset=\"UTF-8\";\n";
		buffer << "    rankdir=LR;\n";
		buffer << "    node [shape=box, fontname=\"Microsoft YaHei\"];\n";
		buffer << "    edge [fontname=\"Microsoft YaHei\"];\n\n";

		// 输出节点
		for (int i = 0; i < vertex_count_; ++i)
		{
			buffer << "    V" << i << " [label=\"" << i << "\"];\n";
		}
		buffer << "\n";

		// 按 pinyinSequence 分组弧
		coral::phonetics::ParseOccurrenceMap pinyinGroups;
		for (size_t i = 0; i < arcs_.size(); ++i)
		{
			const auto &arc = arcs_[i];
			std::string pinyinKey;
			for (const auto &py : arc.data.pinyin_sequence().sequences_)
			{
				if (!pinyinKey.empty()) pinyinKey += "'";
				pinyinKey += std::string(py);
			}
			pinyinGroups[pinyinKey].push_back(i);
		}

		// 输出分组的弧
		constexpr size_t MAX_ARCS_PER_GROUP = 10;
		for (const auto &[pinyinKey, arcIndices] : pinyinGroups)
		{
			size_t displayCount = std::min(arcIndices.size(), MAX_ARCS_PER_GROUP);
			if (arcIndices.size() > 1)
			{
				// 多条弧具有相同拼音，添加注释分组
				buffer << "    // Group: " << pinyinKey << " (" << displayCount << "/" << arcIndices.size() << " arcs)\n";
			}
			for (size_t i = 0; i < displayCount; ++i)
			{
				size_t idx = arcIndices[i];
				const auto &arc = arcs_[idx];
				std::string phrase_str(arc.data.phrase.data(), arc.data.phrase.size());
				// 转义双引号
				for (size_t j = 0; j < phrase_str.size(); ++j)
				{
					if (phrase_str[j] == '"')
					{
						phrase_str.insert(j, "\\");
						++j;
					}
				}
				buffer << "    V" << arc.from << " -> V" << arc.to
					   << " [label=\"" << phrase_str << "\\n" << arc.data.weight << "\"];\n";
			}
			if (arcIndices.size() > 1)
			{
				buffer << "\n";
			}
		}

		buffer << "}\n";

		std::ofstream file{filepath, std::ios::binary};
		if (!file.is_open())
			return false;

		file << buffer.str();
		return true;
#endif
		return false;
	}

private:
	// 存一份拼音序列快照并返回稳定引用。deque 在尾部追加时不会失效已有元素的引用，
	// 弧只保存指针即可。快照语义与逐弧拷贝时一致：取快照之后调用方再改动原序列不影响词格。
	const coral::phonetics::PhoneticSequence& share_sequence(const coral::phonetics::PhoneticSequence& sequence)
	{
		shared_sequences_.push_back(sequence);
		return shared_sequences_.back();
	}

#if LANGENGINE_PERF_COUNTERS
	std::unordered_set<size_t> dup_probe_;
#endif
	int vertex_count_ = 0;
	int span_fetch_cap_ = -1;
	coral::phonetics::ParseTextViews default_phrases_;
	string_view max_pinyin_;
	std::deque<std::string> english_persistent_strings_;
	std::string current_pinyin_;
	std::deque<coral::phonetics::PhoneticSequence, coral::phonetics::support::PoolAllocator<coral::phonetics::PhoneticSequence>> shared_sequences_;
	std::vector<coral::phonetics::CandidateArc> arcs_;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // WORDLATTICE_HPP（词格头文件）

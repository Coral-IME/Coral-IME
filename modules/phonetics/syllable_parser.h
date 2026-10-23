/*
* @author: xuxiang
* @date: 2021-6-10 17:12:57
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_SYLLABLE_PARSER_H_INCLUDED
#define CORAL_MODULES_PHONETICS_SYLLABLE_PARSER_H_INCLUDED
#include "background_tasks.h"
#include "syllable_lattice.h"
#include "lexicon_catalog.h"
#include "candidate_lattice.hpp"
#include "context_scorer.h"
#include "candidate_decoder.h"
#include "candidate_transforms.h"
#include "lexicon_source.h"
#include "spelling_repair.h"
#include <set>
#include <vector>
#include <string>

constexpr int MAX_NORMAL_CANDIDATES = 100; // 普通候选词最大数量
constexpr int MAX_CUSTOM_CANDIDATES = 10; // 自定义候选词最大数量

struct StrInfo
{
	int hot = 0;
	string		   pinyin;
	vector<string> words;
	vector<string> pyRes;
};

enum class LoadResult
{
	Success,		// 所有关键模块加载成功
	PartialFailure, // 非关键模块失败
	Failure			// 关键模块失败，无法继续
};

[[nodiscard]] inline std::string to_string(LoadResult result)
{
	switch (result)
	{
		case LoadResult::Success:
			return "Success";
		case LoadResult::PartialFailure:
			return "PartialFailure";
		case LoadResult::Failure:
			return "Failure";
		default:
			return "Unknown";
	}
}

enum class ParseMode
{
	CacheOnly,		// 仅使用缓存结果，普通翻页时
	Reparse,	// 重新解析并使用缓存结果，翻页到末尾且需要重新解析的输入
	NewParseOnly		// 仅使用新解析结果，新的输入或缓存为空时
};

struct PinyinParseContext
{
	coral::LanguageVariant mainType = coral::LanguageVariant::Simplified;
	string pinyin{};
	string englishPinyin{};
	int fuzzyLimit = 3;
	bool isDoublepinyin = false;

	bool isFullPinyinMatch = false;
	bool sentHitDict = false;
	coral::phonetics::Score shortestPathCost = kMaxWeight;	// 正值，越小表示路径越优
	ParseMode parseMode = ParseMode::NewParseOnly;
	// 直连候选组装预算的显式覆盖，0 = 按 parseMode 自动决定。
	// 供 write_parse_trace 这类需要全量导出的调试工具置为 UNLIMITED_DIRECT_CANDIDATES，
	// 不影响词库查询条数（shouldLimit），只放开 getParsedRes 的组装上限。
	int directCandidateBudgetOverride = 0;

	// 最终直连候选组装预算；新概率排序与词频不同序，不能据此证明召回安全。
	[[nodiscard]] int direct_candidate_budget() const
	{
		if (directCandidateBudgetOverride != 0)
			return directCandidateBudgetOverride;
		return parseMode == ParseMode::Reparse
			? kUnlimitedDirectCandidates
			: kFirstPassDirectCandidateBudget;
	}

	coral::phonetics::ParseSequences pinyinSequences;
	vector<coral::phonetics::CandidateResult> sents;

	coral::phonetics::SyllableLattice syllableGraph;
	coral::phonetics::CandidateLattice wordLattice;
	coral::phonetics::CandidateDecoder languageDecoder;
};

namespace coral::assist {
class ShapeHandler;
} // 命名空间 coral::assist 结束（批次 2 辅助输入）

namespace coral::assist {
class SymbolHandler;
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


namespace coral::phonetics {
class SyllableParser
{
public:
	static SyllableParser& instance();
    // 仅序列化运行时边界；所有借出的视图必须已停止使用。
    static void release_instance();
	void is_exist_next_page(bool& existNextPage);
	const string& candidate_display_text(int index) const;
	// 异步返回仅表示任务已接收；首次解析等待整批完成，失败不发布就绪状态。
	// retryUntilSuccess 允许解析入口在失败后同步重试；显式 load 可重新发起加载。
	bool load(const coral::phonetics::LexiconLocations &path, bool bSync, bool retryUntilSuccess = false,
        bool smart_candidates = true);

	// 拼音解析流程拆分
	bool get_pinyin_sequence(PinyinParseContext& context);
	void apply_fuzzy_replace(PinyinParseContext& context);
	bool init_language_decoder(PinyinParseContext& context);
	bool get_parsed_result(PinyinParseContext& context);

	bool parse_input(
		const string& pystring,
		vector<string>& res,
		vector<string>& pyRes,
		vector<int>& isFixedRes,
		int offset,
		int solutionSize,
		int asciiMode,
		uint32_t conditionId = kInvalidPhraseId, const std::string& condition = {});
	void update_correction_res(const vector<coral::phonetics::syntax::CorrectionResult>& correctionRes);
	void clear_pinyin_model_cache();
	// 丢弃候选缓存前把 Solution 连同已分配好的缓冲收进 m_solutionPool
	void recycle_sents_cache();

	bool update_user_lexicon(
		const string& py,
		const string& sent);
	bool store_lexicon() const;
    bool process_candidate_action(const string& pinyin, const string& word,
                              const string& fullPinyin, coral::CandidateAction action,
                              coral::LanguageVariant variant);

	bool clear_lexicon();
	bool load_user_lexicon_by_line(
		const string& pinyinCode,
		const string& quanPyCode,
		const string& sent,
		coral::phonetics::Score dee,
		int tickTime);
	void add_english_lexicon_results(vector<coral::phonetics::CandidateResult>& sentsCache, const string& originalInput, bool sentHitDict, coral::phonetics::Score shortestPathCost, coral::phonetics::ParseTextSet& outputSentences);
	void begin_load_user_lexicon_by_line();
	void end_load_user_lexicon_by_line();
	bool process_mode_with_state_machine(const string& pystring, vector<coral::phonetics::CandidateResult>& sentsCache,const int& asciiMode);
	void fuzzy_replace(coral::phonetics::ParseSequences& pinyinSequences, coral::phonetics::CandidateLattice& wordLattice, coral::phonetics::LexiconCatalog& lexiconmanager, bool& sentHitDict, const std::unordered_set<std::string_view>& fuzzyOnlySyllables, const bool shouldLimit) const;
	std::vector<coral::phonetics::syntax::CorrectionResult> generate_correction_string(const std::string& pystring, size_t limit);
	void get_selected_update_target(
		const string& sent,
		string& updateSent,
		int& selectedIndex) const;
	uint32_t capture_condition_selection(int selectedIndex);
	void check_update_lexicon(
		const std::string& sent,
		std::string& updateSent,
		int& selectedIndex);
	bool maintain_lexicon();
	hlp::ImportResult import_user_lexicon(const filesystem::path& lexiconPath);
	bool export_user_lexicon(const filesystem::path& lexiconPath);
	bool revert_last_action_for_phrase(const std::string& delPhrase);
	void stop_background_work();

	coral::phonetics::LexiconCatalog& get_lexicon_manager() { return lexicon_manager_; }

	static bool export_pinyin_sequences_to_dot(
		const coral::phonetics::ParseSequences& pinyinSequences,
		const std::filesystem::path& filepath);
protected:
	SyllableParser();
	~SyllableParser();

private:
    struct InstanceDeleter { void operator()(SyllableParser* parser) const; };
    static std::unique_ptr<SyllableParser, InstanceDeleter>& instance_storage();
	void get_page_res(int solutionSize, vector<coral::phonetics::CandidateResult>& sents, vector<string>& res, vector<string>& pyRes, vector<int>& isFixedRes, int offset, ParseMode parseMode);
	string adjust_candidate_to_boundaries(const string& candidate, const string& originalInput);
	void adjust_custom_phrases_position(vector<coral::phonetics::CandidateResult>& sentsCache, const string& originalPinyin);
	bool is_pinyin_full_match(const coral::phonetics::CandidateResult& solution, const string& originalPinyinNoSep);
	void parse_post_middleware(
		vector<coral::phonetics::CandidateResult>& sents,
		coral::phonetics::ParseTextSet& outputedSentences,
		const string& pystring,
		coral::LanguageVariant mainType);
	void build_fuzzy_map();
	bool load_primary_lexicon();
	bool load_pinyin_model();

	void add_ban_and_top_word_results(const string& pinyin, int vertexCount, vector<coral::phonetics::CandidateResult>& sents, coral::phonetics::ParseTextSet& outputSentences, const coral::phonetics::ParseSequences& pinyinSequences, coral::LanguageVariant mainType);
	void generate_correction_candidates(std::vector<coral::phonetics::syntax::CorrectionResult>& correctionRes
		, vector<coral::phonetics::CandidateResult>& sents
		, coral::phonetics::ParseTextSet& outputSentences
		, const std::string& originalPystring
		, coral::LanguageVariant mainType
		, bool bHitDict
		, bool bMixPinyins
		, float score);
	std::pair<std::string, coral::phonetics::Score> find_best_sentence_path(const std::vector<coral::phonetics::syntax::CorrectionResult>& correctionResults
		, std::vector<std::string>& originalSequences
		, const std::string& originalPystring
		, bool& hasHitMainDict
		, bool& isInsertOrDeleteCorrection
		, coral::LanguageVariant mainType
	);
	void insert_correction_solution(
        coral::phonetics::CandidateResult& solution,
        int index,
        vector<coral::phonetics::CandidateResult>& sents,
        coral::phonetics::ParseTextSet& outputSentences,
        bool needDeleteBadFirst
    );
    // 以下加载辅助函数均要求调用方持有 load_mutex_，加载任务不得回调该锁。
    bool load_locked(const coral::phonetics::LexiconLocations& path, bool synchronous, bool retry_until_success);
    bool finish_model_loading_locked();
    bool ensure_model_loaded_locked();
	ParseMode get_parse_mode(const string& pystring, const int bound) const;
	void export_graph_dot_file(const PinyinParseContext& context) const;
private:
	//提供一个string_view用于访问
	syllable_strview_map syllables_view_;
	std::unordered_set<std::string_view> fuzzy_only_syllables_;
    std::unique_ptr<coral::assist::ShapeHandler> u_mode_;
    std::unique_ptr<coral::assist::SymbolHandler> uu_mode_;
    bool uu_available_ = false;
    bool special_mode_unavailable_ = false;
    int cached_special_mode_ = -1;
	coral::phonetics::ContextScorer language_model_;
	std::string cached_condition_;
    bool smart_candidates_ = true;
	vector<coral::phonetics::CandidateResult> sents_cache_;
	// Solution 里 sentence/pinyin/pinyinview 三个容器的堆缓冲，每轮解析都重新分配、
	// 又在下一轮清缓存时全部释放。池子挂在单例上（PinyinParseContext 连同它内部的
	// CLanguageDecoder 都是逐次解析新建的，池子放解码器里活不过一次解析），
	// 丢缓存时把 Solution 连缓冲一起收进来，下一轮解析直接取用。
	vector<coral::phonetics::CandidateResult> solution_pool_;
	string pre_input_string_;
	vector<shared_ptr<coral::phonetics::CandidateTransform>> middlewares_;
	bool has_loaded_ = false;
    bool loading_pending_ = false;
	// 词库装载失败后是否在解析入口重试（由 load 的 retryUntilSuccess 注入）
	bool retry_load_until_success_ = false;
	bool is_exist_next_page_ = false;
	bool is_enable_fuzzy_ = false;
	// 放开上限的重解析也没能给缓存追加新候选，说明当前输入的长尾已经取完
	bool tail_exhausted_ = false;
	bool is_show_infos_ = false;
	coral::phonetics::SpellingRepair pinyin_corrector_;
	vector<int> last_fuzzy_vector_;
	unordered_map<string_view, set<string_view>> fuzzy_initials_;
	unordered_map<string_view, set<string_view>> fuzzy_finals_;
	unordered_map<string_view, string_view> fuzzy_alls_;
	vector<string> last_candidates_;
	vector<string> last_candidate_pinyins_;
	vector<wstring> correction_res_;
	coral::phonetics::LexiconCatalog lexicon_manager_;
	coral::phonetics::LexiconLocations all_lexicon_path_;
	std::mutex load_mutex_;
	coral::foundation::BackgroundTasks loading_tasks_;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // PINYINMODEL_H

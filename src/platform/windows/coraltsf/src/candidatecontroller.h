#pragma once
#include "coral/vocabulary/lexicon_types.h"

#include "candidateactionsink.h"
#include "candidatestatemodel.h"
#include "configauthority.h"
#include "session_bridge.h"
#include "doublepinyinconverter.h"
#include "coral/composition/assist_hints.h"
#include "coral/composition/prediction_context.h"
#include "coral/translation/translation_client.h"

#include <memory>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

enum class STATUS
{
	NONE_STATUS = -1,
	INPUT_STATUS = 0,
	SELECT_STATUS = 1,
};

enum class InputMode
{
	IM_Normal = 0,
	IM_UMode = 1,
	IM_VMode = 2,
	IM_UUMode = 3,
};

// 所有者线程输入状态控制器。它刻意不含 HWND、绘制、
// 字体、主题、命中测试或 GDI 表面；这些关注点属于 renderer.exe。
class CandidateController final
{
public:
	CandidateController();
	~CandidateController();

	void setTextService(CandidateActionSink* sink) { m_sink = sink; }
	void set_candidate_provider(std::shared_ptr<coral::text_service::CandidateProvider> source,
		coral::composition::SessionToken session_id, bool traditional);
	void setConfigSnapshot(std::shared_ptr<const coral::tsf::ImeConfigSnapshot> config);
	void applyPinyinSettings();

	void addLetter(WCHAR letter);
	bool back();
	bool removeNextLetter();
	void moveCursorHome();
	void moveCursorEnd();
	void nextCol();
	void prevCol();
	bool nextRow();
	bool prevRow(const bool& toFirst = false);
	void enterCandidateExpand();
	void collapseCandidateExpand();
	bool isCandidateExpanded() const { return m_candidates.expanded(); }
	void setVisualLayout(std::vector<std::uint32_t> rowStarts,
		std::vector<std::uint32_t> itemCenters);
	void prefetchCandidates(bool force = false);

	int selectCol(CString& text, int column = -1);
	int selectCurrentCandidate(CString& text);
	int selectCandidateByIndex(CString& text, int index);

	CString getPinyinHolder() const;
	CString getPinyin() const { return m_rawInput; }
	int get_pinyin_length() const;
	int getSelCol() const;
	int getSelectedCandidateIndex() const { return static_cast<int>(m_candidates.selectedIndex()); }
	CString getCandidateText(int index) const;
	bool canDisableCandidate(int index) const;
	int getCurrColMaxSize() const;
	STATUS getInputStatus() const { return m_status; }
	InputMode getInputMode() const { return m_inputMode; }
    std::wstring_view modeHint() const;
	bool isNoMoreData() const { return !m_candidates.hasMore(); }

    void set_translation_backend(coral::translation::Backend backend) { translation_backend_ = std::move(backend); }
    bool begin_translation();
    void exit_translation();
    void move_translation_selection(int delta);
    bool is_translation_mode() const { return translation_.active(); }
    const coral::translation::Request& translation_request() const { return translation_.request(); }
    void apply_translation_result(coral::translation::Result result);

	void Reset();
	void resetConditionWord();
	void refreshCandidateContent();
	void adjustTodayWord(int offset);
	bool applyCandidateActionByIndex(int index, coral::CandidateAction action);
	void showAddPhraseByIndex(int index);

	// 剪贴板：V 模式只输入了 v 时，候选第二项（B）为入口；选中后候选窗改为剪贴板历史列表。
	int clipboardEntryIndex() const { return m_clipboardEntryIndex; }
	bool isClipboardMode() const { return m_clipboardMode; }
	void exitClipboardMode();
	void clipboardMove(int delta);
	void clipboardMoveTo(bool last);
	static int clipboardScreenRows();
	// 剪贴板内容按原样上屏，不做全角转换；读取后复位。
	bool takeVerbatimCommit() { const bool verbatim = m_verbatimCommit; m_verbatimCommit = false; return verbatim; }

	void setUiLessMode(bool value) { m_uiLessMode = value; if (value) m_visible = false; }
	bool getUiLessMode() const { return m_uiLessMode; }
	void showCandidateDlg(bool value) { m_visible = value && !m_uiLessMode; }
	bool isVisible() const { return m_visible; }
	void setInputArea(const CRect&, bool show) { if (show) showCandidateDlg(true); }
	void setParentWnd(HWND) {}
	bool isMouseSelected() const { return m_mouseSelected; }
	void setMouseSelectedState(bool value) { m_mouseSelected = value; }

private:
	struct SelectedSegment
	{
		CString word;
		CString pinyin;
		int rawLength = 0;
	};

	void updateInputMode();
	void rebuildDerivedState();
	void queryCandidates();
	void publishCandidates();
	void notifyPresentation();
	std::size_t visualRowFor(std::size_t index) const;
	std::size_t visualRowEnd(std::size_t row) const;
	bool selectVisualNeighbor(int rowDelta);
	void loaderMain();
	void applyLoadedCandidates();
	static LRESULT CALLBACK LoaderWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
	void notifyComposition();
	void refreshSegmentation(const CString& before);
	void moveNextCol();
	void movePrevCol();
	bool moveNextRow();
	bool movePrevRow(bool toFirst);
	coral::tsf::SegmentedComposition segmentedUnfinished() const;
	int compositionCursor() const;
	int consumedRawLength() const;
	static int comparePinyinSeparator(const CString& subPinyin, const CString& basePinyin);
	static CString withoutSeparators(CString value);
	CString convertAllDoublepinyin(const CString& input);
	int consumedDoublepinyinLength(const CString& candidatePinyin);
	bool useDoublepinyinParsing() const;
	bool clipboardEntryAvailable() const;
	void enterClipboardMode();
	bool appendClipboardPage();
	void publishClipboardSelection();

    void start_translation_request();
    void cancel_translation_request();
    coral::translation::TranslationSession translation_;
    coral::translation::Backend translation_backend_;
    coral::translation::Cancellation translation_cancel_;
    struct TranslationJob {
        coral::translation::Request request;
        coral::translation::Cancellation cancelled;
        coral::translation::Backend backend;
    };
    std::optional<TranslationJob> pending_translation_;
    std::vector<coral::translation::Result> translation_results_;
    std::optional<coral::tsf::CandidateStateModel> original_candidates_;

	CandidateActionSink* m_sink = nullptr;
	std::shared_ptr<coral::text_service::CandidateProvider> m_source;
	coral::composition::SessionToken m_sessionId = 0;
	std::shared_ptr<const coral::tsf::ImeConfigSnapshot> m_config;
	coral::tsf::CandidateStateModel m_candidates;
	std::vector<SelectedSegment> m_selected;
	std::unique_ptr<IDoublepinyinScheme> m_doublepinyinScheme;

	CString m_rawInput;
	CString m_unfinished;
	CString m_selectedWord;
	CString m_selectedPinyin;
	CString m_condition;
    HWND prediction_window_ = nullptr;
	std::string prediction_condition() const;
	int m_cursor = 0;
	int m_selectedWordCount = 0;
	std::uint64_t m_version = 0;
	STATUS m_status = STATUS::NONE_STATUS;
	InputMode m_inputMode = InputMode::IM_Normal;
	bool m_doublepinyin = false;
	bool m_traditional = false;
	bool m_uiLessMode = false;
	bool m_visible = false;
	bool m_mouseSelected = false;
	bool m_compositionEditing = false;
	bool m_isLoading = false;
    bool m_specialMatch = false, m_queryUnavailable = false;
	int m_clipboardEntryIndex = -1;
	bool m_clipboardMode = false;
	bool m_verbatimCommit = false;
	std::vector<std::wstring> m_clipboardTexts; // 完整文本；候选项只显示单行摘要
	std::vector<std::uint32_t> m_visualRowStarts;
	std::vector<std::uint32_t> m_itemCenters;
	struct LoadRequest
	{
		std::uint64_t version = 0;
		coral::text_service::CandidateLookup query;
		std::shared_ptr<coral::text_service::CandidateProvider> source;
	};
	struct LoadResult
	{
		std::uint64_t version = 0;
		HRESULT status = E_FAIL;
		coral::text_service::CandidateSnapshot snapshot;
	};
	HWND m_loaderWindow = nullptr;
	std::thread m_loaderThread;
	std::mutex m_loaderMutex;
	std::condition_variable m_loaderWake;
	std::optional<LoadRequest> m_pendingLoad;
	std::vector<LoadResult> m_loadedResults;
	bool m_stopLoader = false;
};

#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TEXTSERVICE_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TEXTSERVICE_H_INCLUDED

#include <ctffunc.h>
#include "compartment.h"
#include "keyeventsink.h"
#include "languageprofile.h"
#include "texteditsink.h"
#include "textlayoutsink.h"
#include "threadfocussink.h"
#include "threadmgreventsink.h"
#include "candidatelistuielement.h"
#include "editsession.h"
#include "candidateactionsink.h"
#include "displayattribute.h"
#include "tsfthreadcontext.h"
#include "configauthority.h"
#include "session_bridge.h"
#include "renderercoordinator.h"
#include "keydecision.h"
#include "csettingmenu.h"

using KeyHandleType = coral::tsf::KeyHandleType;

enum ToolbarButtonId
{
	Ascii = 2,
	Punc = 3,
	FullHalf = 4,
	Charset = 5,
};

// 翻页按键设置
struct PageNavigationSettings
{
	bool commaOrPeriodEnabled = true; // 逗号/句号
	bool page_up_down_enabled = true; // PageUp/PageDown（翻页键）
	bool leftOrRightBracketEnabled = true; // 左/右方括号
};

extern HINSTANCE g_hInst;
class CandidateController;

class ATL_NO_VTABLE CTextService
	: public CComObjectRootEx<CComSingleThreadModel>
	, public CComCoClass<CTextService, &CLSID_TextService>
	, public ITfTextInputProcessorEx
	, public ITfThreadMgrEventSink
	, public ITfThreadFocusSink
	, public ITfTextEditSink
	, public ITfTextLayoutSink
	, public ITfKeyEventSink
	, public ITfCompositionSink
	, public ITfDisplayAttributeProvider
	, public ITfActiveLanguageProfileNotifySink
	, public ITfCompartmentEventSink
	, public ITfFnConfigure
	, public CandidateActionSink
{
	friend class CThreadMgrEventSink;
	friend class CThreadFocussink;
	friend class CTextLayoutSink;
	friend class CTextEditSink;
	friend class CLanguageProfile;
	friend class CKeyEventSink;
	friend class CCompositionSink;
	friend class CCompartmentEventSink;
	friend class CCandidateListUIElement;
	friend class coral::tsf::ContextStore;

public:
	CTextService()
		: m_tsfThreadContext(this)
		, session_bridge_(coral::text_service::shared_session_bridge())
		, m_settingMenu(this)
	{
	}

	DECLARE_REGISTRY_RESOURCEID(IDR_TEXTSERVICE)
	BEGIN_COM_MAP(CTextService)
		COM_INTERFACE_ENTRY(ITfTextInputProcessor)
		COM_INTERFACE_ENTRY(ITfTextInputProcessorEx)
		COM_INTERFACE_ENTRY(ITfThreadMgrEventSink)
		COM_INTERFACE_ENTRY(ITfThreadFocusSink)
		COM_INTERFACE_ENTRY(ITfTextEditSink)
		COM_INTERFACE_ENTRY(ITfTextLayoutSink)
		COM_INTERFACE_ENTRY(ITfKeyEventSink)
		COM_INTERFACE_ENTRY(ITfCompositionSink)
		COM_INTERFACE_ENTRY(ITfDisplayAttributeProvider)
		COM_INTERFACE_ENTRY(ITfActiveLanguageProfileNotifySink)
		COM_INTERFACE_ENTRY(ITfCompartmentEventSink)
		COM_INTERFACE_ENTRY(ITfFnConfigure)
	END_COM_MAP()

	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct();
	void FinalRelease();

public:
	// ITfFnConfigure
	STDMETHODIMP Show(HWND hwndParent, LANGID langid, REFGUID rguidProfile);
	STDMETHODIMP GetDisplayName(BSTR* pbstrName);

	// ITfDisplayAttributeProvider
	STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum);
	STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo);

	// ITfTextInputProcessor
	STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid);
	STDMETHODIMP Deactivate(void);
	// ITfTextInputProcessorEx
	STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags);

	// ITfThreadMgrEventSink
	STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim);
	STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim);
	STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus);
	STDMETHODIMP OnPushContext(ITfContext* pic);
	STDMETHODIMP OnPopContext(ITfContext* pic);

	// ITfThreadFocusSink
	STDMETHODIMP OnSetThreadFocus(void);
	STDMETHODIMP OnKillThreadFocus(void);

	// ITfTextEditSink
	STDMETHODIMP OnEndEdit(ITfContext* pContext, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord);

	// ITfTextLayoutSink
	STDMETHODIMP OnLayoutChange(ITfContext* pic, TfLayoutCode lcode, ITfContextView* pView);

	// ITfKeyEventSink
	STDMETHODIMP OnSetFocus(BOOL fForeground);
	STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
	STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
	STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
	STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
	STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten);

	// ITfCompositionSink,
	STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition);

	// ITfActiveLanguageProfileNotifySink
	STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated);

	// ITfCompartmentEventSink
	STDMETHODIMP OnChange(REFGUID guid);

public:
	ITfThreadMgr* getThreadMgr() const;
	TfClientId getClientId() const;
	HRESULT ensureEngineSessionForKey(ITfContext* context);
	const coral::tsf::ImeConfigSnapshot& configSnapshot() const { return *m_configSnapshot; }

	coral::tsf::KeyHandlingDecision previewKeyDecision(
		ITfContext* context, WPARAM wParam, bool keyDown, bool shiftDown, bool ctrlDown,
		bool altDown, bool winDown, bool capsLockOn);
	void executeKeyDecision(const coral::tsf::KeyHandlingDecision& decision);
	bool isCandidateVisibleForContext(ITfContext* context) const;
	bool isMouseSelectedForContext(ITfContext* context) const;
	void updateLanguageMode(ITfContext* pic, bool updateMode);

	// handle
	HRESULT handleCharacterKey(WPARAM wParam);
	HRESULT handlePunctuation(WPARAM wParam);
	HRESULT handleChinesePunctuation(WPARAM wParam);
	HRESULT handleArrowKey(WPARAM wParam);
	HRESULT handleReturnKey();
	HRESULT handleShiftKey();
	HRESULT handleBackKey();
	HRESULT handleSpaceKey(ITfContext*);
	HRESULT handleSelectKey(WPARAM wParam);
	HRESULT handlePageKey(coral::tsf::CandidatePageKey direction);
	HRESULT handlePadChar(const WPARAM wParam);
	HRESULT handleExit();
	HRESULT handleClipboardKey(WPARAM wParam);
    HRESULT handle_translation_key(WPARAM key);
	HRESULT handleDel();
	HRESULT invokeKeyHandler(ITfContext*, WPARAM wParam, LPARAM lParam);
	HRESULT startComposition(ITfContext* pContext, WPARAM wParam, bool isPositionEnhanced);
	// 全角模式
	HRESULT invokeFullWidthCharactersKeyHandler(ITfContext*, WPARAM wParam, LPARAM lParam);
	const CString& getProcessName() const;
	void sendLeftKey(ITfContext* pContext) const;
	void showCandidateDlg();
	void adjustCandidateDlgPos(const CRect& rcInput);
	void hideCandidateDlg(BOOL bClearKeyFlags = FALSE);
	void onlyHideCandidateDlg();
	void onlyShowCandidateDlg();
	void resetCandidateDlg();
	bool isCandidateVisiable() const;
	void destroyCandidateDlg();
	bool isMouseSelected();
	void showLanguageBar();
	void resetQuoteState();
	// 无界面模式候选词列表接口
	void syncCandidateUI(const coral::tsf::CandidateStateModel& model) override;
	void setCandidatePresentation(bool expanded, bool hasMore) override;
	void beginCandidateUIElement();
	void updateCandidateUIElement();
	void endCandidateUIElement();
	// 无界面模式合成字符串更新
	void updateCompartmentString(const CString& str);

	void resetConditionWord();
	HRESULT commitCurrentCandidate();
	HRESULT commitRawPinyin();
	HRESULT commitCandidateByIndex(int candidateIndex);
	void pinCandidateByIndex(int candidateIndex);
	void deleteCandidateByIndex(int candidateIndex);
	void showAddPhraseByIndex(int candidateIndex);
	void applyCandidateLayout(const std::vector<std::uint32_t>& rowStarts,
		const std::vector<std::uint32_t>& itemCenters);
	void requestCandidatePrefetch();
	void toggleCandidateExpansion();
	void selectCandidateByIndex(int index);
	void selectCharacterFromWord(int characterIndex);

	void destroyFloatingToolbar();
	void showFloatingToolbar(BOOL show);
	bool isFloatingToolbarVisible() const;
	void handleFloatingToolbarClick(int buttonId);
	void initFloatingToolbarIcon();
	void toggleFullHalfWidthFromUi() { changeFullHalfWidth(); }
	void toggleCharacterSetFromUi() { changeCharacterSimOrTra(); }
	void openSettingsMenuFromUi(const RECT* anchorRect = nullptr);
	void refreshFloatingToolbarPreference();

	void changeCapsLock();
	void endEditSession();
	void updateEditSessionAccumulatedPinyin(const CString& pinyinHolder);
	int isNeedFilter(const CString& text) const;
	CString& getAllInput();
	std::vector<CString>& getCandInput();
	BOOL exsitComposition() const;
	ITfComposition* getComposition() const;
	HRESULT getTextBound(CRect& rc);
	HRESULT layoutChangeGetTextBound(CRect& rc);
	ITfContext* getContext() const;
	std::uint64_t getServiceInstanceId() const { return m_tsfThreadContext.contexts().serviceInstanceId(); }
	CEditMgr* getEditMgr() const;
	HRESULT setCursorPos(int pos);
	void setKeepPlaceholder(bool keepPlaceholder);
	void setIsEngPuncMode(bool isEngPuncMode);
	bool getIsEngPuncMode() const;
	// 符号自动补全相关
	bool needSendLeftKey() const;
	void setNeedSendLeftKey(bool need);
	CString autoPunctuationTrans(WPARAM wParam);
	// 测试接口
	bool setTestInputState(bool isChinese, bool isFullWidth, bool isChinesePunct);
	bool getTestInputState(bool* isChinese, bool* isFullWidth, bool* isChinesePunct);
private:
	CString chinesePunctuationTrans(const WPARAM& wParam, bool isNeedChangeQuote = true);
	// 检验是不是中文独有的符号
	bool isChinesePunctuation(const CString& text) const;
	// 获取配对符号的右半部分
	CString getAutoClosePairChar(const CString& leftChar) const;
	// 判断按键是否是配对符号的左半部分
	bool isAutoClosePairKey(WPARAM wParam) const;
	bool isAutoClosePairKey(WPARAM wParam, bool shiftDown) const;
	// 判断按键是否是可输出的字符
	bool isOutputChar(WPARAM wParam) const;
	// 判断是否应该处理数字间符号转换
	bool needs_symbol_conversion(WPARAM wParam) const;
	bool needs_symbol_conversion(
		WPARAM wParam, const coral::tsf::ContextState* state) const;
	// 检测并处理数字间符号转换
	bool handle_symbol_conversion(WCHAR digitChar);
	CString englishPunctuationTrans(const WPARAM wParam);
	void createCandidateDlg();
	void resetInput();
	void changeFloatingToolbarPuncIcon();
	void changeFloatingToolbarCharsetIcon();
	HWND getFocusWindow() const;
	BOOL isUserTypingAndIMEParsing() const;
	void changeFullHalfWidth();
	void changeCharacterSimOrTra();
	void changePunctuationLanguageMode();
	CString CandidateDlgRc2Str(CRect& rc) const;
	bool isFullWidth() const;
	bool isPunctuation(WPARAM wParam) const;
	bool isPunctuation(WPARAM wParam, bool shiftDown) const;
	bool isDefaultEnglishMode() const;
	bool isUsingEnglishPunctuation() const;
	bool isVMode() const;
	bool canShowCandidate() const;
	CString getContent(int col) const;
	HRESULT commitCandidateByVisualColumn(int visualColumn);
	HRESULT finishCandidateSelection(int selectionResult, const CString& text);
	void showCPinyinModelInitErrorDialog() const;

	std::pair<bool, CString> isSpecialKey(WPARAM wParam);
	HRESULT selectContextController(ITfContext* context);
	coral::tsf::ContextState* activeContextState();
	const coral::tsf::ContextState* activeContextState() const;
	coral::tsf::ContextInputState& contextInputState();
	const coral::tsf::ContextInputState& contextInputState() const;
	void clearActiveContext();
	void prepareContextClose(coral::tsf::ContextState& state);
	coral::composition::SessionToken activeEngineSession() const;
	void refreshConfigSnapshot(bool force = false);
	void syncEngineConfig();
	bool hasThreadFocus() const;
	void publishCandidateSnapshot();
	void republishFocusedCandidate();
	void onCandidateUIElementVisibilityChanged(CCandidateListUIElement* element, bool show);
	void publishToolbarSnapshot(bool visible);
private:
	coral::tsf::KeyHandlingDecision decideKeyHandling(
		const coral::tsf::KeyEventSnapshot& event,
		const coral::tsf::ContextState* state) const;
	void applyKeyHandlingEffects(const coral::tsf::KeyHandlingDecision& decision);
	void reconcileInputHistoryOnBackspace();

	CComPtr<ITfThreadMgr> m_pThreadMgr;
	CThreadMgrEventSink* m_pThreadMgrEventSink = nullptr;
	CThreadFocussink* m_pThreadFocusSink = nullptr;
	CTextLayoutSink* m_pTextLayoutSink = nullptr;
	CTextEditSink* m_pTextEditSink = nullptr;
	CLanguageProfile* m_pLanguageProfile = nullptr;
	CKeyEventSink* m_pKeyEventSink = nullptr;
	CCompartmentEventSink* m_pCompartmentEventSink = nullptr;
	CCandidateListUIElement* m_pCandidateListUIElement = nullptr;
	// 非持有的路由缓存。ContextStore 会在销毁匹配的 ContextState
	// 前使其失效；ContextState 仍是唯一的所有者/状态来源。
	CEditMgr* m_pEditMgr = nullptr;
	CComPtr<ITfUIElementMgr> m_pUIElementMgr;
	CComPtr<ITfDisplayAttributeInfo> m_pDisplayAttrInfo;
	CandidateController* m_pCandidateDlg = nullptr;
	// 本次组词期间是否用过方向键导航（用于回车行为：未导航=上屏原始拼音，导航过=上屏选中候选）
	bool m_arrowMovedInComposition = false;
	coral::tsf::ContextToken m_activeContextToken{};
	coral::tsf::ContextInputState m_detachedInputState{};
	CString m_processName;
	CString m_version;

	DWORD m_activateFlags = 0;
	TfClientId m_clientId = 0;
	int m_preAscIIMode = -1;
	PageNavigationSettings m_pageSettings{ true, true, true }; // 翻页设置
	std::mutex m_mutex;
	coral::tsf::TsfThreadContext m_tsfThreadContext;
	std::shared_ptr<coral::text_service::SessionBridge> session_bridge_;
	coral::tsf::ConfigAuthority m_configAuthority;
	coral::tsf::RendererCoordinator m_rendererCoordinator;
	std::shared_ptr<const coral::tsf::ImeConfigSnapshot> m_configSnapshot;
	coral::tsf::RuntimeInputMode m_runtimeMode;
	bool m_remoteToolbarVisible = false;
	CSettingMenu m_settingMenu;
};

OBJECT_ENTRY_AUTO(__uuidof(TextService), CTextService)

#endif // TEXTSERVICE_H

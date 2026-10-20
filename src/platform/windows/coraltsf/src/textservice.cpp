#include "pch.h"
#include "textservice.h"
#include "tsfapi.h"
#include "editsession.h"
#include "coreapi.h"
#include "preference_registry.h"
#include "candidatecontroller.h"
#include "text_file_helpers.hpp"
#include "input_timing.h"
#include "digitpunctuation.h"
#include <atomic>

static std::once_flag gs_logInitOnce;
GET_DPI_FOR_WINDOW g_fGetDpiForWindow = nullptr;

namespace
{
bool TryGetCandidateFallbackAnchor(ITfContext* context, CRect& anchor)
{
	HWND viewWindow = nullptr;
	CComPtr<ITfContextView> view;
	if (context && SUCCEEDED(context->GetActiveView(&view)) && view)
		view->GetWnd(&viewWindow);
	HWND target = ::GetFocus();
	if (!target) target = viewWindow;
	if (!target) return false;

	GUITHREADINFO gui{ sizeof(gui) };
	if (::GetGUIThreadInfo(::GetWindowThreadProcessId(target, nullptr), &gui))
	{
		if (gui.hwndCaret && ::IsWindow(gui.hwndCaret))
		{
			RECT caret = gui.rcCaret;
			::MapWindowPoints(gui.hwndCaret, nullptr,
				reinterpret_cast<POINT*>(&caret), 2);
			anchor = caret;
			return true;
		}
		if (gui.hwndFocus && ::IsWindow(gui.hwndFocus))
			target = gui.hwndFocus;
	}

	POINT caret{};
	if (target == ::GetFocus() && ::GetCaretPos(&caret) &&
		::ClientToScreen(target, &caret))
	{
		anchor.SetRect(caret.x, caret.y, caret.x + 1,
			caret.y + ::GetSystemMetrics(SM_CYCURSOR));
		return true;
	}
	RECT bounds{};
	if (!::GetWindowRect(target, &bounds)) return false;
	anchor.SetRect(bounds.left + 8, bounds.top + 8,
		bounds.left + 9, bounds.top + 32);
	return true;
}
}

HRESULT CTextService::FinalConstruct()
{
	m_clientId = TF_CLIENTID_NULL;
	m_pThreadMgrEventSink = new CThreadMgrEventSink(this);
	m_pThreadFocusSink = new CThreadFocussink(this);
	m_pTextLayoutSink = new CTextLayoutSink(this);
	m_pTextEditSink = new CTextEditSink(this);
	m_pLanguageProfile = new CLanguageProfile(this);
	m_pKeyEventSink = new CKeyEventSink(this);
	m_pCompartmentEventSink = new CCompartmentEventSink(this);
	m_preAscIIMode = -1;

	HMODULE user32 = ::GetModuleHandleW(L"User32.dll");
	if (user32)
		g_fGetDpiForWindow = reinterpret_cast<GET_DPI_FOR_WINDOW>(
			::GetProcAddress(user32, "GetDpiForWindow"));

	return S_OK;
}

void CTextService::FinalRelease()
{
	m_settingMenu.hide();
	DELETE_P(m_pThreadMgrEventSink);
	DELETE_P(m_pThreadFocusSink);
	DELETE_P(m_pTextLayoutSink);
	DELETE_P(m_pTextEditSink);
	DELETE_P(m_pLanguageProfile);
	DELETE_P(m_pKeyEventSink);
	DELETE_P(m_pCompartmentEventSink);
	m_pCandidateListUIElement = nullptr;
	m_rendererCoordinator.stop();
	destroyCandidateDlg();
	m_pCandidateDlg = nullptr;
	g_fGetDpiForWindow = nullptr;
}

STDAPI CTextService::Activate(ITfThreadMgr* ptim, TfClientId tid)
{
	return ActivateEx(ptim, tid, 0U);
}

STDAPI CTextService::Deactivate(void)
{
	LOG_INFO_W(L"[CTextService::Deactivate] start.");
	m_settingMenu.hide();
	if (m_clientId == TF_CLIENTID_NULL && !m_pThreadMgr)
		return S_OK;
	// 清除测试接口的全局指针
	clearTestTextService();
	if (m_clientId != TF_CLIENTID_NULL)
	{
		if (m_pEditMgr)
		{
			m_pEditMgr->cancel(m_clientId);
		}
	}
	if (m_preAscIIMode != int(m_pLanguageProfile->getAsciiMode()))
		m_preAscIIMode = int(m_pLanguageProfile->getAsciiMode());
	m_pTextEditSink->unInit();
	m_pThreadFocusSink->unInit();
	m_pThreadMgrEventSink->unInit();
	m_pLanguageProfile->unInit();
	m_pKeyEventSink->unInit();
	m_pKeyEventSink->clear();
	m_pCompartmentEventSink->unInit();
	m_rendererCoordinator.stop();
	for (const auto handle : m_tsfThreadContext.contexts().engineSessions())
		session_bridge_->destroy_session(handle.value);
	clearActiveContext();
	m_tsfThreadContext.deactivate();
	m_pThreadMgr = NULL;
	m_clientId = TF_CLIENTID_NULL;
	destroyCandidateDlg();
	showFloatingToolbar(FALSE);
	resetInput();
	return S_OK;
}

STDAPI CTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_processName = hlp::getFileName(hlp::getModulePath(NULL)).MakeLower();
	std::call_once(gs_logInitOnce, [this]() {
		hlp::initCoralPinyinLog(hlp::LogPathType::TsfType);
#ifdef _WIN64
		LOG_INFO_W(L"[CTextService::ActivateEx]" + std::wstring(m_processName.GetString()) + L" coraltsf64.dll is activated.");
#else
		LOG_INFO_W(L"[CTextService::ActivateEx]" + std::wstring(m_processName.GetString()) + L" coraltsf32.dll is activated.");
#endif
	});
	// 本进程内嵌的 SettingsConfig 副本此前从未以用户配置路径 init（既有缺陷）：
	// TSF 进程内（如悬浮工具栏开关）的写入只进共享内存、不落 INI，重启后回退。
	// 这里与主进程 main.cpp 保持一致，用 LocalLow 配置路径初始化，保证 INI 与共享内存双通道一致。
	try
	{
		sConfig->init((hlp::getLocalLowCoralPinyinPath() / "config" / "coralpinyinConfig.ini").string());
		sConfig->pollForUpdates();
	}
	catch (const std::exception& error)
	{
		LOG_ERROR_W(L"[CTextService::ActivateEx] config unavailable: " +
			std::wstring(CA2W(error.what(), CP_UTF8)));
	}
	m_activateFlags = dwFlags;
	m_clientId = tid;
	m_pThreadMgr = ptim;
	HRESULT threadContextHr = m_tsfThreadContext.activate(g_hInst);
	if (FAILED(threadContextHr) && threadContextHr != S_FALSE)
		return threadContextHr;
	const HRESULT rendererHr = m_rendererCoordinator.start(this, &m_tsfThreadContext);
	if (FAILED(rendererHr))
		LOG_INFO_W(L"[CTextService::ActivateEx] renderer unavailable; text input remains active.");
	LOG_INFO_W(L"[tsf_service] event=activate service_id=" +
		std::to_wstring(m_tsfThreadContext.contexts().serviceInstanceId()) + L" client_id=" +
		std::to_wstring(static_cast<unsigned long>(tid)));
	HRESULT hr = E_FAIL;
	CString logStr;
	LOG_ERROR_MSG_W(m_pThreadMgrEventSink->init(), m_processName, L"m_pThreadMgrEventSink init failed!");
	// 允许初始化失败
	if (!m_pTextEditSink->init())
	{
		LOG_INFO_W(L"[CTextService::ActivateEx] m_pTextEditSink init failed.");
	}
	LOG_ERROR_MSG_W(m_pKeyEventSink->init(), m_processName, L"m_pKeyEventSink init failed!");
	LOG_ERROR_MSG_W(m_pLanguageProfile->init(), m_processName, L"m_pLanguageProfile init failed!");
	LOG_ERROR_MSG_W(m_pCompartmentEventSink->init(), m_processName, L"m_pCompartmentEventSink init failed!");
	LOG_ERROR_MSG_W(m_pThreadFocusSink->init(), m_processName, L"m_pThreadFocusSink init failed!");

	// 为组串文本初始化点状下划线显示属性。
	if (!m_pDisplayAttrInfo) {
		CComObject<CDisplayAttributeInfo>* pObj = nullptr;
		HRESULT displayAttrHr = CComObject<CDisplayAttributeInfo>::CreateInstance(&pObj);
		if (SUCCEEDED(displayAttrHr) && pObj) {
			// CreateInstance 返回的是零引用对象。QueryInterface 提供的是由
			// m_pDisplayAttrInfo 持有的引用；若在此释放 pObj，提供方在 TSF 查询之前
			// 将持有一个悬空指针。
			displayAttrHr = pObj->QueryInterface(IID_PPV_ARGS(&m_pDisplayAttrInfo));
			LOG_ERROR_MSG_W(SUCCEEDED(displayAttrHr), m_processName, L"Display attribute initialization failed");
		}
	}

	// 引擎连接与会话创建发生在按键处理时。
	refreshConfigSnapshot(true);
	m_runtimeMode.reset(*m_configSnapshot, m_configSnapshot->defaultEnglish);
	m_tsfThreadContext.contexts().clear();
	{
		if (m_pLanguageProfile->getAsciiMode() != m_configSnapshot->defaultEnglish)
			m_pLanguageProfile->updateAsciiMode();
		m_preAscIIMode = static_cast<int>(m_pLanguageProfile->getAsciiMode());
		if (!m_pCompartmentEventSink->isKeyboardOpen())
			m_pCompartmentEventSink->setKeyboardOpen(TRUE);
		m_detachedInputState.punctuationLanguageMode = 0;

		// 初始化键盘输入模式转换状态
		m_pCompartmentEventSink->updateChineseMode(!m_preAscIIMode);
		m_pCompartmentEventSink->updateChinesePunctuationMode(!m_preAscIIMode);
		m_pCompartmentEventSink->updateFullShapeMode(m_configSnapshot->defaultFullWidth);
		showFloatingToolbar(TRUE);
		if (m_pEditMgr)
		{
			m_pEditMgr->setProcessName(m_processName);
		}
		hr = S_OK;
		LOG_INFO_W(L"[CTextService::ActivateEx] success.");
		// 设置测试接口的全局指针
		setTestTextService(this);
	}
Exit0:
	if (hr != S_OK)
	{
		Deactivate();
	}
	return hr;
}

STDAPI CTextService::OnInitDocumentMgr(ITfDocumentMgr* pdim)
{
	return m_pThreadMgrEventSink->OnInitDocumentMgr(pdim);
}

STDAPI CTextService::OnUninitDocumentMgr(ITfDocumentMgr* pdim)
{
	return m_pThreadMgrEventSink->OnUninitDocumentMgr(pdim);
}

STDAPI CTextService::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus)
{
	// 编辑提交期间，文档管理器可能短暂变为空。
	// 工具栏可见性跟随键盘/线程焦点，与文档变化无关。
	return m_pThreadMgrEventSink->OnSetFocus(pdimFocus, pdimPrevFocus);
}

STDAPI CTextService::OnPushContext(ITfContext* pic)
{
	return m_pThreadMgrEventSink->OnPushContext(pic);
}

STDAPI CTextService::OnPopContext(ITfContext* pic)
{
	return m_pThreadMgrEventSink->OnPopContext(pic);
}

STDAPI CTextService::OnSetThreadFocus(void)
{
	if (const auto session = activeEngineSession())
		session_bridge_->refresh_lexicon(session);
	return m_pThreadFocusSink->OnSetThreadFocus();
}

STDAPI CTextService::OnKillThreadFocus(void)
{
	endEditSession();
	resetInput();
	return m_pThreadFocusSink->OnKillThreadFocus();
}

STDAPI CTextService::OnEndEdit(ITfContext* pContext, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord)
{
	return m_pTextEditSink->OnEndEdit(pContext, ecReadOnly, pEditRecord);
}

STDAPI CTextService::OnLayoutChange(ITfContext* pic, TfLayoutCode lcode, ITfContextView* pView)
{
	return m_pTextLayoutSink->OnLayoutChange(pic, lcode, pView);
}

STDAPI CTextService::OnSetFocus(BOOL fForeground)
{
	return m_pKeyEventSink->OnSetFocus(fForeground);
}

STDAPI CTextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
	coral::input_timing::Scope timing(L"tsf.test_key_down", wParam);
	const auto configStart = coral::input_timing::nowUs();
	refreshConfigSnapshot();
	coral::input_timing::mark(L"tsf.test_config", configStart, wParam);
	const HRESULT hr = m_pKeyEventSink->OnTestKeyDown(pic, wParam, lParam, pfEaten);
	return hr;
}

STDAPI CTextService::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
	refreshConfigSnapshot();
	return m_pKeyEventSink->OnTestKeyUp(pic, wParam, lParam, pfEaten);
}

STDAPI CTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
	coral::input_timing::Scope timing(L"tsf.key_down", wParam);
	const auto configStart = coral::input_timing::nowUs();
	refreshConfigSnapshot();
	coral::input_timing::mark(L"tsf.key_config", configStart, wParam);
	const auto contextStart = coral::input_timing::nowUs();
	const HRESULT focusHr = m_tsfThreadContext.contexts().setFocused(pic);
	if (FAILED(focusHr))
		return focusHr;
	const HRESULT hr = selectContextController(pic);
	coral::input_timing::mark(L"tsf.key_context", contextStart, wParam, static_cast<unsigned long>(hr));
	if (FAILED(hr))
		return hr;
	return m_pKeyEventSink->OnKeyDown(pic, wParam, lParam, pfEaten);
}
STDAPI CTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
	refreshConfigSnapshot();
	const HRESULT focusHr = m_tsfThreadContext.contexts().setFocused(pic);
	if (FAILED(focusHr))
		return focusHr;
	const HRESULT hr = selectContextController(pic);
	if (FAILED(hr))
		return hr;
	return m_pKeyEventSink->OnKeyUp(pic, wParam, lParam, pfEaten);
}
STDAPI CTextService::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten)
{
	if (!pfEaten) return E_INVALIDARG;
	*pfEaten = FALSE;
	refreshConfigSnapshot();
	const HRESULT focusHr = m_tsfThreadContext.contexts().setFocused(pic);
	if (FAILED(focusHr))
		return focusHr;
	const HRESULT hr = selectContextController(pic);
	if (FAILED(hr))
		return hr;
	return m_pKeyEventSink->OnPreservedKey(pic, rguid, pfEaten);
}

STDAPI CTextService::OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition)
{
	// 宿主可能随时终止组串。我们自己的 Commit/Cancel 会话也可能
	// 触发此回调，因此绝不能在这里二次结束组串。
	coral::tsf::ContextState* state =
		m_tsfThreadContext.contexts().findByComposition(pComposition);
	if (state && state->compositionController().clearTerminatedComposition(pComposition))
	{
		state->inputState().compositionEnded = true;
		state->inputState().candidatePresentationRequested = false;
		state->candidateController().Reset();
		state->compositionController().getCandInput().clear();
		state->compositionController().getAllInput().Empty();
		auto snapshot = state->candidates();
		++snapshot.version;
		snapshot.candidates.clear();
		snapshot.candidateIds.clear();
		snapshot.selection = 0;
		snapshot.visible = false;
		state->setCandidates(std::move(snapshot));
		if (m_configSnapshot)
			m_rendererCoordinator.publishCandidate(
				*state, state->candidates(), state->candidates().anchor, false,
				*m_configSnapshot);
		state->candidateUIElement().endUI();
		if (activeContextState() == state)
		{
			state->candidateUIElement().syncFrom(state->candidates());
			m_pCandidateDlg->showCandidateDlg(false);
		}
	}
	return S_OK;
}

STDAPI CTextService::OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated)
{
	return m_pLanguageProfile->OnActivated(clsid, guidProfile, fActivated);
}

STDAPI CTextService::OnChange(REFGUID guidCompartment)
{
	// 开/关通知是系统状态变化，不是模式切换键。
	return m_pCompartmentEventSink->OnChange(guidCompartment);
}

ITfThreadMgr* CTextService::getThreadMgr() const
{
	return m_pThreadMgr;
}

TfClientId CTextService::getClientId() const
{
	return m_clientId;
}

HRESULT CTextService::handleCharacterKey(WPARAM wParam)
{
	CString letter;
	if (wParam != VK_OEM_7)
	{
		letter.Format(L"%c", (WCHAR)wParam);
		if (m_pKeyEventSink->isShiftDown() || m_pKeyEventSink->isCapsLockOn())
		{
			letter.MakeUpper();
		}
		else
		{
			letter.MakeLower();
		}
	}
	else
	{
		letter = "'";
	}
	createCandidateDlg();
	m_pCandidateDlg->addLetter(letter[0]);
	if (m_pEditMgr)
		m_pEditMgr->updateAccumulatedPinyin(m_pCandidateDlg->getPinyinHolder(), m_clientId);
	const auto* state = activeContextState();
	if (!state || !state->candidates().visible) {
		showCandidateDlg();
	}
	return S_OK;
}

HRESULT CTextService::handlePunctuation(WPARAM wParam)
{
	CString letter = autoPunctuationTrans(wParam);
	if (isChinesePunctuation(letter) || !isCandidateVisiable())
	{
		handleChinesePunctuation(wParam);
	}
	else
	{
		handleCharacterKey(letter.GetAt(0));
	}
	return S_OK;
}

HRESULT CTextService::handleChinesePunctuation(WPARAM wParam)
{
	CString text;
	text = chinesePunctuationTrans(wParam);
	if (m_pEditMgr && text.GetLength() == 1) {
		const CString& history = m_pEditMgr->getLastTwoChars();
		const wchar_t previous = history.IsEmpty() ? L'\0' : history.GetAt(history.GetLength() - 1);
		text.SetAt(0, coral::tsf::following_period(
			m_configSnapshot->autoConvertDigitSymbol, m_configSnapshot->auto_convert_letter_symbol,
			m_pCandidateDlg && !m_pCandidateDlg->getPinyin().IsEmpty(), previous, text.GetAt(0)));
	}
	CString insertText;
	if (isCandidateVisiable())
	{
		insertText = getContent(m_pCandidateDlg->getSelCol());
		insertText += text;
	}
	else
	{
		insertText = text;
	}

	setKeepPlaceholder(true);
	if (m_pEditMgr)
	{
		m_pEditMgr->insertText(m_clientId, isFullWidth() ? hlp::HalfToFull(insertText) : insertText);
	}
	hideCandidateDlg(TRUE);
	return S_OK;
}

HRESULT CTextService::handleArrowKey(WPARAM wParam)
{
	if (isCandidateVisiable())
	{
		if (wParam == VK_DOWN)
		{
			// 单行模式按 ↓ 展开；展开内下移/滚动
			m_arrowMovedInComposition = true;
			if (m_pCandidateDlg->isCandidateExpanded())
				m_pCandidateDlg->nextRow();
			else
				m_pCandidateDlg->enterCandidateExpand();
			return S_OK;
		}
		else if (wParam == VK_UP)
		{
			if (m_pCandidateDlg->isCandidateExpanded())
			{
				// 展开内上移；到最顶行时 prevRow 内部会收回展开窗口
				m_arrowMovedInComposition = true;
				m_pCandidateDlg->prevRow();
				return S_OK;
			}
			// 单行模式没有上一行；不能把 ↑ 当作 ←，否则会与候选导航语义冲突。
			return S_OK;
		}

		m_arrowMovedInComposition = true;
		if (wParam == VK_RIGHT || wParam == VK_LEFT)
			wParam == VK_RIGHT ? m_pCandidateDlg->nextCol() : m_pCandidateDlg->prevCol();
		else if (wParam == VK_HOME || wParam == VK_END)
			wParam == VK_HOME ? m_pCandidateDlg->moveCursorHome() : m_pCandidateDlg->moveCursorEnd();
	}
	return S_OK;
}
HRESULT CTextService::handleReturnKey()
{
	if (!isCandidateVisiable())
		return S_OK;
	// V 模式（数字/网址）保持原有行为：直接提交当前候选
	if (m_pCandidateDlg && m_pCandidateDlg->getInputMode() == InputMode::IM_VMode)
		return commitCurrentCandidate();
	// 没有移动方向键时，回车直接上屏原始拼音；移动过则提交选中的候选
	if (!m_arrowMovedInComposition)
		return commitRawPinyin();
	return commitCurrentCandidate();
}

HRESULT CTextService::commitRawPinyin()
{
	if (!isCandidateVisiable() || !m_pCandidateDlg)
		return S_OK;
	CString raw = m_pCandidateDlg->getPinyin();
	raw.Remove(_T('\''));
	if (raw.IsEmpty())
		return S_OK;
	if (m_pEditMgr)
		m_pEditMgr->insertText(m_clientId, raw);
	hideCandidateDlg(TRUE);
	return S_OK;
}

HRESULT CTextService::handleShiftKey()
{
	// 组词期间按 Shift：临时英文上屏，把已输入的拼音字母直接输出，不切换中英文模式
	if (isCandidateVisiable() && m_pCandidateDlg &&
		m_pCandidateDlg->getInputMode() != InputMode::IM_VMode &&
		!m_pCandidateDlg->getPinyin().IsEmpty())
	{
		CString raw = m_pCandidateDlg->getPinyin();
		raw.Remove(_T('\''));
		if (!raw.IsEmpty() && m_pEditMgr)
		{
			m_pEditMgr->insertText(m_clientId, raw);
		}
		hideCandidateDlg(TRUE);
		// 标记 preInput，阻止 Shift 抬起时 changeLanguageModeUp 切换中英文模式
		m_pKeyEventSink->setPreInput(true);
	}
	return S_OK;
}

HRESULT CTextService::handleBackKey()
{
	if (isCandidateVisiable())
	{
		if (!m_pCandidateDlg->back())
		{
			if (m_pEditMgr)
				m_pEditMgr->cancel(m_clientId);
			hideCandidateDlg(TRUE);
		}
	}
	return S_OK;
}

HRESULT CTextService::handleSpaceKey(ITfContext* pContext)
{
	return isCandidateVisiable() ? commitCurrentCandidate() : S_OK;
}

HRESULT CTextService::handleSelectKey(WPARAM wParam)
{
	if (isCandidateVisiable())
	{
		CString letter;
		letter.Format(L"%c", (WCHAR)wParam);
		int num = _wtoi(letter) - 1;
		if (wParam >= 1 && wParam <= 9)
		{
			num = static_cast<int>(wParam) - 1;
		}
		else if (wParam == 0)
		{
            handleCharacterKey(L'0');
            return S_OK;
		}

		// 如果按下的数字超出候选词范围，而且字符串也不符合v模式。则将该数字作为普通输入字符处理
		int maxColNum = m_pCandidateDlg->getCurrColMaxSize();
		if (num >= maxColNum && m_pCandidateDlg->getInputMode() != InputMode::IM_VMode)
		{
			// 将数字作为普通字符添加到输入框
			if (wParam >= 1 && wParam <= 9)
			{
				WCHAR digitChar = L'1' + (static_cast<WCHAR>(wParam) - 1);
				handleCharacterKey(digitChar);
			}
			else
			{
				handleCharacterKey((WCHAR)wParam);
			}
			return S_OK;
		}

		commitCandidateByVisualColumn(num);
	}
	return S_OK;
}

HRESULT CTextService::handlePageKey(coral::tsf::CandidatePageKey direction)
{
	if (isCandidateVisiable())
	{
		if (direction == coral::tsf::CandidatePageKey::None) return S_OK;
		if (direction == coral::tsf::CandidatePageKey::Previous)
		{
			if (m_pCandidateDlg->isCandidateExpanded())
				m_pCandidateDlg->prevRow();
			return S_OK;
		}
		if (!m_pCandidateDlg->isCandidateExpanded())
		{
			m_pCandidateDlg->enterCandidateExpand();
			return S_OK;
		}
		// 翻页边界绝不能上屏选中词或输出标点。
		m_pCandidateDlg->nextRow();
	}
	return S_OK;
}

HRESULT CTextService::handleExit()
{
	if (m_pEditMgr)
		m_pEditMgr->cancel(m_clientId);
	hideCandidateDlg();
	return S_OK;
}

// 翻译期间保留原组串，直到真实结果上屏。
HRESULT CTextService::handle_translation_key(WPARAM key)
{
    if (key == VK_ESCAPE || key == VK_BACK) {
        m_pCandidateDlg->exit_translation();
        return S_OK;
    }
    if (key == VK_RETURN) return commitCurrentCandidate();
    const coral::tsf::KeyEventSnapshot event(static_cast<std::uint32_t>(key), true,
        m_pKeyEventSink->isShiftDown(), m_pKeyEventSink->isCtrlDown(),
        m_pKeyEventSink->isAltDown(), m_pKeyEventSink->isWinDown(), false);
    const coral::tsf::CandidatePageKeys enabled{
        m_pageSettings.page_up_down_enabled && m_configSnapshot->page_up_down,
        m_pageSettings.leftOrRightBracketEnabled && m_configSnapshot->bracketPage,
        m_pageSettings.commaOrPeriodEnabled && m_configSnapshot->commaPeriodPage,
        m_configSnapshot->minus_equals_page };
    const auto direction = coral::tsf::DecideCandidatePageKey(event, true, false, enabled);
    if (direction != coral::tsf::CandidatePageKey::None) {
        m_pCandidateDlg->move_translation_selection(direction == coral::tsf::CandidatePageKey::Previous ? -5 : 5);
        return S_OK;
    }
    if (key == VK_DOWN || key == VK_RIGHT) m_pCandidateDlg->move_translation_selection(1);
    else if (key == VK_UP || key == VK_LEFT) m_pCandidateDlg->move_translation_selection(-1);
    return S_OK;
}

// 剪贴板列表只响应导航、上屏、退格（回到 v）和 Esc，其余按键吞掉。
HRESULT CTextService::handleClipboardKey(WPARAM wParam)
{
    const coral::tsf::KeyEventSnapshot event(
        static_cast<std::uint32_t>(wParam), true, m_pKeyEventSink->isShiftDown(),
        m_pKeyEventSink->isCtrlDown(), m_pKeyEventSink->isAltDown(),
        m_pKeyEventSink->isWinDown(), m_pKeyEventSink->isCapsLockOn());
    const coral::tsf::CandidatePageKeys enabled{
        m_pageSettings.page_up_down_enabled && m_configSnapshot->page_up_down,
        m_pageSettings.leftOrRightBracketEnabled && m_configSnapshot->bracketPage,
        m_pageSettings.commaOrPeriodEnabled && m_configSnapshot->commaPeriodPage,
        m_configSnapshot->minus_equals_page };
    const auto direction = coral::tsf::DecideCandidatePageKey(event, true, false, enabled);
    if (direction != coral::tsf::CandidatePageKey::None) {
        const int screen = CandidateController::clipboardScreenRows();
        m_pCandidateDlg->clipboardMove(direction == coral::tsf::CandidatePageKey::Previous ? -screen : screen);
        return S_OK;
    }
	switch (wParam)
	{
	case VK_DOWN:
	case VK_RIGHT:
		m_pCandidateDlg->clipboardMove(1);
		break;
	case VK_UP:
	case VK_LEFT:
		m_pCandidateDlg->clipboardMove(-1);
		break;
	case VK_HOME:
	case VK_END:
		m_pCandidateDlg->clipboardMoveTo(wParam == VK_END);
		break;
	case VK_RETURN:
	case VK_SPACE:
		commitCurrentCandidate();
		break;
	case VK_BACK:
		handleBackKey();
		break;
	case VK_ESCAPE:
		handleExit();
		break;
	default:
		break;
	}
	return S_OK;
}

std::pair<bool, CString> CTextService::isSpecialKey(WPARAM wParam)
{
	bool isShiftDown = m_pKeyEventSink->isShiftDown();
	bool keyAssigned = false;  // 标志是否赋值
	CString res;
	switch (wParam)
	{
	case 0x31:
		res = isShiftDown ? _T("！") : _T("1");
		keyAssigned = true;
		break;
	case 0x32:
		res = isShiftDown ? _T("@") : _T("2");
		keyAssigned = true;
		break;
	case 0x33:
		res = isShiftDown ? _T("#") : _T("3");
		keyAssigned = true;
		break;
	case 0x34:
		res = isShiftDown ? _T("￥") : _T("4");
		keyAssigned = true;
		break;
	case 0x35:
		res = isShiftDown ? _T("%") : _T("5");
		keyAssigned = true;
		break;
	case 0x36:
		res = isShiftDown ? _T("……") : _T("6");
		keyAssigned = true;
		break;
	case 0x37:
		res = isShiftDown ? _T("&") : _T("7");
		keyAssigned = true;
		break;
	case 0x38:
		res = isShiftDown ? _T("*") : _T("8");
		keyAssigned = true;
		break;
	case 0x39:
		res = isShiftDown ? _T("（") : _T("9");
		keyAssigned = true;
		break;
	case 0x30:
		res = isShiftDown ? _T("）") : _T("0");
		keyAssigned = true;
		break;
	case VK_OEM_1:
		res = isShiftDown ? _T("：") : _T("；");
		keyAssigned = true;
		break;
	case VK_OEM_2:
		res = isShiftDown ? _T("？") : _T("、");
		keyAssigned = true;
		break;
	case VK_OEM_3:
		res = isShiftDown ? _T("~") : _T("·");
		keyAssigned = true;
		break;
	case VK_OEM_4:
		res = isShiftDown ? _T("{") : _T("【");
		keyAssigned = true;
		break;
	case VK_OEM_5:
		if (isFullWidth())
			res = isShiftDown ? _T("|") : _T("＼");
		else
			res = isShiftDown ? _T("|") : _T("、");
		keyAssigned = true;
		break;
	case VK_OEM_6: res = isShiftDown ? _T("}") : _T("】");
		keyAssigned = true;
		break;
	case VK_OEM_7:
		if (isShiftDown)
		{
			res = !contextInputState().preQuoteSingle ? _T("“") : _T("”");
			contextInputState().preQuoteSingle = !contextInputState().preQuoteSingle;
		}
		else
		{
			res = !contextInputState().preQuoteDouble ? _T("‘") : _T("’");
			contextInputState().preQuoteDouble = !contextInputState().preQuoteDouble;
		}
		keyAssigned = true;
		break;
	case VK_OEM_MINUS: res = isShiftDown ? _T("——") : _T("-");
		keyAssigned = true;
		break;
	case VK_OEM_COMMA: res = isShiftDown ? _T("《") : _T("，");
		keyAssigned = true;
		break;
	case VK_OEM_PERIOD: res = isShiftDown ? _T("》") : _T("。");
		keyAssigned = true;
		break;
	}
	return { keyAssigned, res };
}

HRESULT CTextService::handleDel()
{
	if (isCandidateVisiable())
	{
		if (!m_pCandidateDlg->removeNextLetter())
		{
			hideCandidateDlg(TRUE);
		}
	}
	return S_OK;
}

HRESULT CTextService::startComposition(ITfContext* pContext, WPARAM wParam, bool isPositionEnhanced)
{
	if (m_pEditMgr)
	{
		if (!m_pEditMgr->exsitComposition())
		{
			auto& input = contextInputState();
			input.compositionEnded = false;
			input.layoutChanged = false;
			input.candidatePresentationRequested = false;
			input.layoutChangeCount = 0;
			input.layoutChangeIndex = 0;
			// 针对win7下coral不能输入中文字符的特殊判断
			if (!isCandidateVisiable())
				m_pEditMgr->processbAboutToShow(wParam);
			CString initialText;
			if (const wchar_t character = coral::tsf::InitialCompositionCharacter(
				static_cast<std::uint32_t>(wParam)))
				initialText.AppendChar(character);
			m_pEditMgr->updateAccumulatedPinyin(initialText, m_clientId);
			m_pEditMgr->setPositionEnhanced(isPositionEnhanced);
			m_pEditMgr->start(m_clientId, pContext, this);
		}
	}
	return S_OK;
}

HRESULT CTextService::invokeKeyHandler(ITfContext* pContext, WPARAM wParam, LPARAM lParam)
{
	m_pCandidateDlg->applyPinyinSettings();
    if (m_pCandidateDlg->is_translation_mode()) return handle_translation_key(wParam);
    if (m_configSnapshot->translation_shortcut_available() && isCandidateVisiable() &&
        wParam == m_configSnapshot->translation_shortcut_key && !m_pKeyEventSink->isShiftDown() &&
        !m_pKeyEventSink->isCtrlDown() && !m_pKeyEventSink->isAltDown() &&
        !m_pKeyEventSink->isWinDown() && m_pCandidateDlg->begin_translation()) return S_OK;
	if (m_pCandidateDlg->isClipboardMode())
		return handleClipboardKey(wParam);
	// 展示请求可能晚于已提交文本到达。它不能把
	// 空输入缓冲变成选词或翻页会话。
	if (m_pCandidateDlg->getPinyin().IsEmpty() && isCandidateVisiable())
		hideCandidateDlg();
	// 已配置的翻页优先于点转换、自动配对和标点处理。
	const coral::tsf::KeyEventSnapshot pageEvent(
		static_cast<std::uint32_t>(wParam), true, m_pKeyEventSink->isShiftDown(),
		m_pKeyEventSink->isCtrlDown(), m_pKeyEventSink->isAltDown(),
		m_pKeyEventSink->isWinDown(), m_pKeyEventSink->isCapsLockOn());
	const coral::tsf::CandidatePageKeys pageKeys{
		m_pageSettings.page_up_down_enabled && m_configSnapshot->page_up_down,
		m_pageSettings.leftOrRightBracketEnabled && m_configSnapshot->bracketPage,
		m_pageSettings.commaOrPeriodEnabled && m_configSnapshot->commaPeriodPage,
        m_configSnapshot->minus_equals_page };
    const auto page_direction = coral::tsf::DecideCandidatePageKey(
        pageEvent, isCandidateVisiable(), isVMode(), pageKeys);
    if (page_direction != coral::tsf::CandidatePageKey::None)
        return handlePageKey(page_direction);
	CString insertText;
	/// 有些应用不走testkeydown，比如QQ​
	if (m_pEditMgr)
	{
		if (!m_pEditMgr->exsitComposition())
		{
			auto& input = contextInputState();
			input.compositionEnded = false;
			input.layoutChanged = false;
			input.candidatePresentationRequested = false;
			input.layoutChangeCount = 0;
			input.layoutChangeIndex = 0;
			m_pEditMgr->start(m_clientId, pContext, this);
			 /* if (wParam >= 'A' && wParam <= 'Z')
			 {
				 m_pEditMgr->insertPlaceholder(m_clientId);
			 }
	 */
		}
	}

	// 数字或字母间符号转换
	if ((m_configSnapshot->autoConvertDigitSymbol || m_configSnapshot->auto_convert_letter_symbol)
		&& !isCandidateVisiable()
		&& !m_pKeyEventSink->isShiftDown()
		&& !m_pKeyEventSink->isCtrlDown() && !m_pKeyEventSink->isAltDown()
		&& !m_pKeyEventSink->isWinDown()
		&& ((wParam >= '0' && wParam <= '9') || (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
			|| (wParam >= 'A' && wParam <= 'Z')))
	{
		WCHAR digitChar = 0;
		if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
		{
			// 小键盘数字转换为字符
			digitChar = static_cast<WCHAR>(L'0' + (wParam - VK_NUMPAD0));
		}
		else
		{
			digitChar = static_cast<WCHAR>(wParam);
			if (wParam >= 'A' && wParam <= 'Z' && !m_pKeyEventSink->isCapsLockOn())
				digitChar += L'a' - L'A';
		}

		if (handle_symbol_conversion(digitChar))
		{
			hideCandidateDlg();
			return S_OK;
		}
	}

	// 【符号自动补全】
	if (m_configSnapshot->autoSymbolCompletion && isAutoClosePairKey(wParam) && !isCandidateVisiable())
	{
		CString leftChar = autoPunctuationTrans(wParam);
		if (!leftChar.IsEmpty() && m_pEditMgr)
		{
			if (leftChar == _T("’"))
				leftChar = _T("‘");
			if (leftChar == _T("”"))
				leftChar = _T("“");
			CString rightChar = getAutoClosePairChar(leftChar);
			CString pairText = leftChar;
			if (!rightChar.IsEmpty())
				pairText += rightChar;
			m_pEditMgr->insertText(m_clientId, pairText);
			hideCandidateDlg();

			// 如果有配对符号，标记需要在 Shift 松开后发送左键
			if (!rightChar.IsEmpty())
			{
				if(m_pKeyEventSink->isShiftDown())
					contextInputState().needSendLeftKey = true;
				else
				{
					sendLeftKey(pContext);
				}
			}

			return S_OK;
		}
	}

	int inputType = 0;
	switch (wParam)
	{
	case VK_OEM_MINUS: // - / _
	case VK_OEM_PLUS: // = / +
	{
        // 已配置的翻页已由上方的共享决策处理。
        if (isCandidateVisiable()) {
            handleCharacterKey(wParam == VK_OEM_MINUS
                ? (m_pKeyEventSink->isShiftDown() ? L'_' : L'-')
                : (m_pKeyEventSink->isShiftDown() ? L'+' : L'='));
        } else if (wParam == VK_OEM_MINUS) {
            insertText = chinesePunctuationTrans(wParam);
        } else {
            insertText = m_pKeyEventSink->isShiftDown() ? L"+" : L"=";
        }
        break;
	}
	case VK_LEFT:
	case VK_RIGHT:
	case VK_UP:
	case VK_DOWN:
	{
		handleArrowKey(wParam);
		break;
	}
	case VK_SEPARATOR: // 不知道是不是shift键，待测试TODO
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT:
	{
		handleShiftKey();
		break;
	}
	case VK_RETURN:
	{
		handleReturnKey();
		break;
	}
	case VK_SPACE:
	{
		handleSpaceKey(pContext);
		break;
	}
	case VK_BACK:
	{
		handleBackKey();
		break;
	}
	case VK_ESCAPE:
	{
		inputType = 1;
		handleExit();
		break;
	}
	case VK_NUMPAD0: // 特判这个0，直接插入
	{
		handleCharacterKey(_T('0'));
		break;
	}
	case VK_NUMPAD1:
	case VK_NUMPAD2:
	case VK_NUMPAD3:
	case VK_NUMPAD4:
	case VK_NUMPAD5:
	case VK_NUMPAD6:
	case VK_NUMPAD7:
	case VK_NUMPAD8:
	case VK_NUMPAD9:
	{
		if (m_pKeyEventSink->isShiftDown())
		{
			handleReturnKey();
		}
		else
		{
			int num = static_cast<int>(wParam) - VK_NUMPAD1 + 1;
			CString str;
			str.Format(_T("%d"), num);  // 使用 Format 方法将整数转换为字符串
			if (isVMode()) {
				handleCharacterKey(str.GetAt(0));
			}
			else {
				handleSelectKey(str.GetAt(0));
			}
		}
		break;
	}
	case VK_MULTIPLY:       // *
	case VK_ADD:            // +
	case VK_SUBTRACT:       // -
	case VK_DIVIDE:         // /
	case VK_DECIMAL:        // .
	{
		handlePadChar(wParam);
		break;
	}
	case VK_OEM_1: // ;:
	{
		handlePunctuation(wParam);
		break;
	}
	case VK_OEM_2: // /?
	{
		handlePunctuation(wParam);
		break;
	}
	case VK_OEM_3: // `~
	{
		handlePunctuation(wParam);
		break;
	}
	case VK_OEM_5: // \|
	{
		handlePunctuation(wParam);
		break;
	}
	case VK_OEM_7: // '"
	{
		CString letter = autoPunctuationTrans(wParam);
		if (isChinesePunctuation(letter))
		{
			// 分隔符特殊处理
			if (isCandidateVisiable() && !m_pKeyEventSink->isShiftDown())
				handleCharacterKey(_T('\''));
			else
				handleChinesePunctuation(wParam);
		}
		else
		{
			handleCharacterKey(letter.GetAt(0));
		}
		break;
	}
	case VK_OEM_4: // [ / {
	case VK_OEM_6: // ] / }
	case VK_OEM_COMMA: // , / <
	case VK_OEM_PERIOD: // . / >
	{
        if (isCandidateVisiable()) {
            if (isVMode() && wParam == VK_OEM_PERIOD && !m_pKeyEventSink->isShiftDown())
                handleCharacterKey(L'.');
            else
                handlePunctuation(wParam);
        } else {
            handleChinesePunctuation(wParam);
        }
        break;
	}
	case VK_HOME:
	case VK_END:
	{
		if (!m_pKeyEventSink->isCtrlDown() && !m_pKeyEventSink->isShiftDown() && !m_pKeyEventSink->isAltDown())
			handleArrowKey(wParam);
		break;
	}
	case VK_DELETE:
	{
		// 没有ctrl/shift/alt按下的时候可以delete
		if (!m_pKeyEventSink->isCtrlDown() && !m_pKeyEventSink->isShiftDown() && !m_pKeyEventSink->isAltDown())
		{
			handleDel();
		}
		break;
	}
	default:
		if ((wParam >= 'A' && wParam <= 'Z'))
		{
			const CString& pinyin = m_pCandidateDlg->getPinyin();
			if (wParam == 'B' && isVMode() && m_pCandidateDlg->clipboardEntryIndex() >= 0)
			{
				commitCandidateByIndex(m_pCandidateDlg->clipboardEntryIndex());
			}
			else if (isVMode()
				&& m_pCandidateDlg->get_pinyin_length() >= 2
				&& wParam <= 'I')
			{
				const int selCol = static_cast<int>(wParam) - 'A' + 1;
				if (selCol <= m_pCandidateDlg->getCurrColMaxSize()) {
					handleSelectKey(selCol);
				}
				else {
					handleCharacterKey(wParam);
				}
			}
			else
			{
				handleCharacterKey(wParam);
			}
		}
		else if (wParam >= '0' && wParam <= '9')
		{
			if (m_pCandidateDlg->isMouseSelected())
			{
				int selCol = static_cast<int>(wParam) - 49 + 1;
				handleSelectKey(selCol);
				m_pCandidateDlg->setMouseSelectedState(false);
			}
			else
			{
				if (isVMode())
				{
					// 在 v 模式下，数字键用于输入
					handleCharacterKey(englishPunctuationTrans(wParam).GetAt(0));
				}
				else
				{
                    if (wParam == '0') {
                        handlePunctuation(wParam);
                    }
					else
					{
						if (isCandidateVisiable())
						{
							if (m_pKeyEventSink->isShiftDown())
							{
								CString letter = autoPunctuationTrans(wParam);
								if (isChinesePunctuation(letter))
								{
									letter = chinesePunctuationTrans(wParam);
									insertText = getContent(m_pCandidateDlg->getSelCol());
									insertText += letter;
								}
								else
								{
									handleCharacterKey(letter.GetAt(0));
								}
							}
							else
							{
								int selCol = static_cast<int>(wParam) - 49 + 1;
								handleSelectKey(selCol);
							}
						}
						else
						{
							insertText = chinesePunctuationTrans(wParam);
						}
					}
				}
			}
		}
	}

	if (!insertText.GetLength())
		return S_OK;

	setKeepPlaceholder(true);
	// inputType  0:完成本次输入; 1:退出本次输入
	if (!inputType)
	{
		if (m_pEditMgr)
		{
			if (contextInputState().fullWidthInChineseMode)
				m_pEditMgr->insertText(m_clientId, hlp::HalfToFull(insertText));
			else
				m_pEditMgr->insertText(m_clientId, (wParam == VK_RETURN && isFullWidth()) ? hlp::HalfToFull(insertText) : insertText);
		}
	}
	hideCandidateDlg();

	return S_OK;
}

//英文模式下的全角输入
HRESULT CTextService::invokeFullWidthCharactersKeyHandler(ITfContext* pContext, WPARAM wParam, LPARAM lParam)
{
	// 某些应用（如 QQ）不触发 testkeydown，需要在此处手动检测并启动编辑会话
	if (m_pEditMgr)
	{
		if (!m_pEditMgr->exsitComposition())
		{
			auto& input = contextInputState();
			input.compositionEnded = false;
			input.layoutChanged = false;
			input.candidatePresentationRequested = false;
			input.layoutChangeCount = 0;
			input.layoutChangeIndex = 0;
			m_pEditMgr->start(m_clientId, pContext, this);
		}
	}
	CString selected_text;
	bool shiftDown = m_pKeyEventSink->isShiftDown();
	// CapsLock 状态可通过 (GetKeyState(VK_CAPITAL) & 1) != 0 判断

	// 1) 大写字母 (A..Z)
	if (wParam >= 'A' && wParam <= 'Z')
	{
		// 将英文字母转换为全角。若按下 Shift 或 CapsLock，就输出大写，否则小写
		bool capsLock = m_pKeyEventSink->isCapsLockOn();

		if (shiftDown || capsLock)
		{
			// 全角大写
			selected_text = static_cast<wchar_t>(wParam + 0xFEE0);
		}
		else
		{
			// 全角小写：先变小写，再加 0xFEE0
			selected_text = static_cast<wchar_t>(wParam + 0xFEE0 + ('a' - 'A'));
		}
	}
	// 2) 顶排数字 (0..9)
	else if (wParam >= '0' && wParam <= '9')
	{
		// 如果按下 Shift，则输出对应的全角符号
		if (shiftDown)
		{
			switch (wParam)
			{
			case '1':  selected_text = L'！'; break;
			case '2':  selected_text = L'＠'; break;
			case '3':  selected_text = L'＃'; break;
			case '4':  selected_text = L'＄'; break;
			case '5':  selected_text = L'％'; break;
			case '6':  selected_text = L'＾'; break;
			case '7':  selected_text = L'＆'; break;
			case '8':  selected_text = L'＊'; break;
			case '9':  selected_text = L'（'; break;
			case '0':  selected_text = L'）'; break;
			default:   return S_FALSE;
			}
		}
		else
		{
			// 普通情况：数字转为全角
			selected_text = static_cast<wchar_t>(wParam + 0xFEE0);
		}
	}
	// 3) 小键盘数字 (VK_NUMPAD0..VK_NUMPAD9)
	else if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
	{
		// 小键盘 0~9 统一转为全角数字
		UINT numValue = static_cast<UINT>(wParam) - VK_NUMPAD0;
		wchar_t digitChar = L'0' + numValue;
		selected_text = static_cast<wchar_t>(digitChar + 0xFEE0);
	}
	// 4) 其他符号 & 标点处理 (OEM / Space / Plus 等)
	else
	{
		switch (wParam)
		{
		case VK_SPACE:  // 空格
			// 全角空格，无论是否按下 Shift 都是同一个字符
			selected_text = L'　';
			break;
		case VK_OEM_1:  // ; :
			selected_text = shiftDown ? L'：' : L'；';
			break;
		case VK_OEM_2:  // / ?
			selected_text = shiftDown ? L'？' : L'／';
			break;
		case VK_OEM_3:  // ` ~
			selected_text = shiftDown ? L'～' : L'｀';
			break;
		case VK_OEM_4:  // [ {
			selected_text = shiftDown ? L'｛' : L'［';
			break;
		case VK_OEM_5:  // \ |
			selected_text = shiftDown ? L'｜' : L'＼';
			break;
		case VK_OEM_6:  // ] }
			selected_text = shiftDown ? L'｝' : L'］';
			break;
		case VK_OEM_7:  // ' "
			selected_text = shiftDown ? L'＂' : L'＇';
			break;
		case VK_OEM_COMMA:   // , <
			selected_text = shiftDown ? L'＜' : L'，';
			break;
		case VK_OEM_PERIOD:  // . >
			selected_text = shiftDown ? L'＞' : L'．';
			break;
		case VK_OEM_MINUS:   // - _
			selected_text = shiftDown ? L'＿' : L'－';
			break;
		case VK_OEM_PLUS:    // = +
			selected_text = shiftDown ? L'＋' : L'＝';
			break;
		case VK_ADD:        // 小键盘 '+'
			selected_text = L'＋';
			break;
		case VK_SUBTRACT:   // 小键盘 '-'
			selected_text = L'－';
			break;
		case VK_MULTIPLY:   // 小键盘 '*'
			selected_text = L'＊';
			break;
		case VK_DIVIDE:     // 小键盘 '/'
			selected_text = L'／';
			break;
		case VK_DECIMAL:    // 小键盘 '.'
			selected_text = L'．';
			break;

		default:
			return S_FALSE;
		}
	}

	// 【符号自动补全】英文全角模式下的配对符号处理
	if (!selected_text.IsEmpty() && m_configSnapshot->autoSymbolCompletion && isAutoClosePairKey(wParam))
	{
		CString rightChar = getAutoClosePairChar(selected_text);
		if (!rightChar.IsEmpty())
		{
			// 自动插入配对的右半部分
			selected_text += rightChar;
		}
	}

	// 插入到上下文
	if (!selected_text.IsEmpty())
	{
		if (m_pEditMgr)
		{
			m_pEditMgr->insertText(m_clientId, selected_text);
		}
		hideCandidateDlg();

		// 【符号自动补全】如果插入了配对符号，光标左移
		if (m_configSnapshot->autoSymbolCompletion && isAutoClosePairKey(wParam))
		{
			CString rightChar = getAutoClosePairChar(selected_text.Left(1));
			if (!rightChar.IsEmpty())
			{
				if (m_pKeyEventSink->isShiftDown())
					contextInputState().needSendLeftKey = true;
				else
				{
					sendLeftKey(pContext);
				}
			}
		}

		return S_OK;
	}

	return S_FALSE;
}

// 判断按键是否是配对符号的左半部分
bool CTextService::isAutoClosePairKey(WPARAM wParam) const
{
	return isAutoClosePairKey(wParam, m_pKeyEventSink->isShiftDown());
}

HRESULT CTextService::ensureEngineSessionForKey(ITfContext* context)
{
	coral::input_timing::Scope timing(L"tsf.ensure_session");
	auto* state = m_tsfThreadContext.contexts().find(context);
	if (!state || activeContextState() != state)
		return E_FAIL;
	if (state->engineSession().valid())
		return S_OK;
	const auto createStart = coral::input_timing::nowUs();
	const auto session_id = session_bridge_->create_session();
	coral::input_timing::mark(L"tsf.create_session", createStart, session_id);
	if (!session_id)
	{
		LOG_ERROR_W(L"[tsf_engine] event=create_session_failed process=" +
			std::wstring(m_processName.GetString()));
		return HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);
	}
	state->setEngineSession({ session_id });
	m_pCandidateDlg->set_candidate_provider(
		session_bridge_, session_id, m_runtimeMode.traditional());
	if (m_configSnapshot)
	{
		const auto configStart = coral::input_timing::nowUs();
		session_bridge_->apply_settings(session_id,
			m_configSnapshot->engineConfig(m_runtimeMode.full_width(), m_runtimeMode.traditional()));
		coral::input_timing::mark(L"tsf.session_config", configStart, session_id);
	}
	return S_OK;
}

HRESULT CTextService::selectContextController(ITfContext* context)
{
	coral::tsf::ContextState* state = nullptr;
	const HRESULT hr = m_tsfThreadContext.contexts().ensure(context, &state);
	if (FAILED(hr) || !state)
		return FAILED(hr) ? hr : E_FAIL;
	CEditMgr* controller = &state->compositionController();
	if (m_pEditMgr != controller)
		controller->setProcessName(m_processName);
	m_pEditMgr = controller;
	m_pCandidateDlg = &state->candidateController();
	m_pCandidateListUIElement = &state->candidateUIElement();
	m_activeContextToken = state->token();
	if (m_pKeyEventSink)
		m_pKeyEventSink->activateContext(context);
	m_pCandidateDlg->setTextService(this);
	m_pCandidateDlg->setConfigSnapshot(m_configSnapshot);
	m_pCandidateDlg->set_candidate_provider(
		session_bridge_, state->engineSession().value, m_runtimeMode.traditional());
	const auto& candidateSnapshot = state->candidates();
	m_pCandidateDlg->showCandidateDlg(candidateSnapshot.visible);
	m_pCandidateListUIElement->syncFrom(candidateSnapshot);
	return S_OK;
}

coral::tsf::ContextState* CTextService::activeContextState()
{
	return m_tsfThreadContext.contexts().find(m_activeContextToken);
}

const coral::tsf::ContextState* CTextService::activeContextState() const
{
	return m_tsfThreadContext.contexts().find(m_activeContextToken);
}

coral::tsf::ContextInputState& CTextService::contextInputState()
{
	if (auto* state = activeContextState())
		return state->inputState();
	return m_detachedInputState;
}

const coral::tsf::ContextInputState& CTextService::contextInputState() const
{
	if (const auto* state = activeContextState())
		return state->inputState();
	return m_detachedInputState;
}

void CTextService::clearActiveContext()
{
	if (m_pKeyEventSink)
		m_pKeyEventSink->activateContext(nullptr);
	m_activeContextToken = {};
	m_pEditMgr = nullptr;
	m_pCandidateDlg = nullptr;
	m_pCandidateListUIElement = nullptr;
	m_detachedInputState = {};
}

void CTextService::prepareContextClose(coral::tsf::ContextState& state)
{
	// 在 ContextState 及其控制器仍然存活时释放所有宿主可见对象。
	// 特别是不能在其底层 Context 消失后
	// 仍保留 UILess 元素的注册。
	state.candidateUIElement().endUI();
	state.candidateController().showCandidateDlg(false);
	m_rendererCoordinator.hideContext(state, state.candidates().version + 1);

	if (m_clientId != TF_CLIENTID_NULL && state.compositionController().exsitComposition())
	{
		const HRESULT cancelHr = state.compositionController().cancelSynchronously(m_clientId);
		if (FAILED(cancelHr))
		{
			LOG_ERROR_W(L"[tsf_context] synchronous composition cancel failed hr=" +
				std::to_wstring(static_cast<unsigned long>(cancelHr)));
		}
	}

	if (m_pKeyEventSink)
		m_pKeyEventSink->clearContext(state.context());
	if (m_activeContextToken.contextId == state.token().contextId &&
		m_activeContextToken.generation == state.token().generation)
	{
		clearActiveContext();
	}
}

coral::composition::SessionToken CTextService::activeEngineSession() const
{
	const auto* state = activeContextState();
	return state ? state->engineSession().value : 0;
}

void CTextService::refreshConfigSnapshot(bool force)
{
	auto next = m_configAuthority.refresh(force);
	if (next == m_configSnapshot) return;
	m_configSnapshot = std::move(next);
	if (m_pCandidateDlg)
		m_pCandidateDlg->setConfigSnapshot(m_configSnapshot);
	if (m_configSnapshot)
	{
		if (const auto session = activeEngineSession())
			session_bridge_->apply_settings(session,
				m_configSnapshot->engineConfig(m_runtimeMode.full_width(), m_runtimeMode.traditional()));
	}
}

void CTextService::syncEngineConfig()
{
	if (!m_configSnapshot)
		refreshConfigSnapshot(true);
	if (const auto session = activeEngineSession())
		session_bridge_->apply_settings(session,
			m_configSnapshot->engineConfig(m_runtimeMode.full_width(), m_runtimeMode.traditional()));
}

bool CTextService::isAutoClosePairKey(WPARAM wParam, bool shiftDown) const
{
	switch (wParam)
	{
	case '9': // Shift+9 = (
		return shiftDown;
	case VK_OEM_4: // [ 或 Shift+[ = {
		return true;
	case VK_OEM_7: // ' 或 Shift+' = "
		return true;
	case VK_OEM_COMMA: // Shift+, = <
		return shiftDown;
	default:
		return false;
	}
}

bool CTextService::isOutputChar(WPARAM wParam) const
{
	// 判断是否是可输出的字符
	return ((wParam >= '0' && wParam <= '9') ||
			(wParam >= 'A' && wParam <= 'Z') ||
			(wParam >= 'a' && wParam <= 'z') ||
			(wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) ||  // 小键盘数字
			(wParam >= VK_OEM_1 && wParam <= VK_OEM_7) ||  // 符号键
			wParam == VK_OEM_MINUS || wParam == VK_OEM_PLUS ||
			wParam == VK_OEM_COMMA || wParam == VK_OEM_PERIOD ||
			wParam == VK_SPACE);
}

bool CTextService::needs_symbol_conversion(WPARAM wParam) const
{
	return needs_symbol_conversion(wParam, activeContextState());
}

bool CTextService::needs_symbol_conversion(
	WPARAM wParam, const coral::tsf::ContextState* state) const
{
	if (!m_configSnapshot->autoConvertDigitSymbol && !m_configSnapshot->auto_convert_letter_symbol)
		return false;
	if (state && (state->candidates().visible || state->candidateController().isVisible()))
		return false;
	if (!((wParam >= '0' && wParam <= '9') || (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
		|| (wParam >= 'A' && wParam <= 'Z')))
		return false;
	// 检查已提交的数字/字母与中文符号
	if (!state)
		return false;
	const CString& lastTwoChars = state->compositionController().getLastTwoChars();
	if (lastTwoChars.GetLength() < 2)
		return false;
	WCHAR lastChar = lastTwoChars.GetAt(1);
	WCHAR secondLastChar = lastTwoChars.GetAt(0);
	const wchar_t next = wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9
		? static_cast<wchar_t>(L'0' + wParam - VK_NUMPAD0) : static_cast<wchar_t>(wParam);
	return coral::tsf::separator_replacement(m_configSnapshot->autoConvertDigitSymbol,
		m_configSnapshot->auto_convert_letter_symbol, secondLastChar, lastChar, next) != L'\0';
}

// 获取配对符号的右半部分
CString CTextService::getAutoClosePairChar(const CString& leftChar) const
{
	if (leftChar.GetLength() != 1)
		return L"";
	wchar_t ch = leftChar.GetAt(0);

	// 半角符号配对
	switch (ch)
	{
	case L'(': return L")";
	case L'[': return L"]";
	case L'{': return L"}";
	case L'\"': return L"\"";
	case L'\'': return L"\'";
	case L'<': return L">";
	default: break;
	}

	// 全角符号配对
	switch (ch)
	{
	case L'（': return L"）";
	case L'［': return L"］";
	case L'｛': return L"｝";
	case L'＂': return L"＂";
	case L'＇': return L"＇";
	case L'＜': return L"＞";
	case L'《': return L"》";
	case L'【': return L"】";
	case L'「': return L"」";
	case L'『': return L"』";
	case L'“': return L"”";
	case L'‘': return L"’";
	default: break;
	}

	return L"";
}

coral::tsf::KeyHandlingDecision CTextService::previewKeyDecision(
	ITfContext* context, WPARAM wParam, bool keyDown, bool shiftDown, bool ctrlDown,
	bool altDown, bool winDown, bool capsLockOn)
{
	const coral::tsf::KeyEventSnapshot event(
		static_cast<std::uint32_t>(wParam), keyDown, shiftDown, ctrlDown,
		altDown, winDown, capsLockOn);
	return decideKeyHandling(event, m_tsfThreadContext.contexts().find(context));
}

void CTextService::executeKeyDecision(
	const coral::tsf::KeyHandlingDecision& decision)
{
	applyKeyHandlingEffects(decision);
}

coral::tsf::KeyHandlingDecision CTextService::decideKeyHandling(
	const coral::tsf::KeyEventSnapshot& event,
	const coral::tsf::ContextState* state) const
{
	const bool candidates_visible = state &&
		(state->candidates().visible || state->candidateController().isVisible());
	const coral::tsf::KeyHandlingContextSnapshot context{
		candidates_visible,
		state && state->candidateController().isMouseSelected(),
		m_pLanguageProfile->getAsciiMode(),
		isFullWidth(),
		m_pCompartmentEventSink->isKeyboardDisable() != FALSE,
		m_configSnapshot->useEnglishPunctuation,
		state ? state->inputState().punctuationLanguageMode
			: m_detachedInputState.punctuationLanguageMode,
		event.virtualKey() == VK_BACK && !candidates_visible,
		m_configSnapshot->autoSymbolCompletion &&
			isAutoClosePairKey(event.virtualKey(), event.shift()),
		!event.shift() && !event.ctrl() && !event.alt() && !event.win() &&
			needs_symbol_conversion(event.virtualKey(), state),
		m_configSnapshot->secondThirdCandidateKey,
		m_configSnapshot->wordToCharacterKey,
        m_pageSettings.page_up_down_enabled && m_configSnapshot->page_up_down,
        m_configSnapshot->translation_shortcut_available() && state &&
            state->candidateController().getInputMode() == InputMode::IM_Normal &&
            !state->candidateController().getPinyin().IsEmpty(),
        state && state->candidateController().is_translation_mode(),
        m_configSnapshot->translation_shortcut_key,
        m_configSnapshot->minus_equals_page,
	};
	return coral::tsf::DecideKeyHandling(event, context);
}

void CTextService::applyKeyHandlingEffects(
	const coral::tsf::KeyHandlingDecision& decision)
{
	if (decision.markPreInput)
		m_pKeyEventSink->setPreInput(true);
	if (decision.fullWidthStateEffect == coral::tsf::FullWidthStateEffect::Set)
		contextInputState().fullWidthInChineseMode = true;
	else if (decision.fullWidthStateEffect == coral::tsf::FullWidthStateEffect::Clear)
		contextInputState().fullWidthInChineseMode = false;

	if (decision.inputHistoryEffect == coral::tsf::InputHistoryEffect::ReconcileBackspace)
		reconcileInputHistoryOnBackspace();
	else if (decision.inputHistoryEffect == coral::tsf::InputHistoryEffect::Reset)
		resetInput();

	switch (decision.candidateSelectionEffect)
	{
	case coral::tsf::CandidateSelectionEffect::SecondCandidate:
		selectCandidateByIndex(1);
		break;
	case coral::tsf::CandidateSelectionEffect::ThirdCandidate:
		selectCandidateByIndex(2);
		break;
	case coral::tsf::CandidateSelectionEffect::FirstCharacter:
		selectCharacterFromWord(0);
		break;
	case coral::tsf::CandidateSelectionEffect::LastCharacter:
		selectCharacterFromWord(-1);
		break;
	default:
		break;
	}
}

void CTextService::reconcileInputHistoryOnBackspace()
{
	if (!m_pEditMgr)
		return;
	bool reverted = false;
	CString& allInput = m_pEditMgr->getAllInput();
	if (allInput.IsEmpty())
		return;
	const wchar_t lastChar = allInput.GetAt(allInput.GetLength() - 1);
	if (lastChar == 0x200B)
	{
		allInput.Truncate(allInput.GetLength() - 1);
		return;
	}

	auto& candInput = m_pEditMgr->getCandInput();
	for (int i = static_cast<int>(candInput.size()) - 1; i >= 0; --i)
	{
		if (candInput[i].IsEmpty() || candInput[i].GetAt(candInput[i].GetLength() - 1) != lastChar)
			continue;
		if (const auto session = activeEngineSession())
			session_bridge_->revert_last_action(
				session, std::string(CT2A(candInput[i], CP_UTF8)));
		reverted = true;
		const int length = candInput[i].GetLength();
		if (allInput.GetLength() >= length)
		{
			allInput.Truncate(allInput.GetLength() - length);
			for (int j = 0; j < length - 1; ++j)
				allInput += L"\u200B";
		}
		candInput.erase(candInput.begin() + i);
		break;
	}
	if (!reverted)
		resetInput();
}

CString CTextService::autoPunctuationTrans(WPARAM wParam)
{
	bool useEnglishPunct = true;
	if (!isVMode() && !m_pLanguageProfile->getAsciiMode() && !contextInputState().englishPunctuationMode)
	{
		useEnglishPunct = isUsingEnglishPunctuation();
	}
	if (useEnglishPunct)
		return englishPunctuationTrans(wParam);
	else
		return chinesePunctuationTrans(wParam, wParam == VK_OEM_7 ? false : true);
}

CString CTextService::chinesePunctuationTrans(const WPARAM& wParam, bool isNeedChangeQuote)
{
	bool isShiftDown = m_pKeyEventSink->isShiftDown();
	CString res;
	switch (wParam)
	{
	case 0x31: res = isShiftDown ? _T("！") : _T("1");
		break;
	case 0x32: res = isShiftDown ? _T("@") : _T("2");
		break;
	case 0x33: res = isShiftDown ? _T("#") : _T("3");
		break;
	case 0x34: res = isShiftDown ? _T("￥") : _T("4");
		break;
	case 0x35: res = isShiftDown ? _T("%") : _T("5");
		break;
	case 0x36: res = isShiftDown ? _T("……") : _T("6");
		break;
	case 0x37: res = isShiftDown ? _T("&") : _T("7");
		break;
	case 0x38: res = isShiftDown ? _T("*") : _T("8");
		break;
	case 0x39: res = isShiftDown ? _T("（") : _T("9");
		break;
	case 0x30: res = isShiftDown ? _T("）") : _T("0");
		break;
	case VK_OEM_1: res = isShiftDown ? _T("：") : _T("；");
		break;
	case VK_OEM_2: res = isShiftDown ? _T("？") : _T("、");
		break;
	case VK_OEM_3: res = isShiftDown ? _T("~") : _T("·");
		break;
	case VK_OEM_4: res = isShiftDown ? _T("{") : _T("【");
		break;
	case VK_OEM_5: res = isShiftDown ? _T("|") : _T("、");
		break;
	case VK_OEM_6: res = isShiftDown ? _T("}") : _T("】");
		break;
	case VK_OEM_7:
		if (!m_pKeyEventSink->isPreQuote())
		{
			res = isShiftDown ? _T("“") : _T("‘");
		}
		else
		{
			res = isShiftDown ? _T("”") : _T("’");
		}
		if(isNeedChangeQuote)
			m_pKeyEventSink->setPreQuote(!m_pKeyEventSink->isPreQuote());
		break;
	case VK_OEM_MINUS:
		res = isShiftDown ? _T("——") : _T("-");
		break;
	case VK_OEM_COMMA: res = isShiftDown ? _T("《") : _T("，");
		break;
	case VK_OEM_PERIOD: res = isShiftDown ? _T("》") : _T("。");
		break;
	default:
		res.Format(_T("%c"), (TCHAR)wParam);
		break;
	}
	return res;
}

bool CTextService::isChinesePunctuation(const CString& text) const
{
	if (text.IsEmpty())
		return false;
	static const LPCTSTR chinesePunctuations[] = {
		_T("！"),
		_T("￥"),
		_T("……"),
		_T("（"),
		_T("）"),
		_T("："),
		_T("；"),
		_T("？"),
		_T("、"),
		_T("·"),
		_T("【"),
		_T("】"),
		_T("“"),
		_T("”"),
		_T("‘"),
		_T("’"),
		_T("——"),
		_T("，"),
		_T("。"),
		_T("《"),
		_T("》"),
		NULL
	};
	for (int i = 0; chinesePunctuations[i] != NULL; i++)
	{
		if (text.Find(chinesePunctuations[i]) != -1)
		{
			return true;
		}
	}
	return false;
}

CString CTextService::englishPunctuationTrans(const WPARAM wParam)
{
	bool isShiftDown = m_pKeyEventSink->isShiftDown();
	CString res;
	switch (wParam)
	{
	case 0x31:
		res = isShiftDown ? _T("!") : _T("1");
		break;
	case 0x32:
		res = isShiftDown ? _T("@") : _T("2");
		break;
	case 0x33:
		res = isShiftDown ? _T("#") : _T("3");
		break;
	case 0x34:
		res = isShiftDown ? _T("$") : _T("4");
		break;
	case 0x35:
		res = isShiftDown ? _T("%") : _T("5");
		break;
	case 0x36:
		res = isShiftDown ? _T("^") : _T("6");
		break;
	case 0x37:
		res = isShiftDown ? _T("&") : _T("7");
		break;
	case 0x38:
		res = isShiftDown ? _T("*") : _T("8");
		break;
	case 0x39:
		res = isShiftDown ? _T("(") : _T("9");
		break;
	case 0x30:
		res = isShiftDown ? _T(")") : _T("0");
		break;
	case VK_OEM_1:
		res = isShiftDown ? _T(":") : _T(";");
		break;
	case VK_OEM_2:
		res = isShiftDown ? _T("?") : _T("/");
		break;
	case VK_OEM_3:
		res = isShiftDown ? _T("~") : _T("`");
		break;
	case VK_OEM_4:
		res = isShiftDown ? _T("{") : _T("[");
		break;
	case VK_OEM_5:
		res = isShiftDown ? _T("|") : _T("\\");
		break;
	case VK_OEM_6:
		res = isShiftDown ? _T("}") : _T("]");
		break;
	case VK_OEM_7:
		res = isShiftDown ? _T("\"") : _T("'");
		break;
	case VK_OEM_MINUS:
		res = isShiftDown ? _T("_") : _T("-");
		break;
	case VK_OEM_COMMA:
		res = isShiftDown ? _T("<") : _T(",");
		break;
	case VK_OEM_PERIOD:
		res = isShiftDown ? _T(">") : _T(".");
		break;
	default:
		res.Format(_T("%c"), (TCHAR)wParam);
		break;
	}
	return res;
}

void CTextService::updateLanguageMode(ITfContext* pic, bool updateMode) // 切换输入法并回车
{
    resetConditionWord();
	// 按回车逻辑
	if (isCandidateVisiable())
	{
		if (m_pEditMgr)
		{
			m_pEditMgr->start(m_clientId, pic, this);
		}
		//原来这个位置要传给主进程获得select text的，现在不需要了
		//而这个coreapi在现在没有实现，所以select text就是空，所以导致有些时候 比如shift抬起的时候没有输出
// 		CRect rc{ -1,-1,-1,-1 };
// 		CString selected_text;
// 		int isCandidateShow = 0;
// 		auto status = Coreapi::invokeTextInputController(VK_RETURN, 0, CandidateDlgRc2Str(rc), selected_text, isCandidateShow);
//
// 		m_pEditMgr->insertText(m_clientId, isFullWidth() ? hlp::HalfToFull(selected_text) : selected_text);
// 		hideCandidateDlg();
		CString inputText;
		m_pCandidateDlg->selectCol(inputText);
		if (m_pEditMgr)
		{
			m_pEditMgr->insertText(m_clientId, isFullWidth() ? hlp::HalfToFull(inputText) : inputText);
		}
		hideCandidateDlg(TRUE);
	}
	if (updateMode) // 当按shift + ctrl的时候会切换其他输入法，此时不让模式更换
	{
		m_pLanguageProfile->updateAsciiMode();
		bool isEnglishMode = m_pLanguageProfile->getAsciiMode();
		m_pCompartmentEventSink->updateChineseMode(!isEnglishMode);
		if (isEnglishMode)
		{
			// 英文状态只能是英文标点
			m_pCompartmentEventSink->updateChinesePunctuationMode(FALSE);
		}
		else
		{
			// 切回中文时恢复当前 Context 的标点偏好（0=跟随配置 1=英文 2=中文）。
			const int punctuationMode = contextInputState().punctuationLanguageMode;
			bool useChinesePunct = (punctuationMode == 2)
				|| (punctuationMode == 0 && !m_configSnapshot->useEnglishPunctuation);
			m_pCompartmentEventSink->updateChinesePunctuationMode(useChinesePunct);
		}
	}
}

const CString& CTextService::getProcessName() const
{
	return m_processName;
}

void CTextService::sendLeftKey(ITfContext* pContext) const
{
	if (m_processName.CompareNoCase(_T("et.exe")) == 0
		|| m_processName.CompareNoCase(_T("excel.exe")) == 0)
	{
		return;
	}
	keybd_event(VK_LEFT, 0, 0, 0);
	keybd_event(VK_LEFT, 0, KEYEVENTF_KEYUP, 0);
}

bool CTextService::canShowCandidate() const
{
	if (!m_pEditMgr)
		return false;
	bool bShow = true;
	const auto& input = contextInputState();
	if ((!m_pEditMgr->exsitComposition()) ||
		(input.compositionEnded
		&& ((input.layoutChangeCount >= 2 && input.layoutChangeIndex <= 1) ||
			(input.layoutChangeCount == 1 && input.layoutChangeIndex == 0)))) {
		bShow = false;
	}
	return bShow;
}

void CTextService::showCandidateDlg()
{
	if (!m_pCandidateDlg)
		return;
	createCandidateDlg();
	if (!m_pCandidateDlg->isVisible()) {
		beginCandidateUIElement();
	}
	auto& input = contextInputState();
	input.candidatePresentationRequested = true;
	CRect rc{};
	bool anchorReady = input.layoutChanged;
	if (m_pEditMgr && m_pEditMgr->getTextBound(m_clientId, rc) == S_OK &&
		rc.bottom > rc.top)
	{
		input.layoutChanged = true;
		anchorReady = true;
	}
	else if (TryGetCandidateFallbackAnchor(getContext(), rc))
	{
		input.layoutChanged = true;
		anchorReady = true;
	}
	else if (const auto* state = activeContextState(); state &&
		state->candidates().anchor.right > state->candidates().anchor.left)
	{
		rc = state->candidates().anchor;
		anchorReady = true;
	}
	const bool bShow = anchorReady && canShowCandidate();
	if (auto* state = activeContextState())
	{
		auto snapshot = state->candidates();
		++snapshot.version;
		snapshot.anchor = rc;
		snapshot.visible = bShow;
		state->setCandidates(std::move(snapshot));
	}
	publishCandidateSnapshot();
	m_pCandidateDlg->showCandidateDlg(bShow);
	m_pCandidateDlg->setInputArea(rc, false);
}

void CTextService::adjustCandidateDlgPos(const CRect& rcInput) {
	auto& input = contextInputState();
	input.layoutChangeIndex++;
	if (m_pCandidateDlg) {
		auto* state = activeContextState();
		if (input.candidatePresentationRequested ||
			(state && state->candidates().visible) || m_pCandidateDlg->isVisible()) {
			bool bShow = input.candidatePresentationRequested && canShowCandidate();
			if (state)
			{
				auto snapshot = state->candidates();
				++snapshot.version;
				snapshot.anchor = rcInput;
				snapshot.visible = bShow;
				state->setCandidates(std::move(snapshot));
			}
			publishCandidateSnapshot();
			m_pCandidateDlg->showCandidateDlg(bShow);
			m_pCandidateDlg->setInputArea(rcInput, false);
		}
	}
	if (!input.compositionEnded) {
		input.layoutChangeCount++;
	}
}

void CTextService::hideCandidateDlg(BOOL bClearKeyFlags)
{
	m_arrowMovedInComposition = false;
	if (bClearKeyFlags)
		m_pKeyEventSink->clear();
	if (m_pCandidateDlg)
		m_pCandidateDlg->Reset();
	onlyHideCandidateDlg();
	// 组串生命周期是显式的：调用方必须在关闭候选 UI 前 Commit 或 Cancel。
	// 本函数不得隐式结束组串。
	contextInputState().compositionEnded = true;
	contextInputState().candidatePresentationRequested = false;
	contextInputState().layoutChangeIndex = 0;
	endCandidateUIElement();
}

void CTextService::onlyHideCandidateDlg() {
	contextInputState().candidatePresentationRequested = false;
	if (auto* state = activeContextState())
	{
		auto snapshot = state->candidates();
		++snapshot.version;
		snapshot.visible = false;
		state->setCandidates(std::move(snapshot));
	}
	publishCandidateSnapshot();
	if (m_pCandidateDlg) {
		m_pCandidateDlg->showCandidateDlg(false);
	}
}

void CTextService::onlyShowCandidateDlg() {
	contextInputState().candidatePresentationRequested = true;
	if (auto* state = activeContextState())
	{
		auto snapshot = state->candidates();
		++snapshot.version;
		snapshot.visible = true;
		state->setCandidates(std::move(snapshot));
	}
	publishCandidateSnapshot();
	if (m_pCandidateDlg) {
		m_pCandidateDlg->showCandidateDlg(true);
	}
}

void  CTextService::resetCandidateDlg() {
	if (m_pCandidateDlg) {
		m_pCandidateDlg->Reset();
	}
}

bool CTextService::isCandidateVisiable() const
{
	const auto* state = activeContextState();
	return (state && (state->inputState().candidatePresentationRequested ||
		state->candidates().visible)) ||
		(m_pCandidateDlg && m_pCandidateDlg->isVisible());
}

bool CTextService::isCandidateVisibleForContext(ITfContext* context) const
{
	const auto* state = m_tsfThreadContext.contexts().find(context);
	return state && (state->inputState().candidatePresentationRequested ||
		state->candidates().visible || state->candidateController().isVisible());
}

bool CTextService::isMouseSelected()
{
	return m_pCandidateDlg && m_pCandidateDlg->isMouseSelected();
}

HWND CTextService::getFocusWindow() const
{
	HWND w = NULL;
	CComPtr<ITfDocumentMgr> pDocumentMgr;
	CComPtr<ITfContext> pContext;
	CComPtr<ITfContextView> pContextView;
	if (m_pThreadMgr != NULL
		&& SUCCEEDED(m_pThreadMgr->GetFocus(&pDocumentMgr)) && pDocumentMgr
		&& SUCCEEDED(pDocumentMgr->GetTop(&pContext)) && pContext
		&& SUCCEEDED(pContext->GetActiveView(&pContextView)))
	{
		pContextView->GetWnd(&w);
	}
	return w;
}

void CTextService::createCandidateDlg()
{
	if (!m_pCandidateDlg)
		return;
	m_pCandidateDlg->setTextService(this);
	m_pCandidateDlg->setConfigSnapshot(m_configSnapshot);
	if (const auto session = activeEngineSession())
		m_pCandidateDlg->set_candidate_provider(
			session_bridge_, session, m_runtimeMode.traditional());
}

bool CTextService::isMouseSelectedForContext(ITfContext* context) const
{
	const auto* state = m_tsfThreadContext.contexts().find(context);
	return state && state->candidateController().isMouseSelected();
}

void CTextService::destroyCandidateDlg()
{
	if (m_pCandidateDlg)
		m_pCandidateDlg->Reset();
}

void CTextService::showFloatingToolbar(BOOL show)
{
	// 焦点可能在没有按键事件的情况下恢复。先刷新再发布，使工具栏
	// 使用本 TSF 宿主失活期间更改过的设置。
	if (show)
		refreshConfigSnapshot();
	m_remoteToolbarVisible = show && m_configSnapshot && m_configSnapshot->floatingToolbarOpen;
	publishToolbarSnapshot(m_remoteToolbarVisible);
}

void CTextService::destroyFloatingToolbar()
{
	m_remoteToolbarVisible = false;
	publishToolbarSnapshot(false);
}

bool CTextService::isFloatingToolbarVisible() const
{
	return m_remoteToolbarVisible;
}

void CTextService::handleFloatingToolbarClick(int buttonId)
{
	switch (buttonId)
	{
	case Ascii:
		if (m_pKeyEventSink->isCapsLockOn())
		{
			changeCapsLock();
		}
		else
		{
			m_pLanguageProfile->updateAsciiMode();
			// 中英与标点联动：不存在“英语+中文标点”组合
			bool isEnglishMode = m_pLanguageProfile->getAsciiMode();
			m_pCompartmentEventSink->updateChineseMode(!isEnglishMode);
			if (isEnglishMode)
				m_pCompartmentEventSink->updateChinesePunctuationMode(FALSE);
			else
			{
				const int punctuationMode = contextInputState().punctuationLanguageMode;
				bool useChinesePunct = (punctuationMode == 2)
					|| (punctuationMode == 0 && !m_configSnapshot->useEnglishPunctuation);
				m_pCompartmentEventSink->updateChinesePunctuationMode(useChinesePunct);
			}
		}
		break;
	case Punc:
		changePunctuationLanguageMode();
		break;
	case FullHalf:
		changeFullHalfWidth();
		break;
	case Charset:
		changeCharacterSimOrTra();
		break;

	default:
		break;
	}
}

void CTextService::initFloatingToolbarIcon()
{
	publishToolbarSnapshot(m_remoteToolbarVisible);
}

void CTextService::changeFloatingToolbarPuncIcon()
{
	publishToolbarSnapshot(m_remoteToolbarVisible);
}

void CTextService::changeFloatingToolbarCharsetIcon()
{
	publishToolbarSnapshot(m_remoteToolbarVisible);
}

void CTextService::resetInput()
{
	if (m_pEditMgr)
	{
		m_pEditMgr->getCandInput().clear();
		m_pEditMgr->getAllInput().Empty();
	}
}

void CTextService::showLanguageBar()
{
	m_pLanguageProfile->updateIcon();
}

void CTextService::resetQuoteState()
{
	contextInputState().preQuoteSingle = false;
	contextInputState().preQuoteDouble = false;
}

void CTextService::syncCandidateUI(const coral::tsf::CandidateStateModel& model)
{
	if (auto* state = activeContextState())
	{
		coral::tsf::CandidateSnapshot snapshot = state->candidates();
		++snapshot.version;
		snapshot.candidates.clear();
		snapshot.candidateIds.clear();
		for (const auto& item : model.items())
		{
			snapshot.candidates.emplace_back(item.text.c_str());
			snapshot.candidateIds.push_back(item.id);
		}
		snapshot.pageSize = static_cast<UINT>(model.pageSize());
		snapshot.selection = static_cast<UINT>(model.selectedIndex());
		snapshot.expanded = model.expanded();
		snapshot.hasMore = model.hasMore();
		state->setCandidates(std::move(snapshot));
		state->candidateUIElement().syncFrom(state->candidates());
	}
	publishCandidateSnapshot();
}

void CTextService::setCandidatePresentation(bool expanded, bool hasMore)
{
	if (auto* state = activeContextState())
	{
		auto snapshot = state->candidates();
		++snapshot.version;
		snapshot.expanded = expanded;
		snapshot.hasMore = hasMore;
		state->setCandidates(std::move(snapshot));
	}
	publishCandidateSnapshot();
}

bool CTextService::hasThreadFocus() const
{
	BOOL focused = TRUE;
	return !m_pThreadMgr || FAILED(m_pThreadMgr->IsThreadFocus(&focused)) || focused;
}

void CTextService::publishCandidateSnapshot()
{
	if (!m_configSnapshot) return;
	if (auto* state = activeContextState())
	{
		const auto& snapshot = state->candidates();
		// 渲染器被所有 TSF 线程共享；没有键盘焦点的线程
		// 绝不能认领它，无论其上下文状态如何。
		m_rendererCoordinator.publishCandidate(
			*state, snapshot, snapshot.anchor,
			snapshot.visible && hasThreadFocus() &&
			!state->candidateController().getUiLessMode() &&
			(!state->candidateUIElement().isBegun() ||
				state->candidateUIElement().isShown()),
			*m_configSnapshot);
	}
}

void CTextService::republishFocusedCandidate()
{
	CComPtr<ITfDocumentMgr> document;
	CComPtr<ITfContext> context;
	if (!m_pThreadMgr || FAILED(m_pThreadMgr->GetFocus(&document)) || !document ||
		FAILED(document->GetTop(&context)) || !context)
		return;
	if (FAILED(m_tsfThreadContext.contexts().setFocused(context)) ||
		FAILED(selectContextController(context)))
		return;
	publishCandidateSnapshot();
}

void CTextService::onCandidateUIElementVisibilityChanged(
	CCandidateListUIElement* element, bool show)
{
	if (auto* state = activeContextState(); state &&
		&state->candidateUIElement() == element)
	{
		auto& controller = state->candidateController();
		controller.setUiLessMode(!element->allowsServerWindow());
		controller.showCandidateDlg(show && state->candidates().visible);
		publishCandidateSnapshot();
	}
}

void CTextService::publishToolbarSnapshot(bool visible)
{
	if (!m_configSnapshot) return;
	BOOL threadFocused = FALSE;
	if (visible && (!m_pThreadMgr || FAILED(m_pThreadMgr->IsThreadFocus(&threadFocused)) ||
		!threadFocused)) visible = false;
	const auto* state = m_tsfThreadContext.contexts().focused();
	m_rendererCoordinator.publishToolbar(
		state, visible, m_pLanguageProfile->getAsciiMode(),
		m_pKeyEventSink->isCapsLockOn(), m_runtimeMode.full_width(),
		isUsingEnglishPunctuation(), m_runtimeMode.traditional(), *m_configSnapshot);
}

void CTextService::beginCandidateUIElement()
{
	if (!m_pCandidateDlg)
		return;
	BOOL isAllowShow = 0;
	m_pCandidateListUIElement->beginUI(isAllowShow);
	m_pCandidateDlg->setUiLessMode(!isAllowShow);
	m_pCandidateDlg->showCandidateDlg(
		activeContextState() && activeContextState()->candidates().visible);
	// 当 TSF 拒绝渲染该元素时，由宿主接管其呈现。
	// 在 Begin 之后立即发送已填充好的元素快照，使宿主
	// 无需等待下一次编辑即可绘制首个候选列表。
	if (!isAllowShow)
		m_pCandidateListUIElement->updateUI();
}

void CTextService::updateCandidateUIElement()
{
	if (!m_pCandidateDlg) return;
	if (!m_pCandidateDlg->getUiLessMode()) return;
	m_pCandidateListUIElement->updateUI();
}

void CTextService::endCandidateUIElement()
{
	auto* state = activeContextState();
	if (!state)
		return;
	auto snapshot = state->candidates();
	++snapshot.version;
	snapshot.candidates.clear();
	snapshot.candidateIds.clear();
	snapshot.selection = 0;
	state->setCandidates(std::move(snapshot));
	state->candidateUIElement().syncFrom(state->candidates());
	publishCandidateSnapshot();
	state->candidateUIElement().endUI();
}

void CTextService::updateCompartmentString(const CString& str)
{
	if (m_pEditMgr)
	{
		if (m_pEditMgr->exsitComposition())
			m_pEditMgr->updateComposition(m_clientId, str);
	}
}

BOOL CTextService::isUserTypingAndIMEParsing() const
{
	if (isCandidateVisiable()) {
		return TRUE;
	}
	return FALSE;
}

void CTextService::changeFullHalfWidth()
{
	bool newValue = !m_runtimeMode.full_width();
	m_runtimeMode.setFullWidth(newValue);
	// 迁移期保留旧 UI 状态镜像；引擎只通过 SessionPort 接收配置。
	syncEngineConfig();
	initFloatingToolbarIcon();
	m_pCompartmentEventSink->updateFullShapeMode(isFullWidth());
}

void CTextService::changeCharacterSimOrTra()
{
	bool newValue = !m_runtimeMode.traditional();
	m_runtimeMode.setTraditional(newValue);
	syncEngineConfig();
	if (const auto session = activeEngineSession())
		m_pCandidateDlg->set_candidate_provider(session_bridge_, session, newValue);
	changeFloatingToolbarCharsetIcon();
	if (isCandidateVisiable() && m_pCandidateDlg)
	{
		m_pCandidateDlg->refreshCandidateContent();
	}
}

void CTextService::changePunctuationLanguageMode()
{
	// punctuationLanguageMode: 0表示跟随默认设置，1表示英文标点，2表示中文标点
	auto& punctuationLanguageMode = contextInputState().punctuationLanguageMode;
	if (m_pLanguageProfile->getAsciiMode())
	{
		// 英文模式下不允许“英语+中文标点”组合：切到中文标点时自动切回中文模式
		punctuationLanguageMode = 2;
		m_pLanguageProfile->updateAsciiMode();
		m_pCompartmentEventSink->updateChineseMode(TRUE);
		m_pCompartmentEventSink->updateChinesePunctuationMode(TRUE);
		initFloatingToolbarIcon();
		return;
	}

	if (punctuationLanguageMode == 0)
	{
		punctuationLanguageMode = m_configSnapshot->useEnglishPunctuation ? 2 : 1;
	}
	else
	{
		punctuationLanguageMode = (punctuationLanguageMode == 1) ? 2 : 1;
	}

	m_pCompartmentEventSink->updateChinesePunctuationMode(punctuationLanguageMode == 2);
	initFloatingToolbarIcon();
}

CString CTextService::CandidateDlgRc2Str(CRect& rc) const
{
	CString rcStr;
	rcStr.Format(_T("%d|%d|%d|%d"), rc.left, rc.top, rc.right, rc.bottom);
	return rcStr;
}

bool CTextService::isFullWidth() const
{
	return m_runtimeMode.full_width();
}

bool CTextService::setTestInputState(bool isChinese, bool isFullWidth, bool isChinesePunct)
{
	if (!m_pLanguageProfile)
		return false;

	// 1. 设置中英文模式
	bool currentChinese = !m_pLanguageProfile->getAsciiMode(); // ASCII模式=英文，所以取反
	if (currentChinese != isChinese)
	{
		m_pLanguageProfile->updateAsciiMode();
	}

	// 2. 设置全半角
	bool currentFullWidth = m_runtimeMode.full_width();
	if (currentFullWidth != isFullWidth)
	{
		changeFullHalfWidth();
	}

	// 3. 设置标点模式
	bool currentChinesePunct = false;
	if (!m_pLanguageProfile->getAsciiMode() && !m_pKeyEventSink->isCapsLockOn())
	{
		const int punctuationMode = contextInputState().punctuationLanguageMode;
		currentChinesePunct = (punctuationMode == 0) ? m_configSnapshot->useEnglishPunctuation : (punctuationMode == 1);
		// 得到的为是否为英文，手动取反
		currentChinesePunct = !currentChinesePunct;
	}
	if (currentChinesePunct != isChinesePunct)
	{
		changePunctuationLanguageMode();
	}

	return true;
}

bool CTextService::getTestInputState(bool* isChinese, bool* isFullWidth, bool* isChinesePunct)
{
	if (!isChinese || !isFullWidth || !isChinesePunct)
		return false;

	if (!m_pLanguageProfile)
		return false;

	*isChinese = !m_pLanguageProfile->getAsciiMode();
	*isFullWidth = m_runtimeMode.full_width();
	*isChinesePunct = !isUsingEnglishPunctuation();
	return true;
}

bool CTextService::isDefaultEnglishMode() const
{
	return m_configSnapshot && m_configSnapshot->defaultEnglish;
}

bool CTextService::isUsingEnglishPunctuation() const
{
	if (m_pLanguageProfile->getAsciiMode() || m_pKeyEventSink->isCapsLockOn())
		return true;

	// 中文模式下，根据当前 Context 的 punctuationLanguageMode 判断
	const int punctuationMode = contextInputState().punctuationLanguageMode;
	if (punctuationMode == 0)
		return m_configSnapshot && m_configSnapshot->useEnglishPunctuation;
	else
		return (punctuationMode == 1);
}

bool CTextService::isPunctuation(WPARAM wParam) const
{
	return isPunctuation(wParam, m_pKeyEventSink->isShiftDown());
}

bool CTextService::isPunctuation(WPARAM wParam, bool shiftDown) const
{
	switch (wParam)
	{
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
	case 0x34:
	case 0x35:
	case 0x36:
	case 0x37:
	case 0x38:
	case 0x39:
		if (shiftDown) return true;
		else return false;
	case VK_OEM_1:     // ;:
	case VK_OEM_2:     // /?
	case VK_OEM_3:     // `~
	case VK_OEM_4:     // [{
	case VK_OEM_5:     // \|
	case VK_OEM_6:     // ]}
	case VK_OEM_7:     // '"
	case VK_OEM_COMMA: // ,<
	case VK_OEM_PERIOD:// .>
	case VK_OEM_MINUS: // -_
	case VK_OEM_PLUS: // -_
	case VK_ADD:
	case VK_SUBTRACT:
	case VK_MULTIPLY:
	case VK_DIVIDE:
	case VK_DECIMAL:
		return true;
	default:
		return false;
	}
}

STDMETHODIMP CTextService::Show(HWND hwndParent, LANGID langid, REFGUID rguidProfile)
{
	Coreapi::showSettingMenu();
	return S_OK;
}

void CTextService::openSettingsMenuFromUi(const RECT* anchorRect)
{
	RECT anchor{};
	if (anchorRect && anchorRect->right > anchorRect->left)
		anchor = *anchorRect;
	else
	{
		POINT cursor{};
		::GetCursorPos(&cursor);
		anchor = { cursor.x, cursor.y, cursor.x + 1, cursor.y + 1 };
	}
	m_settingMenu.toggle(anchor);
}

void CTextService::refreshFloatingToolbarPreference()
{
	refreshConfigSnapshot(true);
	showFloatingToolbar(sConfig->getIsFloatingToolbarOpen() ? TRUE : FALSE);
}

STDMETHODIMP CTextService::GetDisplayName(BSTR* pbstrName)
{
	if (!pbstrName) return E_POINTER;
	*pbstrName = SysAllocString(L"CORAL IME Settings");
	return (*pbstrName) ? S_OK : E_OUTOFMEMORY;
}

// ITfDisplayAttributeProvider
STDMETHODIMP CTextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum)
{
	if (!ppEnum) return E_INVALIDARG;
	*ppEnum = nullptr;
	if (!m_pDisplayAttrInfo) return E_FAIL;

	CComObject<CEnumDisplayAttributeInfo>* pEnum = nullptr;
	HRESULT hr = CComObject<CEnumDisplayAttributeInfo>::CreateInstance(&pEnum);
	if (FAILED(hr)) return hr;
	pEnum->AddRef();
	pEnum->Init(m_pDisplayAttrInfo);
	*ppEnum = pEnum;
	return S_OK;
}

STDMETHODIMP CTextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo)
{
	if (!ppInfo) return E_INVALIDARG;
	*ppInfo = nullptr;
	if (!IsEqualGUID(guid, GUID_CoralPinyin_DisplayAttr)) return E_INVALIDARG;
	if (!m_pDisplayAttrInfo) return E_FAIL;

	m_pDisplayAttrInfo.CopyTo(ppInfo);
	return S_OK;
}

bool CTextService::isVMode() const
{
	if (m_pCandidateDlg->getInputMode() == InputMode::IM_VMode && m_configSnapshot->v_mode) {
		return true;
	}
	return false;
}

HRESULT CTextService::handlePadChar(const WPARAM wParam)
{
	if (isCandidateVisiable())
	{
		switch (wParam)
		{
		case VK_MULTIPLY:       // *
			handleCharacterKey(_T('*'));
			break;
		case VK_ADD:            // +
			handleCharacterKey(_T('+'));
			break;
		case VK_SUBTRACT:       // -
			handleCharacterKey(_T('-'));
			break;
		case VK_DIVIDE:         // /
			handleCharacterKey(_T('/'));
			break;
		case VK_DECIMAL:        // . 先不当成句号处理
			handleCharacterKey(_T('.'));
			break;
		default:
			break;
		}
	}
	return S_OK;
}

CString CTextService::getContent(int col) const
{
	CString returnValue;
	m_pCandidateDlg->selectCurrentCandidate(returnValue);
	return returnValue;
}

HRESULT CTextService::finishCandidateSelection(
	int selectionResult, const CString& text)
{
	if (selectionResult != 1)
		return S_OK; // 部分上屏：剩余组串保持活动。

	const bool verbatim = m_pCandidateDlg && m_pCandidateDlg->takeVerbatimCommit();
	if (m_pEditMgr)
		m_pEditMgr->insertText(m_clientId, isFullWidth() && !verbatim ? hlp::HalfToFull(text) : text);
	hideCandidateDlg(TRUE);
	return S_OK;
}

HRESULT CTextService::commitCurrentCandidate()
{
	if (!isCandidateVisiable())
		return S_OK;
	CString text;
	const int result = m_pCandidateDlg->selectCurrentCandidate(text);
	return finishCandidateSelection(result, text);
}

HRESULT CTextService::commitCandidateByIndex(int candidateIndex)
{
	if (!isCandidateVisiable())
		return S_OK;
	CString text;
	const int result = m_pCandidateDlg->selectCandidateByIndex(text, candidateIndex);
	return finishCandidateSelection(result, text);
}

HRESULT CTextService::commitCandidateByVisualColumn(int visualColumn)
{
	if (!isCandidateVisiable())
		return S_OK;
	CString text;
	const int result = m_pCandidateDlg->selectCol(text, visualColumn);
	return finishCandidateSelection(result, text);
}

void CTextService::resetConditionWord() {
	if (m_pCandidateDlg)
		m_pCandidateDlg->resetConditionWord();
	LOG_TRACE("Reset condition word.");
}

void CTextService::selectCandidateByIndex(int index)
{
	if (isCandidateVisiable())
	{
		commitCandidateByVisualColumn(index);
	}
}

void CTextService::selectCharacterFromWord(int characterIndex)
{
	if (isCandidateVisiable() && (m_pCandidateDlg->isClipboardMode() || m_pCandidateDlg->is_translation_mode()))
	{
        // 剪贴板翻页只由已配置的翻页键决策处理。
		return;
	}
	if (isCandidateVisiable())
	{
		CString currentWord;
		int res = m_pCandidateDlg->selectCurrentCandidate(currentWord);
		if (res == 1)
			m_pCandidateDlg->adjustTodayWord(1 - currentWord.GetLength());
		else
			m_pCandidateDlg->adjustTodayWord(1);
		if (currentWord.GetLength() > 1)
		{
			int actualIndex = characterIndex;
			if (characterIndex == -1)
			{
				actualIndex = currentWord.GetLength() - 1;
			}

			if (actualIndex >= 0 && actualIndex < currentWord.GetLength())
			{
				CString selectedChar = currentWord.Mid(actualIndex, 1);
				if (m_pEditMgr)
				{
					m_pEditMgr->insertText(m_clientId, isFullWidth() ? hlp::HalfToFull(selectedChar) : selectedChar);
				}
				hideCandidateDlg();
			}
		}
		else if (currentWord.GetLength() == 1)
		{
			if (m_pEditMgr)
			{
				m_pEditMgr->insertText(m_clientId, isFullWidth() ? hlp::HalfToFull(currentWord) : currentWord);
			}
			hideCandidateDlg();
		}
	}
}

void CTextService::showCPinyinModelInitErrorDialog() const
{
	static std::atomic_flag shown = ATOMIC_FLAG_INIT;
	if (!shown.test_and_set(std::memory_order_relaxed)) {
		Coreapi::showErrorDialog(hlp::_tr(IDS_CORALPINYIN_TITLE), hlp::_tr(IDS_CPINYINMODEL_LOAD_ERROR), m_processName);
	}
}

void CTextService::applyCandidateLayout(
	const std::vector<std::uint32_t>& rowStarts,
	const std::vector<std::uint32_t>& itemCenters)
{
	if (m_pCandidateDlg)
		m_pCandidateDlg->setVisualLayout(rowStarts, itemCenters);
}

void CTextService::requestCandidatePrefetch()
{
	if (m_pCandidateDlg)
		m_pCandidateDlg->prefetchCandidates(true);
}

void CTextService::toggleCandidateExpansion()
{
	if (!m_pCandidateDlg) return;
	if (m_pCandidateDlg->isCandidateExpanded())
		m_pCandidateDlg->collapseCandidateExpand();
	else
		m_pCandidateDlg->enterCandidateExpand();
}

void CTextService::pinCandidateByIndex(int candidateIndex)
{
	if (m_pCandidateDlg)
		m_pCandidateDlg->applyCandidateActionByIndex(candidateIndex, coral::CandidateAction::PinFirst);
}

void CTextService::deleteCandidateByIndex(int candidateIndex)
{
	if (m_pCandidateDlg)
		m_pCandidateDlg->applyCandidateActionByIndex(candidateIndex, coral::CandidateAction::Disable);
}

void CTextService::showAddPhraseByIndex(int candidateIndex)
{
	if (m_pCandidateDlg)
		m_pCandidateDlg->showAddPhraseByIndex(candidateIndex);
}

void CTextService::endEditSession()
{
	if (m_pEditMgr)
		m_pEditMgr->cancel(m_clientId);
}

void CTextService::changeCapsLock()
{
	keybd_event(VK_CAPITAL, 0, 0, 0);
	keybd_event(VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0);
}

void CTextService::updateEditSessionAccumulatedPinyin(const CString& pinyinHolder)
{
	if (m_pEditMgr)
		m_pEditMgr->updateAccumulatedPinyin(pinyinHolder, m_clientId);
}

int CTextService::isNeedFilter(const CString& text) const
{
	return m_pEditMgr->isNeedFilter(text);
}

CString& CTextService::getAllInput()
{
	return m_pEditMgr->getAllInput();
}

std::vector<CString>& CTextService::getCandInput()
{
	return m_pEditMgr->getCandInput();
}

BOOL CTextService::exsitComposition() const
{
	if (m_pEditMgr)
		return m_pEditMgr->exsitComposition();
	return FALSE;
}

ITfComposition* CTextService::getComposition() const
{
	if (m_pEditMgr)
		return m_pEditMgr->getComposition();
	return NULL;
}

HRESULT CTextService::layoutChangeGetTextBound(CRect& rc)
{
	HRESULT res = getTextBound(rc);
	if (res == S_OK) {
		contextInputState().layoutChanged = true;
	}
	return res;
}

HRESULT CTextService::getTextBound(CRect& rc)
{
	if (m_pEditMgr)
		return m_pEditMgr->getTextBound(m_clientId, rc);
	return S_FALSE;
}

ITfContext* CTextService::getContext() const
{
	if (m_pEditMgr)
		return m_pEditMgr->getContext();
	return NULL;
}

CEditMgr* CTextService::getEditMgr() const
{
	return m_pEditMgr;
}

HRESULT CTextService::setCursorPos(int pos)
{
	if (m_pEditMgr)
		return m_pEditMgr->setCursorPos(m_clientId, pos);
	return S_FALSE;
}

void CTextService::setKeepPlaceholder(bool keepPlaceholder)
{
	if (m_pEditMgr)
	{
		m_pEditMgr->setKeepPlaceholder(keepPlaceholder);
	}
}

void CTextService::setIsEngPuncMode(bool isEngPuncMode)
{
	contextInputState().englishPunctuationMode = isEngPuncMode;
}

bool CTextService::getIsEngPuncMode() const
{
	return contextInputState().englishPunctuationMode;
}

bool CTextService::needSendLeftKey() const
{
	return contextInputState().needSendLeftKey;
}

void CTextService::setNeedSendLeftKey(bool need)
{
	contextInputState().needSendLeftKey = need;
}

bool CTextService::handle_symbol_conversion(WCHAR digitChar)
{
	// 如果功能未开启，直接返回
	if (!m_configSnapshot->autoConvertDigitSymbol && !m_configSnapshot->auto_convert_letter_symbol)
		return false;
	// 获取最后2个输入字符
	if (!m_pEditMgr)
		return false;

	const CString& lastTwoChars = m_pEditMgr->getLastTwoChars();
	// 检查是否有两个历史字符
	if (lastTwoChars.GetLength() < 2)
		return false;
	WCHAR lastChar = lastTwoChars.GetAt(1);  // 最后一个字符（中文符号）
	WCHAR secondLastChar = lastTwoChars.GetAt(0);  // 倒数第二个字符（数字或字母）
	const WCHAR englishSymbol = coral::tsf::separator_replacement(
		m_configSnapshot->autoConvertDigitSymbol, m_configSnapshot->auto_convert_letter_symbol,
		secondLastChar, lastChar, digitChar);
	if (!englishSymbol)
		return false;
	// 单次 SendInput 原子注入：退格删中文符号 + Unicode 注入英文符号和后续字符
	// 批次内事件不会被其他进程输入插队
	INPUT allInputs[6] = {};
	// [0][1] 退格 down/up
	allInputs[0].type = INPUT_KEYBOARD;
	allInputs[0].ki.wVk = VK_BACK;
	allInputs[1].type = INPUT_KEYBOARD;
	allInputs[1].ki.wVk = VK_BACK;
	allInputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
	// [2][3] 英文符号 down/up
	allInputs[2].type = INPUT_KEYBOARD;
	allInputs[2].ki.wScan = englishSymbol;
	allInputs[2].ki.dwFlags = KEYEVENTF_UNICODE;
	allInputs[3].type = INPUT_KEYBOARD;
	allInputs[3].ki.wScan = englishSymbol;
	allInputs[3].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
	// [4][5] 后续字符 down/up
	allInputs[4].type = INPUT_KEYBOARD;
	allInputs[4].ki.wScan = digitChar;
	allInputs[4].ki.dwFlags = KEYEVENTF_UNICODE;
	allInputs[5].type = INPUT_KEYBOARD;
	allInputs[5].ki.wScan = digitChar;
	allInputs[5].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
	SendInput(6, allInputs, sizeof(INPUT));

	return true;
}

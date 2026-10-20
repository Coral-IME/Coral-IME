#include "pch.h"
#include "keyeventsink.h"
#include "textservice.h"
#include "editsession.h"
#include <src/coreapi.h>
#include "preference_registry.h"
#include "keydecision.h"
#include "input_timing.h"

namespace
{
// 注册、派发与注销共用的命令标识。
constexpr GUID kCtrlSpaceCommand{ 0x961d648e, 0x203b, 0x4d78, { 0x9e, 0x97, 0x71, 0xd0, 0xd3, 0x75, 0xf6, 0xe2 } };
constexpr TF_PRESERVEDKEY kCtrlSpaceKey{ VK_SPACE, TF_MOD_CONTROL };

struct ModifierSnapshot
{
	bool shift = false;
	bool ctrl = false;
	bool alt = false;
	bool win = false;
	bool capsLock = false;
};

ModifierSnapshot CaptureModifiers(WPARAM eventKey, bool eventKeyDown)
{
	BYTE keys[256] = {};
	::GetKeyboardState(keys);
	auto down = [&keys](int key) { return (keys[key] & 0x80) != 0; };
	ModifierSnapshot result;
	result.shift = down(VK_SHIFT) || down(VK_LSHIFT) || down(VK_RSHIFT);
	result.ctrl = down(VK_CONTROL) || down(VK_LCONTROL) || down(VK_RCONTROL);
	result.alt = down(VK_MENU) || down(VK_LMENU) || down(VK_RMENU);
	result.win = down(VK_LWIN) || down(VK_RWIN);
	result.capsLock = (::GetKeyState(VK_CAPITAL) & 1) != 0;

	if (eventKey == VK_SHIFT || eventKey == VK_LSHIFT || eventKey == VK_RSHIFT)
		result.shift = eventKeyDown;
	else if (eventKey == VK_CONTROL || eventKey == VK_LCONTROL || eventKey == VK_RCONTROL)
		result.ctrl = eventKeyDown;
	else if (eventKey == VK_MENU || eventKey == VK_LMENU || eventKey == VK_RMENU)
		result.alt = eventKeyDown;
	else if (eventKey == VK_LWIN || eventKey == VK_RWIN)
		result.win = eventKeyDown;
	return result;
}

coral::tsf::ShortcutContextSnapshot CaptureShortcutContext(
	CTextService* service, ITfContext* context, const coral::tsf::KeyEventSnapshot& event)
{
	const auto& config = service->configSnapshot();
	const bool configuredCtrlShortcut = config.simplifiedTraditionalShortcut &&
		event.ctrl() && event.shift() &&
		((config.simplifiedTraditionalKey == "F" && event.virtualKey() == 'F') ||
		 (config.simplifiedTraditionalKey == "G" && event.virtualKey() == 'G'));
	return {
		service->isCandidateVisibleForContext(context),
		service->isMouseSelectedForContext(context),
		configuredCtrlShortcut,
		config.ctrlSpaceShortcut,
	};
}
}

STDAPI CKeyEventSink::OnSetFocus(BOOL foreground)
{
	if (!foreground) {
        keyState().ctrlSpaceHeld = false;
        m_pTextService->resetConditionWord();
    }
	// 线程焦点负责处理键盘焦点与候选 UI 的实际丢失。
	if (m_pTextService->getEditMgr())
		m_pTextService->getEditMgr()->setLastTwoChars(L"");
	return S_OK;
}

STDAPI CKeyEventSink::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
	if (!pfEaten)
		return E_INVALIDARG;
	*pfEaten = FALSE;
	const ModifierSnapshot modifiers = CaptureModifiers(wParam, true);
	const coral::tsf::KeyEventSnapshot event(
		static_cast<std::uint32_t>(wParam), true, modifiers.shift, modifiers.ctrl,
		modifiers.alt, modifiers.win, modifiers.capsLock);
	const auto decision = evaluateKeyDecision(pic, event);
	rememberTestDecision(pic, decision);
	const auto& config = m_pTextService->configSnapshot();
	*pfEaten = (coral::tsf::ShouldCaptureModeSwitchKey(
		event, config.shiftShortcut, config.ctrlShortcut) ||
		decision.shortcut.consumed() ||
		decision.handling.handleType != KeyHandleType::Nothing) ? TRUE : FALSE;
	return S_OK;
}

STDAPI CKeyEventSink::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
	activateContext(pic);
	auto& state = keyState();
	state.handledMultiKey = false;
	testPreservedKeyStatus(); // 重新获取特殊按键的状态

	modifyKeyStatusSwitch(wParam, true);
	if (isShiftDown() || isCtrlDown())
		state.shiftOrCtrlDown = true;
	const coral::tsf::KeyEventSnapshot event(
		static_cast<std::uint32_t>(wParam), true, isShiftDown(), isCtrlDown(),
		isAltDown(), isWinDown(), isCapsLockOn());
	const auto decision = consumeTestDecisionOrEvaluate(pic, event);
    // 编辑、标点、快捷键及原样输出打断连续中文；候选选择键不打断。
    const bool modifier = wParam == VK_SHIFT || wParam == VK_CONTROL || wParam == VK_MENU ||
        wParam == VK_LSHIFT || wParam == VK_RSHIFT || wParam == VK_LCONTROL || wParam == VK_RCONTROL;
    const bool break_prediction = wParam == VK_ESCAPE || wParam == VK_RETURN ||
        (!m_pTextService->isCandidateVisibleForContext(pic) && !modifier &&
            ((wParam < 'A' || wParam > 'Z') || event.ctrl() || event.alt() || event.win())) ||
        (wParam >= VK_OEM_1 && wParam <= VK_OEM_3) || wParam == VK_OEM_COMMA ||
        wParam == VK_OEM_PERIOD || wParam == VK_OEM_5;
    struct PredictionBoundary {
        CTextService* service;
        bool clear;
        ~PredictionBoundary() { if (clear) service->resetConditionWord(); }
    } boundary{m_pTextService, break_prediction};
    if (break_prediction) m_pTextService->resetConditionWord();
	if (decision.shortcut.consumed())
	{
		switch (decision.shortcut.command)
		{
		case coral::tsf::ShortcutCommand::ToggleLanguageMode:
			handleCtrlSpace(pic);
			break;
		case coral::tsf::ShortcutCommand::ToggleFullWidth:
			changeFullHalfWidthDown(pic, wParam);
			break;
		case coral::tsf::ShortcutCommand::CloseComposition:
			m_pTextService->hideCandidateDlg(TRUE);
			break;
		case coral::tsf::ShortcutCommand::TogglePunctuation:
			changePunctuationLanguageModeDown(pic, wParam);
			break;
		default:
			break;
		}
		*pfEaten = TRUE;
		return S_OK;
	}

	const auto handleType = decision.handling.handleType;
	m_pTextService->executeKeyDecision(decision.handling);
	if (decision.handling.candidateSelectionEffect != coral::tsf::CandidateSelectionEffect::None)
	{
		*pfEaten = TRUE;
		return S_OK;
	}
	if (handleType == KeyHandleType::Normal)
	{
		const auto sessionStart = coral::input_timing::nowUs();
		const HRESULT sessionHr = m_pTextService->ensureEngineSessionForKey(pic);
		coral::input_timing::mark(L"tsf.key_ensure_session", sessionStart, wParam, static_cast<unsigned long>(sessionHr));
		if (FAILED(sessionHr))
		{
			*pfEaten = FALSE;
			return S_OK;
		}
		const auto compositionStart = coral::input_timing::nowUs();
		m_pTextService->startComposition(pic, wParam, true);
		coral::input_timing::mark(L"tsf.start_composition", compositionStart, wParam);
		const auto handlerStart = coral::input_timing::nowUs();
		m_pTextService->invokeKeyHandler(pic, wParam, lParam);
		coral::input_timing::mark(L"tsf.invoke_key_handler", handlerStart, wParam);
		*pfEaten = true;
		return S_OK;
	}
	else if (handleType == KeyHandleType::Nothing)
	{
		const auto& config = m_pTextService->configSnapshot();
		const bool captureModeSwitchKey = coral::tsf::ShouldCaptureModeSwitchKey(
			event, config.shiftShortcut, config.ctrlShortcut);
		// 记录或清空输入字符历史
		recordInputChar(wParam);

		changeCharacterSimOrTraDown(pic, wParam);
		if (wParam == VK_CAPITAL)
			m_pTextService->showLanguageBar();
		*pfEaten = captureModeSwitchKey ? TRUE : FALSE;
		return captureModeSwitchKey ? S_OK : S_FALSE;
	}
	else
	{
		*pfEaten = true;
		m_pTextService->invokeFullWidthCharactersKeyHandler(pic, wParam, lParam);
		return S_OK;
	}
	return S_FALSE;
}

STDAPI CKeyEventSink::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
	if (!pfEaten)
		return E_INVALIDARG;
	if (const auto* context = m_pTextService->m_tsfThreadContext.contexts().find(pic))
	{
		if (wParam == VK_SPACE && context->keyState().ctrlSpaceHeld)
		{
			*pfEaten = TRUE;
			return S_OK;
		}
	}
	const ModifierSnapshot modifiers = CaptureModifiers(wParam, false);
	const coral::tsf::KeyEventSnapshot event(
		static_cast<std::uint32_t>(wParam), false, modifiers.shift, modifiers.ctrl,
		modifiers.alt, modifiers.win, modifiers.capsLock);
	const auto decision = evaluateKeyDecision(pic, event);
	rememberTestDecision(pic, decision);
	const auto& config = m_pTextService->configSnapshot();
	*pfEaten = (coral::tsf::ShouldCaptureModeSwitchKey(
		event, config.shiftShortcut, config.ctrlShortcut) ||
		decision.shortcut.consumed() ||
		decision.handling.handleType != KeyHandleType::Nothing) ? TRUE : FALSE;
	return S_OK;
}

STDAPI CKeyEventSink::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
	if (!pfEaten) return E_INVALIDARG;
	activateContext(pic);
	auto& state = keyState();
	modifyKeyStatusSwitch(wParam, false);
	if (wParam == VK_SPACE && state.ctrlSpaceHeld)
	{
		state.ctrlSpaceHeld = false;
		state.pendingDecision = {};
		*pfEaten = TRUE;
		return S_OK;
	}
	// 检测 Shift 松开后，如果有标志则发送左键
	if ((wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) && m_pTextService->needSendLeftKey())
	{
		m_pTextService->setNeedSendLeftKey(false);
		m_pTextService->sendLeftKey(pic);
	}
	//只有当没有处理过多个按键组成的快捷键，才处理单按键快捷键
	if (!state.handledMultiKey)
		changeLanguageModeUp(pic, wParam);

	const coral::tsf::KeyEventSnapshot event(
		static_cast<std::uint32_t>(wParam), false, isShiftDown(), isCtrlDown(),
		isAltDown(), isWinDown(), isCapsLockOn());
	const auto decision = consumeTestDecisionOrEvaluate(pic, event);
	const auto& config = m_pTextService->configSnapshot();
	*pfEaten = (coral::tsf::ShouldCaptureModeSwitchKey(
		event, config.shiftShortcut, config.ctrlShortcut) ||
		decision.shortcut.consumed() ||
		decision.handling.handleType != KeyHandleType::Nothing) ? TRUE : FALSE;
	if (wParam == VK_SHIFT || wParam == VK_CONTROL)
		state.shiftOrCtrlDown = false;
	if (wParam == VK_CAPITAL || wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
		m_pTextService->showLanguageBar();
	}
	return S_OK;
}

void CKeyEventSink::changeLanguageModeUp(ITfContext* pic, WPARAM wParam)
{
	auto& state = keyState();
	//if (!isShiftDown()) // 防止特殊情况下会出现一次keyup一次testkeyup等异常情况
	//	return;
	//todo 热键配置
    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT)
    {
        // 当存在输入组合键时，以及按下enter的时候
        // 出现候选框的时候返回true
        // CHECK_ERROR(!CEditSession::exsitComposition());
        // 当存在alt ctrl win按下时，不能切换语言
		if (state.preInput || state.enterDown)
		{
			state.preInput = false;
		}
		else if (areAltWinCtrlNotPressed() && m_pTextService->configSnapshot().shiftShortcut && state.shiftOrCtrlDown)
		{
			m_pTextService->updateLanguageMode(pic, true);
		}
	}else if (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL)
	{
		if (state.preInput)
		{
			state.preInput = false;
		}
		else if (areAltWinShiftNotPressed() && m_pTextService->configSnapshot().ctrlShortcut && state.shiftOrCtrlDown)
		{
			m_pTextService->updateLanguageMode(pic, true);
		}
	}
}

//Shift+Space快捷键改变全半角
void CKeyEventSink::changeFullHalfWidthDown(ITfContext* pic, WPARAM wParam)
{
	if (wParam == VK_SPACE && isShiftDown() && m_pTextService->configSnapshot().shiftSpaceShortcut)
	{
		m_pTextService->changeFullHalfWidth();
		keyState().handledMultiKey = true;
	}
}

void CKeyEventSink::changeCharacterSimOrTraDown(ITfContext* pic, WPARAM wParam)
{
	if (m_pTextService->configSnapshot().simplifiedTraditionalShortcut)
	{
		if ((isShiftDown() && isCtrlDown() && m_pTextService->configSnapshot().simplifiedTraditionalKey == "F" && wParam == 'F') ||
			(isShiftDown() && isCtrlDown() && m_pTextService->configSnapshot().simplifiedTraditionalKey == "G" && wParam == 'G'))
		{
			m_pTextService->changeCharacterSimOrTra();
			keyState().handledMultiKey = true;
		}
	}
}

void CKeyEventSink::changePunctuationLanguageModeDown(ITfContext* pic, WPARAM wParam)
{
	if (wParam == VK_OEM_PERIOD && isCtrlDown() && m_pTextService->configSnapshot().ctrlPeriodShortcut)
	{
		m_pTextService->changePunctuationLanguageMode();
		keyState().handledMultiKey = true;
	}
}

bool CKeyEventSink::handleCtrlSpace(ITfContext* context)
{
	if (!context || !m_pTextService->configSnapshot().ctrlSpaceShortcut)
		return false;
	activateContext(context);
	auto& state = keyState();
	if (!state.ctrlSpaceHeld)
		m_pTextService->updateLanguageMode(context, true); // 与 Shift 相同的模式更新。
	// 上屏/清除可能重置按键状态，因此这些操作要在更新模式之后进行。
	state.ctrlSpaceHeld = true;
	state.handledMultiKey = true;
	state.preInput = true;
	state.shiftOrCtrlDown = false;
	state.pendingDecision = {};
	return true;
}

void CKeyEventSink::updatePreInputStatus(const WPARAM wParam)
{
	if (wParam != VK_SHIFT && wParam != VK_LSHIFT && wParam != VK_RSHIFT)
	{
		if (keyState().preInput)
			setPreInput(true);
		else
			setPreInput(false);
	}
	else if (!isShiftDown())
	{
		setPreInput(false);
	}
}

bool CKeyEventSink::getPreInput()
{
	return keyState().preInput;
}

void CKeyEventSink::setPreInput(bool preInput)
{
	keyState().preInput = preInput;
}

void CKeyEventSink::recordInputChar(WPARAM wParam)
{
	if (!m_pTextService->getEditMgr())
		return;
	
	// 如果是可输出字符，记录到历史
	if (m_pTextService->isOutputChar(wParam))
	{
		// 转换为实际字符
		CString actualText;
		if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
		{
			// 小键盘数字转换
			actualText.Format(L"%c", L'0' + (wParam - VK_NUMPAD0));
		}
		else if (m_pTextService->isPunctuation(wParam))
		{
			// 标点符号获取实际会输入的字符
			actualText = m_pTextService->autoPunctuationTrans(wParam);
		}
		else
		{
			actualText.Format(L"%c", static_cast<WCHAR>(wParam));
		}
		
		// 更新 lastTwoChars（保持最后2个字符）
		const CString& lastTwoChars = m_pTextService->getEditMgr()->getLastTwoChars();
		CString combined = lastTwoChars + actualText;
		if (combined.GetLength() >= 2)
		{
			m_pTextService->getEditMgr()->setLastTwoChars(combined.Right(2));
		}
		else
		{
			m_pTextService->getEditMgr()->setLastTwoChars(combined);
		}
	}
	// 如果不是可输出字符且不是 Shift，清空历史
	else if (wParam != VK_SHIFT && wParam != VK_LSHIFT && wParam != VK_RSHIFT)
	{
		m_pTextService->getEditMgr()->setLastTwoChars(L"");
	}
}

bool CKeyEventSink::getHasHandleMultiKey()
{
	return keyState().handledMultiKey;
}

void CKeyEventSink::setHasHandleMultiKey(bool hasHandleMultiKey)
{
	keyState().handledMultiKey = hasHandleMultiKey;
}

STDAPI CKeyEventSink::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten)
{
	if (!pfEaten) return E_INVALIDARG;
	*pfEaten = FALSE;
	if (IsEqualGUID(rguid, kCtrlSpaceCommand))
		*pfEaten = handleCtrlSpace(pic) ? TRUE : FALSE;
	return S_OK;
}

int CKeyEventSink::getPreservedKeyStatus()
{
	return keyState().modifierMask;
}

CKeyEventSink::CKeyEventSink(CTextService* pTextService)
	: m_pTextService(pTextService)
{

}

CKeyEventSink::~CKeyEventSink()
{
}

void CKeyEventSink::activateContext(ITfContext* context)
{
	if (!context)
	{
		m_activeContextToken = {};
		return;
	}
	if (const auto* state = m_pTextService->m_tsfThreadContext.contexts().find(context))
		m_activeContextToken = state->token();
}

coral::tsf::ContextKeyState& CKeyEventSink::keyState()
{
	if (auto* state = m_pTextService->m_tsfThreadContext.contexts().find(m_activeContextToken))
		return state->keyState();
	return m_detachedKeyState;
}

const coral::tsf::ContextKeyState& CKeyEventSink::keyState() const
{
	if (const auto* state = m_pTextService->m_tsfThreadContext.contexts().find(m_activeContextToken))
		return state->keyState();
	return m_detachedKeyState;
}

BOOL CKeyEventSink::init()
{
	BOOL bRet = FALSE;
	LOG_ERROR_MSG_W(initKeyEventSink(), m_pTextService->getProcessName(), L"CKeyEventSink::init 1");
	LOG_ERROR_MSG_W(initPreservedKey(), m_pTextService->getProcessName(), L"CKeyEventSink::init 2");
	
	bRet = TRUE;
	keyState().shiftOrCtrlDown = false;
Exit0:
	return bRet;
}

void CKeyEventSink::unInit()
{
	// 当输入法有内容并且需要切换输入法的时候，不走keyup的逻辑，但是会走unInit的逻辑，因此在这需要实现enter的效果
	if (auto* state = m_pTextService->m_tsfThreadContext.contexts().find(m_activeContextToken))
		m_pTextService->updateLanguageMode(state->context(), false);
	
	unInitKeyEventSink();
	unInitPreservedKey();
}

BOOL CKeyEventSink::initKeyEventSink()
{
	BOOL bRet = FALSE;
	CComPtr<ITfKeystrokeMgr> pKeyMgr;
	LOG_ERROR_MSG_W(m_pTextService->getThreadMgr()->QueryInterface(&pKeyMgr) == S_OK, m_pTextService->getProcessName(), L"CKeyEventSink::_initKeyEventSink 1");
	LOG_ERROR_MSG_W(pKeyMgr->AdviseKeyEventSink(m_pTextService->getClientId(), (ITfKeyEventSink*)m_pTextService, TRUE) == S_OK, m_pTextService->getProcessName(), L"CKeyEventSink::_initKeyEventSink 2");
	bRet = TRUE;
Exit0:
	return bRet;
}

void CKeyEventSink::unInitKeyEventSink()
{
	CComPtr<ITfKeystrokeMgr> pKeyMgr;
	if (m_pTextService->getThreadMgr()->QueryInterface(&pKeyMgr) == S_OK)
	{
		pKeyMgr->UnadviseKeyEventSink(m_pTextService->getClientId());
	}
}

BOOL CKeyEventSink::initPreservedKey()
{
	if (m_ctrlSpacePreserved) return TRUE;
	CComPtr<ITfKeystrokeMgr> keyMgr;
	if (FAILED(m_pTextService->getThreadMgr()->QueryInterface(&keyMgr))) return FALSE;
	constexpr wchar_t description[] = L"切换中英文输入";
	m_ctrlSpacePreserved = SUCCEEDED(keyMgr->PreserveKey(
		m_pTextService->getClientId(), kCtrlSpaceCommand, &kCtrlSpaceKey,
		description, static_cast<ULONG>(std::size(description) - 1)));
	return m_ctrlSpacePreserved ? TRUE : FALSE;
}

void CKeyEventSink::unInitPreservedKey()
{
	if (!m_ctrlSpacePreserved) return;
	CComPtr<ITfKeystrokeMgr> keyMgr;
	if (SUCCEEDED(m_pTextService->getThreadMgr()->QueryInterface(&keyMgr)))
		keyMgr->UnpreserveKey(kCtrlSpaceCommand, &kCtrlSpaceKey);
	m_ctrlSpacePreserved = false;
}

bool CKeyEventSink::isPreQuote()
{
	return keyState().preQuote;
}

bool CKeyEventSink::isShiftDown()
{
	return (keyState().modifierMask & SHIFT) != 0;
}

bool CKeyEventSink::isCtrlDown()
{
	return (keyState().modifierMask & CTRL) != 0;
}

bool CKeyEventSink::isAltDown()
{
	return (keyState().modifierMask & ALT) != 0;
}

bool CKeyEventSink::isWinDown()
{
	return (keyState().modifierMask & WIN) != 0;
}

bool CKeyEventSink::isCapsLockOn()
{
	// 使用GetKeyState获取CapsLock状态，确保获取到最新的状态
	return (GetKeyState(VK_CAPITAL) & 1) != 0;
}

void CKeyEventSink::setPreQuote(const bool preQuote)
{
	keyState().preQuote = preQuote;
}

void CKeyEventSink::clear() {
	keyState() = {};
}

void CKeyEventSink::clearContext(ITfContext* context)
{
	if (!context)
		return;
	const auto* state = m_pTextService->m_tsfThreadContext.contexts().find(context);
	if (state && state->token().contextId == m_activeContextToken.contextId)
		m_activeContextToken = {};
}

coral::tsf::ContextKeyState::PendingDecision CKeyEventSink::evaluateKeyDecision(
	ITfContext* context, const coral::tsf::KeyEventSnapshot& event) const
{
	coral::tsf::ContextKeyState::PendingDecision result;
	result.valid = true;
	result.virtualKey = event.virtualKey();
	result.keyDown = event.keyDown();
	result.shortcut = coral::tsf::DecideShortcut(
		event, CaptureShortcutContext(m_pTextService, context, event));
	if (!result.shortcut.decided())
	{
		result.handling = m_pTextService->previewKeyDecision(
			context, static_cast<WPARAM>(event.virtualKey()), event.keyDown(),
			event.shift(), event.ctrl(), event.alt(), event.win(), event.capsLock());
	}
	return result;
}

void CKeyEventSink::rememberTestDecision(
	ITfContext* context, const coral::tsf::ContextKeyState::PendingDecision& decision)
{
	if (auto* state = m_pTextService->m_tsfThreadContext.contexts().find(context))
		state->keyState().pendingDecision = decision;
}

coral::tsf::ContextKeyState::PendingDecision CKeyEventSink::consumeTestDecisionOrEvaluate(
	ITfContext* context, const coral::tsf::KeyEventSnapshot& event)
{
	auto& pending = keyState().pendingDecision;
	if (pending.valid && pending.virtualKey == event.virtualKey() &&
		pending.keyDown == event.keyDown())
	{
		auto result = pending;
		pending = {};
		return result;
	}
	pending = {};
	return evaluateKeyDecision(context, event);
}

void CKeyEventSink::testPreservedKeyStatus()
{
	// 仅检查Ctrl、Shift、Alt状态
	// shift：VK_SHIFT VK_LSHIFT VK_RSHIFT
	// ctrl：VK_CONTROL VK_LCONTROL VK_RCONTROL
	// alt：VK_MENU VK_LMENU VK_RMENU
	bool isCtrlDown = false;
	bool isShiftDown = false;
	bool isAltDown = false;
	bool isWinDown = false;
	keyState().enterDown = false;
	BYTE keys[256] = {};
	GetKeyboardState(keys);

	// 检查Ctrl键状态
	if (keys[VK_CONTROL] & 0x80 || keys[VK_LCONTROL] & 0x80 || keys[VK_RCONTROL] & 0x80)
	{
		isCtrlDown = true;
	}

	// 检查Shift键状态
	if (keys[VK_SHIFT] & 0x80 || keys[VK_LSHIFT] & 0x80 || keys[VK_RSHIFT] & 0x80)
	{
		isShiftDown = true;
	}

	// 检查Alt键状态
	if (keys[VK_MENU] & 0x80 || keys[VK_LMENU] & 0x80 || keys[VK_RMENU] & 0x80)
	{
		isAltDown = true;
	}

	// 检查Win键状态,因为keyeaten的时候还有Win键的判断，导致如果不判断的话一旦按下就不能再用中文输入法了
	if (keys[VK_LWIN] & 0x80 || keys[VK_RWIN] & 0x80)
	{
		isWinDown = true;
	}

	// 检查Enter键状态
	if (keys[VK_RETURN] & 0x80)
	{
		keyState().enterDown = true;
	}

	modifyKeyStatusSwitch(VK_CONTROL, isCtrlDown);
	modifyKeyStatusSwitch(VK_SHIFT, isShiftDown);
	modifyKeyStatusSwitch(VK_MENU, isAltDown);
	modifyKeyStatusSwitch(VK_LWIN, isWinDown);
}

void CKeyEventSink::modifyKeyStatusSwitch(const WPARAM wParam, const bool isDown)
{
	auto status = [&isDown , this](PreservedKeyStatus status)
	{
		auto& mask = keyState().modifierMask;
		const auto bit = static_cast<std::uint8_t>(status);
		if (isDown)
			mask = static_cast<std::uint8_t>(mask | bit);
		else
			mask = static_cast<std::uint8_t>(mask & static_cast<std::uint8_t>(~bit));
	
	};

	updatePreInputStatus(wParam); // 更新shift状态

	switch (wParam)
	{
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT:
		status(SHIFT);
		break;
	case VK_CONTROL:
	case VK_LCONTROL:
	case VK_RCONTROL:
		status(CTRL);
		break;
	case VK_MENU:
	case VK_LMENU:
	case VK_RMENU:
		status(ALT);
		break;
	case VK_LWIN:
	case VK_RWIN:
		status(WIN);
		break;
	}
}

bool CKeyEventSink::areAltWinCtrlNotPressed()
{
	PreservedKeyStatus mask = static_cast<PreservedKeyStatus>(ALT | WIN | CTRL);
	PreservedKeyStatus result = static_cast<PreservedKeyStatus>(keyState().modifierMask & mask);
	return result == NONE;
}

bool CKeyEventSink::areAltWinShiftNotPressed()
{
	PreservedKeyStatus mask = static_cast<PreservedKeyStatus>(ALT | WIN | SHIFT);
	PreservedKeyStatus result = static_cast<PreservedKeyStatus>(keyState().modifierMask & mask);
	return result == NONE;
}


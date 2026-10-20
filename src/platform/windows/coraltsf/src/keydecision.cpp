#include <windows.h>
#include "keydecision.h"

#include <array>

namespace coral::tsf
{
namespace
{
constexpr std::uint32_t kCloseCompositionKey = 0xE8;
constexpr std::uint32_t kAutoDigitSymbolKey = 0xE9;

struct ShortcutRule
{
	std::uint32_t virtualKey;
	bool shift;
	bool ctrl;
	bool alt;
};

constexpr std::array<ShortcutRule, 5> kHandledShortcuts = {{
	{ VK_F11, true, true, true },
	{ VK_F12, true, true, true },
	{ VK_SPACE, true, false, false },
	{ kCloseCompositionKey, false, false, false },
	{ VK_OEM_PERIOD, false, true, false },
}};

bool IsPunctuation(std::uint32_t key, bool shift)
{
	if (key >= '0' && key <= '9')
		return shift;
	switch (key)
	{
	case VK_OEM_1:
	case VK_OEM_2:
	case VK_OEM_3:
	case VK_OEM_4:
	case VK_OEM_5:
	case VK_OEM_6:
	case VK_OEM_7:
	case VK_OEM_COMMA:
	case VK_OEM_PERIOD:
	case VK_OEM_MINUS:
	case VK_OEM_PLUS:
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

bool CanConvertToFullWidth(std::uint32_t key)
{
	if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
		(key >= VK_NUMPAD0 && key <= VK_NUMPAD9))
		return true;
	switch (key)
	{
	case VK_SPACE:
	case VK_OEM_1:
	case VK_OEM_2:
	case VK_OEM_3:
	case VK_OEM_4:
	case VK_OEM_5:
	case VK_OEM_6:
	case VK_OEM_7:
	case VK_OEM_COMMA:
	case VK_OEM_PERIOD:
	case VK_OEM_MINUS:
	case VK_OEM_PLUS:
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
}

CandidatePageKey DecideCandidatePageKey(const KeyEventSnapshot& event,
    bool candidates_visible, bool v_mode, const CandidatePageKeys& enabled)
{
    if (!event.keyDown() || !candidates_visible || event.shift() ||
        event.ctrl() || event.alt() || event.win())
        return CandidatePageKey::None;
    switch (event.virtualKey())
    {
    case VK_OEM_MINUS: return enabled.minus_equals ? CandidatePageKey::Previous : CandidatePageKey::None;
    case VK_OEM_PLUS: return enabled.minus_equals ? CandidatePageKey::Next : CandidatePageKey::None;
    case VK_PRIOR: return enabled.page_up_down ? CandidatePageKey::Previous : CandidatePageKey::None;
    case VK_NEXT: return enabled.page_up_down ? CandidatePageKey::Next : CandidatePageKey::None;
    case VK_OEM_4: return enabled.brackets ? CandidatePageKey::Previous : CandidatePageKey::None;
    case VK_OEM_6: return enabled.brackets ? CandidatePageKey::Next : CandidatePageKey::None;
    case VK_OEM_COMMA: return enabled.commaPeriod && !v_mode ? CandidatePageKey::Previous : CandidatePageKey::None;
    case VK_OEM_PERIOD: return enabled.commaPeriod && !v_mode ? CandidatePageKey::Next : CandidatePageKey::None;
    default: return CandidatePageKey::None;
    }
}

KeyDecision DecideShortcut(
	const KeyEventSnapshot& event,
	const ShortcutContextSnapshot& context)
{
	if (!event.keyDown())
		return {};
	// Ctrl+Space 必须先于候选 Ctrl 键抑制处理。
	if (context.ctrlSpaceShortcut && event.virtualKey() == VK_SPACE &&
		event.ctrl() && !event.shift() && !event.alt() && !event.win())
		return { KeyDecisionAction::Handle, ShortcutCommand::ToggleLanguageMode };
	if (context.candidates_visible && !context.mouseSelecting && event.ctrl() &&
		!context.configuredCtrlShortcut &&
		event.virtualKey() != VK_CONTROL && event.virtualKey() != VK_LCONTROL &&
		event.virtualKey() != VK_RCONTROL)
	{
		return { KeyDecisionAction::Handle, ShortcutCommand::Consume };
	}

	for (const ShortcutRule& rule : kHandledShortcuts)
	{
		if (event.virtualKey() == rule.virtualKey &&
			event.shift() == rule.shift && event.ctrl() == rule.ctrl && event.alt() == rule.alt)
		{
			ShortcutCommand command = ShortcutCommand::Consume;
			if (rule.virtualKey == VK_SPACE)
				command = ShortcutCommand::ToggleFullWidth;
			else if (rule.virtualKey == kCloseCompositionKey)
				command = ShortcutCommand::CloseComposition;
			else if (rule.virtualKey == VK_OEM_PERIOD)
				command = ShortcutCommand::TogglePunctuation;
			return { KeyDecisionAction::Handle, command };
		}
	}
	return {};
}

bool ShouldCaptureModeSwitchKey(
	const KeyEventSnapshot& event,
	bool shiftShortcutEnabled,
	bool ctrlShortcutEnabled)
{
	const std::uint32_t key = event.virtualKey();
	if (shiftShortcutEnabled &&
		(key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT))
	{
		return true;
	}
	if (ctrlShortcutEnabled &&
		(key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL))
	{
		return true;
	}
	return false;
}

namespace
{
KeyHandlingDecision DecideBaseKeyHandling(
	const KeyEventSnapshot& event,
	const KeyHandlingContextSnapshot& context)
{
	const std::uint32_t key = event.virtualKey();
	KeyHandlingDecision decision;
    if (!context.keyboardDisabled && context.candidates_visible &&
        !event.ctrl() && !event.alt() && !event.win())
    {
        if (context.translation_mode ||
            (context.translation_shortcut && key == context.translation_shortcut_key && !event.shift() &&
                !(key == VK_OEM_PLUS && context.minus_equals_page)) ||
            (context.minus_equals_page && !event.shift() && (key == VK_OEM_MINUS || key == VK_OEM_PLUS)) ||
            (context.page_up_down && !event.shift() && (key == VK_PRIOR || key == VK_NEXT)))
        {
            decision.handleType = KeyHandleType::Normal;
            return decision;
        }
    }
	if (context.candidates_visible && !context.mouseSelecting)
	{
		// 这些已配置的候选动作优先于普通 OEM 键输入。
		if (context.secondThirdCandidateKey == 1 && !event.shift())
		{
			if (key == VK_OEM_1)
				decision.candidateSelectionEffect = CandidateSelectionEffect::SecondCandidate;
			else if (key == VK_OEM_7)
				decision.candidateSelectionEffect = CandidateSelectionEffect::ThirdCandidate;
		}
		if (decision.candidateSelectionEffect == CandidateSelectionEffect::None)
		{
			if (context.wordToCharacterKey == 1)
			{
				if (key == VK_OEM_MINUS)
					decision.candidateSelectionEffect = CandidateSelectionEffect::FirstCharacter;
				else if (key == VK_OEM_PLUS)
					decision.candidateSelectionEffect = CandidateSelectionEffect::LastCharacter;
			}
			else if (context.wordToCharacterKey == 2)
			{
				if (key == VK_OEM_4)
					decision.candidateSelectionEffect = CandidateSelectionEffect::FirstCharacter;
				else if (key == VK_OEM_6)
					decision.candidateSelectionEffect = CandidateSelectionEffect::LastCharacter;
			}
		}
		if (decision.candidateSelectionEffect != CandidateSelectionEffect::None)
		{
			decision.handleType = KeyHandleType::Normal;
			return decision;
		}
	}
	if (key == kAutoDigitSymbolKey)
	{
		decision.handleType = KeyHandleType::Normal;
		return decision;
	}

	if (event.capsLock() && !context.candidates_visible)
	{
		decision.handleType = context.full_width && CanConvertToFullWidth(key) && !event.ctrl()
			? KeyHandleType::FullWidth : KeyHandleType::Nothing;
		return decision;
	}

	decision.markPreInput = key != VK_SHIFT && key != VK_LSHIFT &&
		key != VK_RSHIFT && event.shift();
	if (context.asciiMode)
	{
		decision.handleType = context.full_width && CanConvertToFullWidth(key) && !event.ctrl()
			? KeyHandleType::FullWidth : KeyHandleType::Nothing;
		return decision;
	}

	if (!context.candidates_visible &&
		(context.punctuationLanguageMode != 0 || context.useEnglishPunctuation) &&
		IsPunctuation(key, event.shift()))
	{
		const bool useEnglish = context.punctuationLanguageMode == 0
			? context.useEnglishPunctuation : context.punctuationLanguageMode == 1;
		if (useEnglish)
		{
			decision.handleType = context.full_width && CanConvertToFullWidth(key) && !event.ctrl()
				? KeyHandleType::FullWidth : KeyHandleType::Nothing;
			return decision;
		}
	}

	if (context.keyboardDisabled ||
		((event.alt() || event.ctrl() || event.win()) && !context.mouseSelecting))
		return decision;

	decision.fullWidthStateEffect = FullWidthStateEffect::Clear;
	switch (key)
	{
	case VK_LEFT:
	case VK_RIGHT:
	case VK_UP:
	case VK_DOWN:
	case VK_RETURN:
	case VK_BACK:
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT:
	case VK_ESCAPE:
	case VK_TAB:
	case VK_HOME:
	case VK_END:
	case VK_INSERT:
	case VK_DELETE:
	case VK_SEPARATOR:
		decision.handleType = context.candidates_visible
			? KeyHandleType::Normal : KeyHandleType::Nothing;
		if (key == VK_BACK && context.reconcileBackspaceHistory)
			decision.inputHistoryEffect = InputHistoryEffect::ReconcileBackspace;
		else if (key == VK_LEFT || key == VK_RIGHT)
			decision.inputHistoryEffect = InputHistoryEffect::Reset;
		return decision;
	case VK_SPACE:
	case VK_OEM_PLUS:
		decision.handleType = context.candidates_visible ? KeyHandleType::Normal
			: (context.full_width ? KeyHandleType::FullWidth : KeyHandleType::Nothing);
		return decision;
	case VK_NUMPAD0:
	case VK_NUMPAD1:
	case VK_NUMPAD2:
	case VK_NUMPAD3:
	case VK_NUMPAD4:
	case VK_NUMPAD5:
	case VK_NUMPAD6:
	case VK_NUMPAD7:
	case VK_NUMPAD8:
	case VK_NUMPAD9:
	case VK_MULTIPLY:
	case VK_ADD:
	case VK_SUBTRACT:
	case VK_DIVIDE:
	case VK_DECIMAL:
		decision.handleType = context.candidates_visible ? KeyHandleType::Normal
			: (context.asciiMode && context.full_width
				? KeyHandleType::FullWidth : KeyHandleType::Nothing);
		return decision;
	case VK_OEM_1:
	case VK_OEM_2:
	case VK_OEM_3:
	case VK_OEM_4:
	case VK_OEM_5:
	case VK_OEM_6:
	case VK_OEM_7:
	case VK_OEM_COMMA:
	case VK_OEM_PERIOD:
	case VK_OEM_MINUS:
		decision.handleType = KeyHandleType::Normal;
		decision.fullWidthStateEffect = context.full_width
			? FullWidthStateEffect::Set : FullWidthStateEffect::Clear;
		return decision;
	default:
		if (key >= 'A' && key <= 'Z')
		{
			decision.handleType = KeyHandleType::Normal;
			return decision;
		}
		if (key >= '0' && key <= '9')
		{
			decision.handleType = KeyHandleType::Normal;
			if (context.full_width && !context.candidates_visible)
				decision.fullWidthStateEffect = FullWidthStateEffect::Set;
			return decision;
		}
		return decision;
	}
}
}

KeyHandlingDecision DecideKeyHandling(
	const KeyEventSnapshot& event,
	const KeyHandlingContextSnapshot& context)
{
	// 键抬起是独立的 TSF 事件。绝不能因为携带相同的虚拟键码
	// 就把它解释成第二次按下。
	if (!event.keyDown())
		return {};
	KeyHandlingDecision decision = DecideBaseKeyHandling(event, context);
	if (decision.handleType == KeyHandleType::Nothing &&
		(context.autoSymbolCompletionKey || context.autoConvertDigitSymbolKey))
	{
		decision.handleType = KeyHandleType::Normal;
	}
	return decision;
}

wchar_t InitialCompositionCharacter(std::uint32_t virtualKey)
{
	if ((virtualKey >= 'A' && virtualKey <= 'Z') ||
		(virtualKey >= '0' && virtualKey <= '9'))
		return static_cast<wchar_t>(virtualKey);
	if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)
		return static_cast<wchar_t>(L'0' + virtualKey - VK_NUMPAD0);
	return L'\0';
}

} // 命名空间 coral::tsf 结束

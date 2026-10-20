#pragma once

#include <cstdint>

namespace coral::tsf
{

enum class KeyHandleType
{
	Nothing = 0,
	FullWidth,
	Normal,
};

class KeyEventSnapshot final
{
public:
	KeyEventSnapshot(
		std::uint32_t virtualKey,
		bool keyDown,
		bool shift,
		bool ctrl,
		bool alt,
		bool win,
		bool capsLock)
		: m_virtualKey(virtualKey)
		, m_keyDown(keyDown)
		, m_shift(shift)
		, m_ctrl(ctrl)
		, m_alt(alt)
		, m_win(win)
		, m_capsLock(capsLock)
	{
	}

	std::uint32_t virtualKey() const { return m_virtualKey; }
	bool keyDown() const { return m_keyDown; }
	bool shift() const { return m_shift; }
	bool ctrl() const { return m_ctrl; }
	bool alt() const { return m_alt; }
	bool win() const { return m_win; }
	bool capsLock() const { return m_capsLock; }

private:
	const std::uint32_t m_virtualKey;
	const bool m_keyDown;
	const bool m_shift;
	const bool m_ctrl;
	const bool m_alt;
	const bool m_win;
	const bool m_capsLock;
};

enum class CandidatePageKey { None, Previous, Next };

struct CandidatePageKeys {
    bool page_up_down = true;
    bool brackets = true;
    bool commaPeriod = true;
    bool minus_equals = false;
};

CandidatePageKey DecideCandidatePageKey(const KeyEventSnapshot& event,
    bool candidates_visible, bool v_mode, const CandidatePageKeys& enabled);

enum class KeyDecisionAction
{
	Undecided,
	PassThrough,
	Handle,
};

enum class ShortcutCommand
{
	None,
	Consume,
	ToggleFullWidth,
	CloseComposition,
	TogglePunctuation,
	ToggleLanguageMode,
};

struct KeyDecision
{
	KeyDecisionAction action = KeyDecisionAction::Undecided;
	ShortcutCommand command = ShortcutCommand::None;
	bool consumed() const { return action == KeyDecisionAction::Handle; }
	bool decided() const { return action != KeyDecisionAction::Undecided; }
};

struct ShortcutContextSnapshot
{
	bool candidates_visible = false;
	bool mouseSelecting = false;
	bool configuredCtrlShortcut = false;
	bool ctrlSpaceShortcut = true;
};

struct KeyHandlingContextSnapshot
{
	bool candidates_visible = false;
	bool mouseSelecting = false;
	bool asciiMode = false;
	bool full_width = false;
	bool keyboardDisabled = false;
	bool useEnglishPunctuation = false;
	int punctuationLanguageMode = 0;
	bool reconcileBackspaceHistory = false;
	bool autoSymbolCompletionKey = false;
	bool autoConvertDigitSymbolKey = false;
	int secondThirdCandidateKey = 0;
	int wordToCharacterKey = 0;
    bool page_up_down = true;
    bool translation_shortcut = false;
    bool translation_mode = false;
    std::uint32_t translation_shortcut_key = 0xBB;
    bool minus_equals_page = false;
};

enum class CandidateSelectionEffect
{
	None,
	SecondCandidate,
	ThirdCandidate,
	FirstCharacter,
	LastCharacter,
};

enum class InputHistoryEffect
{
	None,
	ReconcileBackspace,
	Reset,
};

enum class FullWidthStateEffect
{
	Keep,
	Clear,
	Set,
};

struct KeyHandlingDecision
{
	KeyHandleType handleType = KeyHandleType::Nothing;
	InputHistoryEffect inputHistoryEffect = InputHistoryEffect::None;
	FullWidthStateEffect fullWidthStateEffect = FullWidthStateEffect::Keep;
	CandidateSelectionEffect candidateSelectionEffect = CandidateSelectionEffect::None;
	bool markPreInput = false;
};

KeyDecision DecideShortcut(
	const KeyEventSnapshot& event,
	const ShortcutContextSnapshot& context);

// 作为点按切换快捷键的修饰键，必须在其按下与抬起
// 两个事件上都捕获。某些 TSF 宿主只有在对应的 OnTest 回调
// 把事件报告为已吃掉之后才会调用 OnKeyDown/OnKeyUp。
bool ShouldCaptureModeSwitchKey(
	const KeyEventSnapshot& event,
	bool shiftShortcutEnabled,
	bool ctrlShortcutEnabled);

KeyHandlingDecision DecideKeyHandling(
	const KeyEventSnapshot& event,
	const KeyHandlingContextSnapshot& context);

// 只有具有无歧义可打印字符的键才能开启新的预编辑。
// OEM 虚拟键值是按键标识，不是 Unicode 标点。
wchar_t InitialCompositionCharacter(std::uint32_t virtualKey);

} // 命名空间 coral::tsf 结束

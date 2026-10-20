#include "keydecision.h"
#include "candidatestatemodel.h"

#include <windows.h>

#include <array>
#undef NDEBUG
#include <cassert>
#include <cwchar>
#include <iostream>
#include <type_traits>

using coral::tsf::DecideShortcut;
using coral::tsf::DecideKeyHandling;
using coral::tsf::CandidateSelectionEffect;
using coral::tsf::FullWidthStateEffect;
using coral::tsf::InputHistoryEffect;
using coral::tsf::InitialCompositionCharacter;
using coral::tsf::KeyDecisionAction;
using coral::tsf::KeyEventSnapshot;
using coral::tsf::KeyHandleType;
using coral::tsf::KeyHandlingContextSnapshot;
using coral::tsf::ShortcutCommand;
using coral::tsf::ShortcutContextSnapshot;
using coral::tsf::ShouldCaptureModeSwitchKey;

namespace
{
struct TestCase
{
	const char* name;
	KeyEventSnapshot event;
	ShortcutContextSnapshot context;
	KeyDecisionAction expected;
};
}

int main()
{
	{
		coral::tsf::CandidateStateModel state;
		state.setPageSize(3);
		state.beginComposition(7);
		state.replace({
			{ 11, L"一", L"yi" }, { 12, L"二", L"er" },
			{ 13, L"三", L"san" }, { 14, L"四", L"si" },
			{ 15, L"五", L"wu" } }, true);
		assert(state.version() == 7);
		assert(state.currentRowSize() == 3);
		assert(state.moveNextRow());
		assert(state.selectedIndex() == 3);
		assert(state.currentRowSize() == 2);
		assert(state.selectById(15));
		assert(state.selectedIndex() == 4);
		state.append({ { 15, L"重复", L"chongfu" }, { 16, L"六", L"liu" } }, false);
		assert(state.size() == 6);
		assert(!state.hasMore());
		state.reset();
		assert(state.empty());
		state.replace({ { 21, L"瞐(mò)", L"umumumu", L"瞐" },
			{ 22, L"骰(tou)子", L"shaizi", L"骰子" },
			{ 23, L"原文(括号)", L"yuanwen" } }, true);
		assert(state.item(0)->text == L"瞐(mò)");
		assert(state.item(0)->word() == L"瞐");
		assert(state.item(1)->word() == L"骰子");
		assert(state.item(2)->word() == L"原文(括号)");
		state.append({ { 24, L"㐀(qiū)", L"ushhsh", L"㐀" } }, false);
		assert(state.item(3)->word() == L"㐀");
	}
	{
		using coral::tsf::SegmentComposition;
		const struct { const wchar_t* raw; const wchar_t* pinyin; const wchar_t* expected; } segments[] = {
			{ L"nihaobuhao", L"ni'hao'bu'hao", L"ni'hao'bu'hao" },
			{ L"xian", L"xian", L"xian" },
			{ L"xian", L"xi", L"xi'an" },
			{ L"xi'an", L"xi'an", L"xi'an" },
			{ L"ni'", L"ni", L"ni'" },
			{ L"nih", L"ni'h", L"ni'h" },
			{ L"nhbh", L"n'h'b'h", L"n'h'b'h" },
			{ L"Nihao", L"ni'hao", L"Ni'hao" },
			{ L"nihaoqwe", L"ni'hao'qie", L"ni'hao'qwe" },
			{ L"zonguo", L"zhong'guo", L"zonguo" },
			{ L"nihao", L"", L"nihao" },
			{ L"", L"ni'hao", L"" },
		};
		for (const auto& segment : segments)
		{
			const auto result = SegmentComposition(segment.raw, segment.pinyin);
			if (result.text != segment.expected ||
				result.rawToDisplay.size() != std::wcslen(segment.raw) + 1 ||
				result.rawToDisplay.back() != static_cast<int>(result.text.size()))
			{
				std::wcerr << L"failed: segment " << segment.raw << L" -> " << result.text << L'\n';
				return 1;
			}
		}
		const auto cursor = SegmentComposition(L"nihao", L"ni'hao");
		if (cursor.rawToDisplay != std::vector<int>{ 0, 1, 2, 4, 5, 6 })
		{
			std::cerr << "failed: segmented composition cursor mapping\n";
			return 1;
		}
	}
	static_assert(!std::is_copy_assignable<KeyEventSnapshot>::value,
		"KeyEventSnapshot must stay immutable");
	for (const bool keyDown : { false, true })
	{
		assert(ShouldCaptureModeSwitchKey(
			KeyEventSnapshot(VK_SHIFT, keyDown, keyDown, false, false, false, false),
			true, false));
		assert(ShouldCaptureModeSwitchKey(
			KeyEventSnapshot(VK_RSHIFT, keyDown, keyDown, false, false, false, false),
			true, false));
		assert(ShouldCaptureModeSwitchKey(
			KeyEventSnapshot(VK_CONTROL, keyDown, false, keyDown, false, false, false),
			false, true));
		assert(!ShouldCaptureModeSwitchKey(
			KeyEventSnapshot(VK_SHIFT, keyDown, keyDown, false, false, false, false),
			false, false));
		assert(!ShouldCaptureModeSwitchKey(
			KeyEventSnapshot('A', keyDown, keyDown, false, false, false, false),
			true, true));
	}
	if (InitialCompositionCharacter('A') != L'A' ||
		InitialCompositionCharacter('1') != L'1' ||
		InitialCompositionCharacter(VK_NUMPAD1) != L'1')
	{
		std::cerr << "failed: initial alphanumeric preedit\n";
		return 1;
	}
	for (const auto key : { VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2, VK_OEM_5 })
	{
		if (InitialCompositionCharacter(key) != L'\0')
		{
			std::cerr << "failed: OEM key leaked into initial preedit\n";
			return 1;
		}
	}

	// 在空闲与候选可见两种上下文中，Ctrl+Space 都具有最高优先级。
	for (bool visible : { false, true })
	{
		const KeyEventSnapshot ctrlSpace(VK_SPACE, true, false, true, false, false, false);
		const auto decision = DecideShortcut(ctrlSpace, { visible, false, false, true });
		if (!decision.consumed() || decision.command != ShortcutCommand::ToggleLanguageMode)
		{
			std::cerr << "failed: Ctrl+Space language switch precedence\n";
			return 1;
		}
	}
	for (const KeyEventSnapshot event : {
		KeyEventSnapshot(VK_SPACE, false, false, true, false, false, false),
		KeyEventSnapshot(VK_SPACE, true, false, false, false, false, false),
		KeyEventSnapshot(VK_SPACE, true, true, true, false, false, false),
		KeyEventSnapshot(VK_SPACE, true, false, true, true, false, false),
		KeyEventSnapshot(VK_SPACE, true, false, true, false, true, false) })
	{
		if (DecideShortcut(event, {}).command == ShortcutCommand::ToggleLanguageMode)
		{
			std::cerr << "failed: non-Ctrl+Space event switched language\n";
			return 1;
		}
	}
	if (DecideShortcut(KeyEventSnapshot(VK_SPACE, true, false, true, false, false, false),
		{ false, false, false, false }).decided())
	{
		std::cerr << "failed: disabled Ctrl+Space shortcut consumed\n";
		return 1;
	}

	// 所有已配置的翻页键对共享方向与修饰键规则。
	const std::array<std::uint32_t, 6> pageKeys{
		VK_PRIOR, VK_NEXT, VK_OEM_4, VK_OEM_6, VK_OEM_COMMA, VK_OEM_PERIOD };
	for (std::size_t i = 0; i < pageKeys.size(); ++i)
	{
		const auto key = pageKeys[i];
		const KeyEventSnapshot down(key, true, false, false, false, false, false);
		const auto expected = i % 2 == 0 ? coral::tsf::CandidatePageKey::Previous : coral::tsf::CandidatePageKey::Next;
		if (coral::tsf::DecideCandidatePageKey(down, true, false, {}) != expected ||
			coral::tsf::DecideCandidatePageKey(down, false, false, {}) != coral::tsf::CandidatePageKey::None ||
			coral::tsf::DecideCandidatePageKey(down, true, false, { false, false, false }) != coral::tsf::CandidatePageKey::None)
		{
			std::cerr << "failed: configured paging direction/visibility/disable rules\n";
			return 1;
		}
		for (const auto event : {
			KeyEventSnapshot(key, false, false, false, false, false, false),
			KeyEventSnapshot(key, true, true, false, false, false, false),
			KeyEventSnapshot(key, true, false, true, false, false, false),
			KeyEventSnapshot(key, true, false, false, true, false, false),
			KeyEventSnapshot(key, true, false, false, false, true, false) })
		{
			if (coral::tsf::DecideCandidatePageKey(event, true, false, {}) != coral::tsf::CandidatePageKey::None)
			{
				std::cerr << "failed: modified or released key paged candidates\n";
				return 1;
			}
		}
		const auto vExpected = i < 4 ? expected : coral::tsf::CandidatePageKey::None;
		if (coral::tsf::DecideCandidatePageKey(down, true, true, {}) != vExpected)
		{
			std::cerr << "failed: V-mode paging exception\n";
			return 1;
		}
	}

    // 只有三个设置暴露的六个键可以翻动候选。
    for (std::uint32_t key = 0; key <= 255; ++key) {
        bool configured = false;
        for (const auto allowed : pageKeys) configured = configured || key == allowed;
        if (!configured && coral::tsf::DecideCandidatePageKey(
            KeyEventSnapshot(key, true, false, false, false, false, false),
            true, false, {}) != coral::tsf::CandidatePageKey::None) {
            std::cerr << "failed: unconfigured key paged candidates\n";
            return 1;
        }
    }
    for (const auto key : { VK_PRIOR, VK_NEXT }) {
        coral::tsf::KeyHandlingContextSnapshot context;
        context.candidates_visible = true;
        if (coral::tsf::DecideKeyHandling(
            KeyEventSnapshot(key, true, false, false, false, false, false), context).handleType
            != coral::tsf::KeyHandleType::Normal) {
            std::cerr << "failed: PageUp/PageDown binding not captured\n";
            return 1;
        }
    }
    for (const auto key : { '9', '0' }) {
        if (coral::tsf::DecideCandidatePageKey(
            KeyEventSnapshot(key, true, true, false, false, false, false),
            true, false, {}) != coral::tsf::CandidatePageKey::None) {
            std::cerr << "failed: parentheses paged candidates\n";
            return 1;
        }
    }

    // 加减翻页默认关闭；开启后优先于以词定字，修饰键与抬起仍放行。
    for (const auto key : {VK_OEM_MINUS, VK_OEM_PLUS}) {
        const auto direction = key == VK_OEM_MINUS ? coral::tsf::CandidatePageKey::Previous : coral::tsf::CandidatePageKey::Next;
        for (bool v_mode : {false, true}) {
            assert(coral::tsf::DecideCandidatePageKey(KeyEventSnapshot(key, true, false, false, false, false, false),
                true, v_mode, {false, false, false, true}) == direction);
        }
        KeyHandlingContextSnapshot context;
        context.candidates_visible = true; context.minus_equals_page = true; context.wordToCharacterKey = 1;
        const auto decision = DecideKeyHandling(KeyEventSnapshot(key, true, false, false, false, false, false), context);
        assert(decision.handleType == KeyHandleType::Normal && decision.candidateSelectionEffect == CandidateSelectionEffect::None);
        for (const auto event : {
            KeyEventSnapshot(key, false, false, false, false, false, false),
            KeyEventSnapshot(key, true, true, false, false, false, false),
            KeyEventSnapshot(key, true, false, true, false, false, false),
            KeyEventSnapshot(key, true, false, false, true, false, false),
            KeyEventSnapshot(key, true, false, false, false, true, false)}) {
            assert(coral::tsf::DecideCandidatePageKey(event, true, false, {false, false, false, true}) == coral::tsf::CandidatePageKey::None);
        }
        assert(coral::tsf::DecideCandidatePageKey(KeyEventSnapshot(key, true, false, false, false, false, false),
            false, false, {false, false, false, true}) == coral::tsf::CandidatePageKey::None);
    }
    // 功能键只在有普通候选并启用绑定时捕获；全部十二个选项遵循同一规则。
    for (std::uint32_t key = VK_F1; key <= VK_F12; ++key) {
        KeyHandlingContextSnapshot context;
        context.candidates_visible = true; context.translation_shortcut = true;
        context.translation_shortcut_key = key; context.minus_equals_page = true;
        assert(DecideKeyHandling(KeyEventSnapshot(key, true, false, false, false, false, false), context).handleType == KeyHandleType::Normal);
        for (const auto event : {
            KeyEventSnapshot(key, false, false, false, false, false, false),
            KeyEventSnapshot(key, true, true, false, false, false, false),
            KeyEventSnapshot(key, true, false, true, false, false, false),
            KeyEventSnapshot(key, true, false, false, true, false, false),
            KeyEventSnapshot(key, true, false, false, false, true, false)}) {
            assert(DecideKeyHandling(event, context).handleType == KeyHandleType::Nothing);
        }
        context.translation_shortcut = false;
        assert(DecideKeyHandling(KeyEventSnapshot(key, true, false, false, false, false, false), context).handleType == KeyHandleType::Nothing);
        context.translation_shortcut = true; context.candidates_visible = false;
        assert(DecideKeyHandling(KeyEventSnapshot(key, true, false, false, false, false, false), context).handleType == KeyHandleType::Nothing);
    }

    // 等号翻译优先于可选的词转字绑定。
    {
        coral::tsf::KeyHandlingContextSnapshot context;
        context.candidates_visible = true;
        context.translation_shortcut = true;
        context.wordToCharacterKey = 1;
        const auto decision = coral::tsf::DecideKeyHandling(
            KeyEventSnapshot(VK_OEM_PLUS, true, false, false, false, false, false), context);
        if (decision.handleType != coral::tsf::KeyHandleType::Normal ||
            decision.candidateSelectionEffect != coral::tsf::CandidateSelectionEffect::None) return 1;
        context.wordToCharacterKey = 0;
        context.translation_shortcut = false;
        context.page_up_down = false;
        for (const auto key : {VK_PRIOR, VK_NEXT}) {
            if (coral::tsf::DecideKeyHandling(
                KeyEventSnapshot(key, true, false, false, false, false, false), context).handleType
                != coral::tsf::KeyHandleType::Nothing) return 1;
        }
        context.translation_mode = true;
        context.wordToCharacterKey = 1;
        context.secondThirdCandidateKey = 1;
        for (const auto key : { VK_OEM_PLUS, VK_OEM_MINUS, VK_OEM_1, VK_RETURN, VK_ESCAPE }) {
            const auto panel = coral::tsf::DecideKeyHandling(
                KeyEventSnapshot(key, true, false, false, false, false, false), context);
            if (panel.handleType != coral::tsf::KeyHandleType::Normal ||
                panel.candidateSelectionEffect != coral::tsf::CandidateSelectionEffect::None) return 1;
        }
    }

	const std::array<TestCase, 7> cases = {{
		{ "debug_f11", { VK_F11, true, true, true, true, false, false }, {}, KeyDecisionAction::Handle },
		{ "shift_space", { VK_SPACE, true, true, false, false, false, false }, {}, KeyDecisionAction::Handle },
		{ "close_composition", { 0xE8, true, false, false, false, false, false }, {}, KeyDecisionAction::Handle },
		{ "ctrl_period", { VK_OEM_PERIOD, true, false, true, false, false, false }, {}, KeyDecisionAction::Handle },
		{ "plain_letter", { 'A', true, false, false, false, false, false }, {}, KeyDecisionAction::Undecided },
		{ "candidate_ctrl_letter", { 'A', true, false, true, false, false, false }, { true, false }, KeyDecisionAction::Handle },
		{ "mouse_selection", { 'A', true, false, true, false, false, false }, { true, true }, KeyDecisionAction::Undecided },
	}};

	for (const TestCase& test : cases)
	{
		if (DecideShortcut(test.event, test.context).action != test.expected)
		{
			std::cerr << "failed: " << test.name << '\n';
			return 1;
		}
	}

	{
		const KeyEventSnapshot letter('A', true, false, false, false, false, false);
		const KeyHandlingContextSnapshot chinese;
		const auto first = DecideKeyHandling(letter, chinese);
		const auto repeated = DecideKeyHandling(letter, chinese);
		assert(first.handleType == KeyHandleType::Normal);
		assert(repeated.handleType == first.handleType);
		assert(repeated.inputHistoryEffect == InputHistoryEffect::None);
		assert(repeated.fullWidthStateEffect == FullWidthStateEffect::Clear);

		const KeyEventSnapshot letterUp('A', false, false, false, false, false, false);
		const auto keyUpDecision = DecideKeyHandling(letterUp, chinese);
		assert(keyUpDecision.handleType == KeyHandleType::Nothing);
		assert(keyUpDecision.inputHistoryEffect == InputHistoryEffect::None);
		assert(keyUpDecision.fullWidthStateEffect == FullWidthStateEffect::Keep);

		KeyHandlingContextSnapshot asciiFullWidth;
		asciiFullWidth.asciiMode = true;
		asciiFullWidth.full_width = true;
		assert(DecideKeyHandling(letter, asciiFullWidth).handleType == KeyHandleType::FullWidth);

		KeyHandlingContextSnapshot backspace;
		backspace.reconcileBackspaceHistory = true;
		const auto backspaceDecision = DecideKeyHandling(
			KeyEventSnapshot(VK_BACK, true, false, false, false, false, false), backspace);
		assert(backspaceDecision.handleType == KeyHandleType::Nothing);
		assert(backspaceDecision.inputHistoryEffect == InputHistoryEffect::ReconcileBackspace);

		KeyHandlingContextSnapshot autoSymbol;
		autoSymbol.autoSymbolCompletionKey = true;
		assert(DecideKeyHandling(
			KeyEventSnapshot(VK_OEM_4, true, false, false, false, false, false),
			autoSymbol).handleType == KeyHandleType::Normal);

		KeyHandlingContextSnapshot candidate;
		candidate.candidates_visible = true;
		candidate.secondThirdCandidateKey = 1;
		candidate.wordToCharacterKey = 1;
		const auto semicolon = KeyEventSnapshot(VK_OEM_1, true, false, false, false, false, false);
		const auto apostrophe = KeyEventSnapshot(VK_OEM_7, true, false, false, false, false, false);
		assert(DecideKeyHandling(semicolon, candidate).candidateSelectionEffect ==
			CandidateSelectionEffect::SecondCandidate);
		assert(DecideKeyHandling(apostrophe, candidate).candidateSelectionEffect ==
			CandidateSelectionEffect::ThirdCandidate);
		assert(DecideKeyHandling(KeyEventSnapshot(VK_OEM_1, true, true, false, false, false, false),
			candidate).candidateSelectionEffect == CandidateSelectionEffect::None);
		assert(DecideKeyHandling(KeyEventSnapshot(VK_OEM_1, false, false, false, false, false, false),
			candidate).candidateSelectionEffect == CandidateSelectionEffect::None);
		assert(DecideKeyHandling(KeyEventSnapshot(VK_OEM_MINUS, true, false, false, false, false, false),
			candidate).candidateSelectionEffect == CandidateSelectionEffect::FirstCharacter);
		assert(DecideKeyHandling(KeyEventSnapshot(VK_OEM_PLUS, true, false, false, false, false, false),
			candidate).candidateSelectionEffect == CandidateSelectionEffect::LastCharacter);
		candidate.wordToCharacterKey = 2;
		assert(DecideKeyHandling(KeyEventSnapshot(VK_OEM_4, true, false, false, false, false, false),
			candidate).candidateSelectionEffect == CandidateSelectionEffect::FirstCharacter);
		assert(DecideKeyHandling(KeyEventSnapshot(VK_OEM_6, true, false, false, false, false, false),
			candidate).candidateSelectionEffect == CandidateSelectionEffect::LastCharacter);
		candidate.mouseSelecting = true;
		assert(DecideKeyHandling(semicolon, candidate).candidateSelectionEffect ==
			CandidateSelectionEffect::None);
	}

	{
		const KeyEventSnapshot shiftSpace(
			VK_SPACE, true, true, false, false, false, false);
		const auto decision = DecideShortcut(shiftSpace, {});
		assert(decision.consumed());
		assert(decision.command == ShortcutCommand::ToggleFullWidth);

		const KeyEventSnapshot configured('F', true, true, true, false, false, false);
		const auto bypass = DecideShortcut(configured, { true, false, true });
		assert(!bypass.decided());
	}
	return 0;
}

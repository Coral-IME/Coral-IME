#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_KEYEVENTSINK_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_KEYEVENTSINK_H_INCLUDED

#include "contextstore.h"

enum ModeType : uint8_t
{
    NoneMode = 0,
    PMode,
    UMode,
    VMode,
    NineSquareMode
};

class CTextService;
class CKeyEventSink
{
public:
	CKeyEventSink(CTextService* pTextService);
	~CKeyEventSink();

	enum PreservedKeyStatus
	{
		NONE = 0,
		SHIFT = 1 << 0,
		CTRL = 1 << 1,
		ALT = 1 << 2,
		WIN = 1 << 3
	};

public:
	STDMETHODIMP OnSetFocus(BOOL fForeground);
	
	/*
	按键逻辑预处理
	*/
	STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
	
	STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
	/*
	按键逻辑处理
	*/
	STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
	
	STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
	/*
	Windows API，用于处理保留键（preserved keys）。保留键是一组由输入法编辑器（IME）或其他输入处理组件定义并注册的特殊按键组合。
	它们通常用于在IME和应用程序之间实现特定的交互和功能，例如切换输入法、显示候选窗口等
	*/
	STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten);
	int getPreservedKeyStatus();
	bool isPreQuote();

	bool isShiftDown();
	bool isCtrlDown();
	bool isAltDown();
	bool isWinDown();
	bool isCapsLockOn();
	void setPreQuote(const bool preQuote);
	void clear();
	void clearContext(ITfContext* context = nullptr);
	void activateContext(ITfContext* context);

	void changeLanguageModeUp(ITfContext* pic, WPARAM wParam);
	void changeFullHalfWidthDown(ITfContext* pic, WPARAM wParam);
	void changeCharacterSimOrTraDown(ITfContext* pic, WPARAM wParam);
	void changePunctuationLanguageModeDown(ITfContext* pic, WPARAM wParam);

	void updatePreInputStatus(const WPARAM wParam);
	bool getPreInput();
	void setPreInput(bool preInput);
	bool getHasHandleMultiKey();
	void setHasHandleMultiKey(bool hasHandleMultiKey);

	BOOL init();
	void unInit();

private:
	BOOL initKeyEventSink();
	void unInitKeyEventSink();
	bool handleCtrlSpace(ITfContext* context);
	BOOL initPreservedKey();
	void unInitPreservedKey();
	void modifyKeyStatusSwitch(const WPARAM wParam, const bool isDown);
	// 记录或清空输入字符历史（数字间符号转换用）
	void recordInputChar(WPARAM wParam);
	bool areAltWinCtrlNotPressed();
	bool areAltWinShiftNotPressed();
	void testPreservedKeyStatus(); // 每次按下按键前预判断当前键盘的状态Ctrl/Shift/Alt
	coral::tsf::ContextKeyState::PendingDecision evaluateKeyDecision(
		ITfContext* context, const coral::tsf::KeyEventSnapshot& event) const;
	void rememberTestDecision(
		ITfContext* context, const coral::tsf::ContextKeyState::PendingDecision& decision);
	coral::tsf::ContextKeyState::PendingDecision consumeTestDecisionOrEvaluate(
		ITfContext* context, const coral::tsf::KeyEventSnapshot& event);
	coral::tsf::ContextKeyState& keyState();
	const coral::tsf::ContextKeyState& keyState() const;
	bool m_ctrlSpacePreserved = false;
	CTextService* m_pTextService = nullptr;
	coral::tsf::ContextToken m_activeContextToken{};
	coral::tsf::ContextKeyState m_detachedKeyState;
};

#endif // KEYEVENTSINK_H

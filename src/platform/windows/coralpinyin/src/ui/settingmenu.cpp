#include "pch.h"
#include "settingmenu.h"
#include "settingswindow.h"
#include "addphrasedialog.h"

namespace
{
    constexpr wchar_t kFeedbackUrl[] = L"https://coralime.cn/";
}

SettingMenu* SettingMenu::getMenu() {
	static SettingMenu menuIns;
	return &menuIns;
}

void SettingMenu::showSettingMenu()
{
	SettingsWindow::showInstance(); // 显示并居中设置窗口
}

void SettingMenu::showAddPhrasePage(const std::wstring& pinyin, const std::wstring& word)
{
	AddPhraseDialog::Request request;
	request.content = word;
	// 正在输入的拼音作为输入码预填；带分隔符以外的符号时交给默认输入码。
	std::wstring code;
	for (const wchar_t c : pinyin)
	{
		if (c == L'\'') continue;
		if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'))) { code.clear(); break; }
		code += static_cast<wchar_t>(towlower(c));
	}
	request.code = code;
	AddPhraseDialog::show(nullptr, request);
}

void SettingMenu::onSendFeedbackActionTriggered() {
	openUrl(kFeedbackUrl);
}

void SettingMenu::openUrl(const std::wstring& url)
{
	::ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_RESTORE);
}

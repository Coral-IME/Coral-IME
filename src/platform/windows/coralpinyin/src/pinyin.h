#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_PINYIN_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_PINYIN_H_INCLUDED

#include <atlsync.h>

#define WM_POPUP_SETTING (WM_USER + 1000)

/*
该模块主要用于和langengine做通信，里面存储了各种langengine的导出
*/

class CPinyinModule
{
public:
	//todo 因为开了safethreadinit- 参数，该单例只能在单线程下使用
	static CPinyinModule& Instance()
	{
		static CPinyinModule inst;
		return inst;
	}

	HRESULT start();
	void term();
	HRESULT showSettingMenu();
	HRESULT showAboutPage();
	HRESULT showAddPhrasePage(const std::wstring& pinyin, const std::wstring& word);
	HRESULT sendFeedback();
	HRESULT maintainUserLexicon();
	HRESULT flush_user_lexicon();
	HRESULT export_user_lexicon(const std::filesystem::path& lexiconPath);
	hlp::ImportResult import_user_lexicon(const std::filesystem::path& lexiconPath);

private:
	explicit CPinyinModule();
private:
	HANDLE m_hThreadStoreLexicon = nullptr;
};
// 词库维护/落盘定时器由 apphost 承载（替代原 QTimer）

#endif // PINYIN_H

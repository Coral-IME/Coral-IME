#include "pch.h"
#include "pinyin.h"
#include "engineserver.h"
#include "ui/settingmenu.h"
#include "ui/settingswindow.h"
#include "core_operations.h"
#include "apphost.h"
#include <Windows.h>
#include <filesystem>

using namespace coral::runtime;

CPinyinModule::CPinyinModule()
{
};

HRESULT CPinyinModule::start()
{
	if (std::filesystem::exists("debug.txt")) {
		while (!IsDebuggerPresent()) {
			Sleep(100);
		}
	}

	HRESULT hr = E_FAIL;
	{
		// 新版本管道
		CHECK_ERROR(SUCCEEDED(CEngineServer::Instance().Init(getEngineServerName(), 2)));
		// 兼容旧 TSF
		CHECK_ERROR(SUCCEEDED(CEngineServerLegacy::Instance().Init(LEGACY_ENGINE_SERVER_NAME, 1)));
		std::filesystem::path locallowDir = hlp::getLocalLowCoralPinyinPath();
		start_runtime(locallowDir.string().c_str());
	}
	hr = S_OK;
Exit0:
	if (FAILED(hr))
	{
		stop_runtime();
	}
	return hr;
}

void CPinyinModule::term()
{
	CEngineServer::Instance().Uninit();
	stop_runtime();

	if (m_hThreadStoreLexicon)
	{
		::WaitForSingleObject(m_hThreadStoreLexicon, INFINITE);
		::CloseHandle(m_hThreadStoreLexicon);
		m_hThreadStoreLexicon = NULL;
	}
}

//////////////////////////////////////////////////////////////////////////////
// 以下入口由 IPC 工作线程调用，经 appPostTask 转发到主线程执行（替代 QMetaObject::invokeMethod）

HRESULT CPinyinModule::showSettingMenu()
{
	appPostTask([]() {
		SettingMenu::getMenu()->showSettingMenu();
		});
	return S_OK;
}

HRESULT CPinyinModule::showAboutPage()
{
	appPostTask([]() {
		SettingsWindow::showAboutPage();
		});
	return S_OK;
}

HRESULT CPinyinModule::showAddPhrasePage(const std::wstring& pinyin, const std::wstring& word)
{
	std::wstring py = pinyin, w = word;
	appPostTask([py, w]() {
		SettingMenu::getMenu()->showAddPhrasePage(py, w);
		});
	return S_OK;
}

HRESULT CPinyinModule::sendFeedback()
{
	appPostTask([]() {
		SettingMenu::getMenu()->onSendFeedbackActionTriggered();
		});
	return S_OK;
}

HRESULT CPinyinModule::maintainUserLexicon()
{
	const bool storeRes = coral::runtime::maintain_lexicon();
	return storeRes ? S_OK : S_FALSE;
}

HRESULT CPinyinModule::flush_user_lexicon()
{
	const bool storeRes = coral::runtime::flush_user_lexicon();
	return storeRes ? S_OK : S_FALSE;
}

HRESULT CPinyinModule::export_user_lexicon(const std::filesystem::path& lexiconPath)
{
	const bool res = export_user_file(lexiconPath);
	return res ? S_OK : S_FALSE;
}

hlp::ImportResult CPinyinModule::import_user_lexicon(const std::filesystem::path& lexiconPath)
{
	return import_user_file(lexiconPath);
}

#include "pch.h"
#include "engineserver.h"
#include "pinyin.h"
#include "apphost.h"
#include <atlstr.h>
#include <Windows.h>

HRESULT WINAPI CEngineServer::showSettingMenu(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result)
{
	return CPinyinModule::Instance().showSettingMenu();
}

HRESULT WINAPI CEngineServer::showAboutPage(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result)
{
	return CPinyinModule::Instance().showAboutPage();
}

HRESULT __stdcall CEngineServer::sendFeedback(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result)
{
	return CPinyinModule::Instance().sendFeedback();
}

HRESULT __stdcall CEngineServer::showErrorDialog(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result)
{
	if (params.size() < 3)
		return E_INVALIDARG;

	//显示错误弹窗（转主线程弹 MessageBox，替代 QMessageBox）
	std::wstring title = params[0].GetString();
	std::wstring text = params[1].GetString();
	appPostTask([title, text]() {
		::MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
		});
	return S_OK;
}

HRESULT __stdcall CEngineServer::showAddPhrasePage(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result)
{
	// 旧版 TSF 会额外带候选位置与简繁标记，常用语不再使用。
	std::wstring pinyin;
	std::wstring word;
	if (params.size() >= 2)
	{
		pinyin = params[0].GetString();
		word = params[1].GetString();
	}
	return CPinyinModule::Instance().showAddPhrasePage(pinyin, word);
}

HRESULT __stdcall CEngineServer::sendSimOrTra(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result)
{
	// 常用语简繁共用，保留接口以兼容旧版 TSF。
	return S_OK;
}

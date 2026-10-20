#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_ENGINESERVER_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_ENGINESERVER_H_INCLUDED

#include "ipcserver.h"

class CEngineServer : public CIpcServerImpl<CEngineServer>
{
public:
	BEGIN_API_MAP(CEngineServer)
		BIND_API(L"showSettingMenu", showSettingMenu)
		BIND_API(L"showAboutPage", showAboutPage)
		BIND_API(L"sendFeedback", sendFeedback)
		BIND_API(L"showErrorDialog", showErrorDialog)
		BIND_API(L"showAddPhrasePage", showAddPhrasePage)
		BIND_API(L"sendSimOrTra", sendSimOrTra)
	END_API_MAP()

	static HRESULT WINAPI showSettingMenu(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result);
	static HRESULT WINAPI showAboutPage(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result);
	static HRESULT WINAPI sendFeedback(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result);
	static HRESULT WINAPI showErrorDialog(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result);
	static HRESULT WINAPI showAddPhrasePage(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result);
	static HRESULT WINAPI sendSimOrTra(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result);
};

// 监听旧管道名来兼容旧版本tsf
class CEngineServerLegacy : public CIpcServerImpl<CEngineServerLegacy>
{
public:
	BEGIN_API_MAP(CEngineServerLegacy)
		BIND_API(L"showSettingMenu", CEngineServer::showSettingMenu)
		BIND_API(L"showAboutPage", CEngineServer::showAboutPage)
		BIND_API(L"sendFeedback", CEngineServer::sendFeedback)
		BIND_API(L"showErrorDialog", CEngineServer::showErrorDialog)
		BIND_API(L"showAddPhrasePage", CEngineServer::showAddPhrasePage)
		BIND_API(L"sendSimOrTra", CEngineServer::sendSimOrTra)
	END_API_MAP()
};

// 旧管道名常量
constexpr const WCHAR* LEGACY_ENGINE_SERVER_NAME = _T("_#_coral_pinyin_engineserver_#_");

#endif // ENGINESERVER_H

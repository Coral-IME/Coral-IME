#include "coral/product/product_identity.h"
#include "pch.h"
#include "coreapi.h"
#include "preference_registry.h"
#include "processmonitor.h"
#include "input_timing.h"
#include <string>

namespace Coreapi
{
	// 获取当前 Session 的管道名
	inline const CString& getEngineServerName() {
		static const CString name = []() -> CString {
			DWORD session_id = 0;
			if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id)) {
				CString name;
				name.Format(_T("_#_coral_pinyin_engineserver_%u_#_"), session_id);
				return name;
			}
			return _T("_#_coral_pinyin_engineserver_#_");
		}();
		return name;
	}

	static const CString CORALPINYINSVC_NAME = coral::product::kServiceBridgeWide;

	void showSettingMenu()
	{
		RestartCoralPinyin();//拉活主进程
 		CIpcClient client(getEngineServerName());
 		client.Call(L"showSettingMenu");
	}

	void showAboutPage()
	{
		RestartCoralPinyin();//拉活主进程
		CIpcClient client(getEngineServerName());
		client.Call(L"showAboutPage");
	}

	void showAddPhrasePage(const CString& pinyin, const CString& word)
	{
		RestartCoralPinyin();//拉活主进程
		CIpcClient client(getEngineServerName());
		client.Add(pinyin);
		client.Add(word);
		client.Call(L"showAddPhrasePage");
	}

	void sendFeedback()
	{
		RestartCoralPinyin();//拉活主进程
		CIpcClient client(getEngineServerName());
		client.Call(L"sendFeedback");
	}

	void showErrorDialog(const CString& title, const CString& errorMessage, const CString& process_name)
	{
		RestartCoralPinyin();
		CIpcClient client(getEngineServerName());
		client.Add(title);
		client.Add(errorMessage);
		client.Add(process_name);
		client.Call(L"showErrorDialog");
	}

	bool LaunchMainProcessViaService()
	{
		EnsureServiceRunning();//无论是否启动成功，都继续ipc逻辑，如果服务确实没启动，ipc返回失败即可
		CIpcClient client(CORALPINYINSVC_NAME);
		DWORD session_id = 0;
		if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
		{
			CString sessionIdStr;
			sessionIdStr.Format(L"%u", session_id);
			client.Add(sessionIdStr);
		}
		else return false;
		HRESULT hr = client.Call(L"LaunchMainProcess");
		if (SUCCEEDED(hr))
		{
			CString status = client.Get(L"status");
			return (status == L"launched" || status == L"already_running");
		}
		return false;
	}

	bool LaunchEngineHostViaService()
	{
		coral::input_timing::Scope timing(L"engine.launch_via_service");
		const auto serviceStart = coral::input_timing::nowUs();
		EnsureServiceRunning();
		coral::input_timing::mark(L"engine.ensure_service", serviceStart);
		CIpcClient client(CORALPINYINSVC_NAME);
		DWORD session_id = 0;
		if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
		{
			CString sessionIdStr;
			sessionIdStr.Format(L"%u", session_id);
			client.Add(sessionIdStr);
		}
		else return false;
		const auto callStart = coral::input_timing::nowUs();
		const HRESULT hr = client.Call(L"LaunchEngineHost");
		coral::input_timing::mark(L"engine.service_call", callStart, 0, static_cast<unsigned long>(hr));
		if (FAILED(hr))
		{
			LOG_ERROR_W(L"[tsf_engine] event=service_launch_call_failed hr=" +
				std::to_wstring(static_cast<unsigned long>(hr)));
			return false;
		}
		const CString status = client.Get(L"status");
		if (status != L"launched" && status != L"already_running")
			LOG_ERROR_W(L"[tsf_engine] event=service_launch_rejected status=" +
				std::wstring(status.GetString()));
		return status == L"launched" || status == L"already_running";
	}

	bool LaunchRendererViaService()
	{
		coral::input_timing::Scope timing(L"renderer.launch_via_service");
		const auto serviceStart = coral::input_timing::nowUs();
		EnsureServiceRunning();
		coral::input_timing::mark(L"renderer.ensure_service", serviceStart);
		CIpcClient client(CORALPINYINSVC_NAME);
		DWORD session_id = 0;
		if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
		{
			CString sessionIdStr;
			sessionIdStr.Format(L"%u", session_id);
			client.Add(sessionIdStr);
		}
		else return false;
		const auto callStart = coral::input_timing::nowUs();
		const HRESULT hr = client.Call(L"LaunchRenderer");
		coral::input_timing::mark(L"renderer.service_call", callStart, 0, static_cast<unsigned long>(hr));
		if (FAILED(hr))
			return false;
		const CString status = client.Get(L"status");
		return status == L"launched" || status == L"already_running";
	}
}

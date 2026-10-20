#include "coral/product/product_identity.h"
#include <windows.h>
#include "launch_rpc_server.h"
#include "windows_launch_actions.h"
#include "trace_log.h"
#include "text_file_helpers.hpp"

#pragma comment(lib, "advapi32.lib")
namespace coral::service {
namespace {

constexpr const wchar_t* const kServiceName = coral::product::kServiceNameWide;
// 此完全信任服务在共享管道根目录中发布。SearchHost 的
// AppContainer 无法解析会话零中服务的 LOCAL 管道。
constexpr const wchar_t* const kServicePipeName = coral::product::kServiceBridgeWide;

SERVICE_STATUS service_status{};
SERVICE_STATUS_HANDLE status_handle = nullptr;
HANDLE stop_event = nullptr;

void report_status(DWORD current, DWORD win32Exit = NO_ERROR, DWORD waitHintMs = 0)
{
	static DWORD checkpoint = 1;

	service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	service_status.dwCurrentState = current;
	service_status.dwWin32ExitCode = win32Exit;
	service_status.dwWaitHint = waitHintMs;
	service_status.dwControlsAccepted = (current == SERVICE_START_PENDING) ? 0 : SERVICE_ACCEPT_STOP;
	service_status.dwCheckPoint = (current == SERVICE_RUNNING || current == SERVICE_STOPPED) ? 0 : checkpoint++;

	if (status_handle)
	{
		SetServiceStatus(status_handle, &service_status);
	}
}

void WINAPI service_control_handler(DWORD ctrl)
{
	if (ctrl == SERVICE_CONTROL_STOP)
	{
		LOG_INFO_W(L"Received service stop signal");
		report_status(SERVICE_STOP_PENDING, NO_ERROR, 2000);
		if (stop_event)
			SetEvent(stop_event);
	}
}

// 服务入口
void WINAPI service_main(DWORD /*argc*/, LPWSTR* /*argv*/)
{
	LOG_INFO_W(L"========== Service Starting ==========");
	status_handle = RegisterServiceCtrlHandlerW(kServiceName, service_control_handler);
	if (!status_handle)
	{
		LOG_ERROR_W(L"RegisterServiceCtrlHandlerW failed");
		return;
	}
	report_status(SERVICE_START_PENDING, NO_ERROR, 3000);
	stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!stop_event)
	{
		LOG_ERROR_W((L"CreateEventW failed, error code: " + std::to_wstring(GetLastError())));
		report_status(SERVICE_STOPPED, GetLastError());
		return;
	}

	WindowsLaunchActions actions;
	LaunchRpcServer rpc(actions);
	HRESULT hr = rpc.start(kServicePipeName);
	if (FAILED(hr))
	{
		LOG_ERROR_W((L"IPC server initialization failed, HRESULT: " + std::to_wstring(hr)));
		CloseHandle(stop_event);
		stop_event = nullptr;
		report_status(SERVICE_STOPPED, (DWORD)hr);
		return;
	}

	report_status(SERVICE_RUNNING);
	WaitForSingleObject(stop_event, INFINITE);
	rpc.stop();
	CloseHandle(stop_event);
	stop_event = nullptr;
	report_status(SERVICE_STOPPED);
	LOG_INFO_W(L"========== Service Stopped ==========");
}

int run_service()
{
	hlp::initCoralPinyinLog(hlp::LogPathType::AppType);
	
	SERVICE_TABLE_ENTRYW table[] = {
		{ (LPWSTR)kServiceName, service_main },
		{ nullptr, nullptr }
	};
	StartServiceCtrlDispatcherW(table);
	return 0;
}
} // namespace
} // 命名空间 coral::service 结束

int wmain() { return coral::service::run_service(); }

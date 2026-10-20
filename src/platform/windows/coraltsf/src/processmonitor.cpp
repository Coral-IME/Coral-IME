#include "coral/product/product_identity.h"
#include "pch.h"
#include "processmonitor.h"
#include <filesystem>
#include <TlHelp32.h>
#include <windows.h>
#include <wtsapi32.h>
#include <atlpath.h>
#include "coreapi.h"

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "advapi32.lib")

HANDLE g_hEvent = NULL;

// 从注册表中读取 coralpinyin.exe 的路径
std::wstring GetCoralPinyinExePath() {
    HKEY hKey = nullptr;
    LONG lResult = 0;
    DWORD dwType = REG_SZ;
    wchar_t szPath[MAX_PATH] = { 0 };
    DWORD dwSize = sizeof(szPath);

    // 从 HKLM 读取 InstallPath
    lResult = RegOpenKeyEx(HKEY_LOCAL_MACHINE, coral::product::kAppRegistryWide, 0, KEY_READ | KEY_WOW64_32KEY, &hKey);
    if (lResult == ERROR_SUCCESS) {
        lResult = RegQueryValueEx(hKey, coral::product::kInstallPathValueWide, NULL, &dwType, (LPBYTE)szPath, &dwSize);
        RegCloseKey(hKey);

        if (lResult == ERROR_SUCCESS && szPath[0] != L'\0') {
            // 拼接 coralpinyin.exe
            std::wstring installPath(szPath);
            if (installPath.back() != L'\\') {
                installPath += L'\\';
            }
            installPath += coral::product::kSettingsExeWide;
            return installPath;
        }
    }

    return std::wstring();
}

// 检查进程是否在运行的辅助函数
bool IsProcessRunning(const wchar_t* process_name)
{
    // 获取当前进程的 Session ID
    DWORD currentSessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId)) {
        LOG_ERROR("IsProcessRunning: Failed to get current session ID, error: " + std::to_string(GetLastError()));
        return false;
    }

    PROCESSENTRY32 processInfo;
    processInfo.dwSize = sizeof(processInfo);
    HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (processesSnapshot == INVALID_HANDLE_VALUE) {
        LOG_ERROR("IsProcessRunning: Failed to create snapshot, error: " + std::to_string(GetLastError()));
        return false;
    }

    bool found = false;
    if (Process32First(processesSnapshot, &processInfo)) {
        do {
            if (wcsicmp(processInfo.szExeFile, process_name) == 0) {
                // 找到同名进程，检查是否在同一个 Session
                DWORD processSessionId = 0;
                if (ProcessIdToSessionId(processInfo.th32ProcessID, &processSessionId)) {
                    if (processSessionId == currentSessionId) {
                        found = true;
                        break;
                    }
                }
            }
        } while (Process32Next(processesSnapshot, &processInfo));
    }

    CloseHandle(processesSnapshot);
    return found;
}

/*
    循环检查：监控和重启 coralpinyin.exe 的函数，用于独立线程执行，暂时弃用
    如果使用独立守护线程的方案，需要在卸载脚本中[卸载tsf后、杀死coralpinyin.exe前]添加：
        Sleep 1000 ; （1000是线程循环的等待时间）
    确保coralpinyin进程的守护线程已经退出，以免卸载不干净
*/
void MonitorAndRestartCoralPinyin(const std::wstring& exePath) {

    std::vector<wchar_t> directory(exePath.begin(), exePath.end());
    directory.push_back(L'\0'); 
    PathRemoveFileSpec(directory.data());

    while (true) {
        DWORD waitResult = WaitForSingleObject(g_hEvent, 1000);//等待事件或超时，超时时间（也即循环的时间间隔）为1秒

        // 检查 coralpinyin.exe 是否在运行
        if (!IsProcessRunning(coral::product::kSettingsExeWide)) {
            ShellExecute(NULL, L"open", exePath.c_str(), NULL, directory.data(), SW_HIDE);
        }

        // 如果事件被设置，退出循环
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
    }
}

// 检查目标进程是否加载指定 DLL；仅用于现有平台模块诊断
int IsDllLoadedInProcess(DWORD pid, const std::wstring& dllName)
{
    // 获取进程加载的DLL列表
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        // 当前TSF为32位，而目标进程为64位时，CreateToolhelp32Snapshot会失败
		return -1;
    }

	MODULEENTRY32W moduleEntry = { sizeof(moduleEntry) };
	BOOL hasModule = Module32FirstW(hSnapshot, &moduleEntry);
	while (hasModule)
    {
        if (std::filesystem::path(moduleEntry.szModule).filename().wstring() == dllName)
        {
			CloseHandle(hSnapshot);
			return 1;
		}
		hasModule = Module32NextW(hSnapshot, &moduleEntry);
	}

	CloseHandle(hSnapshot);
	return 0;
}

bool isInstalling()
{
	HANDLE hMutex = OpenMutexW(SYNCHRONIZE, FALSE, coral::product::kInstallationMutexWide);
	if (hMutex != NULL){
		CloseHandle(hMutex);
		return true;
	}
	else{
		//uwp应用会访问不到互斥量，但是可以根据错误类型来判断，如果错误类型是ERROR_ACCESS_DENIED，则说明互斥量存在，只是没有权限访问
		DWORD err = GetLastError();
		if(err == ERROR_ACCESS_DENIED){
			return true;
		}
	}
	return false;
}

bool EnsureServiceRunning(const wchar_t* serviceName)
{
	SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (!hSCM) { 
		LOG_ERROR_W(L"OpenSCManager failed: " + std::to_wstring(GetLastError())); 
		return false; 
	}

	SC_HANDLE hService = OpenServiceW(hSCM, serviceName, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG | SERVICE_START);
	if (!hService) { 
		LOG_ERROR_W(L"OpenService failed: " + std::to_wstring(GetLastError())); 
		CloseServiceHandle(hSCM); 
		return false; 
	}

	// 查询服务配置，检查启动类型
	DWORD dwBytesNeeded = 0;
	QueryServiceConfigW(hService, nullptr, 0, &dwBytesNeeded);
	std::vector<BYTE> buffer(dwBytesNeeded);
	LPQUERY_SERVICE_CONFIGW pServiceConfig = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buffer.data());

	if (QueryServiceConfigW(hService, pServiceConfig, dwBytesNeeded, &dwBytesNeeded)) {
		if (pServiceConfig->dwStartType == SERVICE_DISABLED) {
			LOG_ERROR_W(L"EnsureServiceRunning: Service '" + std::wstring(serviceName) + L"' is DISABLED. Cannot start a disabled service");
			CloseServiceHandle(hService);
			CloseServiceHandle(hSCM);
			return false;
		}
	} else {
		LOG_ERROR_W(L"EnsureServiceRunning: Failed to query service config, error code: " + std::to_wstring(GetLastError()));
	}

	// 查询服务状态
	SERVICE_STATUS status{};
	if (!QueryServiceStatus(hService, &status)) {
		LOG_ERROR_W(L"QueryServiceStatus failed: " + std::to_wstring(GetLastError()));
		CloseServiceHandle(hService); 
		CloseServiceHandle(hSCM); 
		return false;
	}

	bool result = true;
	if (status.dwCurrentState != SERVICE_RUNNING) {
		if (!StartServiceW(hService, 0, nullptr)) {
			DWORD err = GetLastError();
			if (err != ERROR_SERVICE_ALREADY_RUNNING) {
				LOG_ERROR_W(L"EnsureServiceRunning: Failed to start service, error code: " + std::to_wstring(err));
				CloseServiceHandle(hService); 
				CloseServiceHandle(hSCM);
				return false;
			}
		}
		// 轮询等待服务启动
		const int maxRetries = 300;
		for (int i = 0; i < maxRetries; ++i) {
			if (QueryServiceStatus(hService, &status) && status.dwCurrentState == SERVICE_RUNNING) {
				result = true;
				break;
			}
			Sleep(100);
		}
		if (status.dwCurrentState != SERVICE_RUNNING) {
			LOG_ERROR_W(L"Timeout waiting for service to start, current state: " + std::to_wstring(status.dwCurrentState));
			result = false;
		}
	}

	CloseServiceHandle(hService);
	CloseServiceHandle(hSCM);
	return result;
}

// 重启 coralpinyin.exe 的函数
void RestartCoralPinyin()
{
	if (isInstalling()) {
		return;  // 安装中，不拉活主进程
	}
    // 检查 coralpinyin.exe 是否在运行
    if (!IsProcessRunning(coral::product::kSettingsExeWide))
    {
        std::wstring exePath = GetCoralPinyinExePath();

        std::vector<wchar_t> directory(exePath.begin(), exePath.end());
        directory.push_back(L'\0');
        PathRemoveFileSpec(directory.data());

		std::wstring cmdLine = L"\"" + exePath + L"\"--startup-type=1";
		std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
		cmdBuf.push_back(L'\0'); // 必须以 \0 结尾

		STARTUPINFO startupInfo = { sizeof(startupInfo) };
		PROCESS_INFORMATION procInfo = {};

		BOOL success = CreateProcess(
			NULL,                  // 应用程序名称
			cmdBuf.data(),         // 命令行
			NULL,                  // 进程安全属性
			NULL,                  // 线程安全属性
			FALSE,                 // 继承句柄
			CREATE_NO_WINDOW,      // 不显示窗口
			NULL,                  // 环境变量
			directory.data(),      // 当前目录
			&startupInfo,          // 启动信息
			&procInfo              // 返回的进程信息
		);
		if (!success) {
			bool ok = Coreapi::LaunchMainProcessViaService();
			if (!ok) {
				LOG_ERROR_W(L"failed Launch coralpinyin.exe By Service");
			}
		} else {
			CloseHandle(procInfo.hProcess);
			CloseHandle(procInfo.hThread);
		}
    }
}

#include "coral/product/product_identity.h"
#include "pch.h"
#include "pinyin.h"
#include "settingmenu.h"
#include "crash_reporter.h"
#include "helper.h"
#include "apphost.h"
#include "online_lexicon_coordinator.h"
#include "update_coordinator.h"
#include "engineserver.h"
#include "ipcclient.h"

//todo 跨平台
#include <windows.h>
#include <filesystem>

namespace
{
void PrelaunchIfMissing(const std::filesystem::path& directory,
	const wchar_t* executableName, const std::wstring& pipeName)
{
	if (::WaitNamedPipeW(pipeName.c_str(), 0)) return;
	const auto executable = directory / executableName;
	if (!std::filesystem::exists(executable)) return;
	std::wstring command = L"\"" + executable.wstring() + L"\"";
	STARTUPINFOW startup{ sizeof(startup) };
	PROCESS_INFORMATION process{};
	if (::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
		FALSE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process))
	{
		::CloseHandle(process.hThread);
		::CloseHandle(process.hProcess);
	}
}

void PrelaunchRuntimeProcesses()
{
	wchar_t modulePath[MAX_PATH] = {};
	if (!::GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath))) return;
	const auto directory = std::filesystem::path(modulePath).parent_path();
	DWORD session_id = 0;
	if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id)) return;
	const auto suffix = std::to_wstring(session_id);
	PrelaunchIfMissing(directory, coral::product::kSessionHostExeWide,
		(std::wstring(L"\\\\.\\pipe\\") + coral::product::kSessionPipePrefixWide) + suffix);
	PrelaunchIfMissing(directory, coral::product::kRendererExeWide,
		coral::product::kRendererPipePrefixWide + suffix);
}
}

const LPCWSTR szPinyinMutexName = L"CORALPINYIN{446574D8-E23F-4956-A2A9-7FB0E5F79080}";

bool _IsMultiProcess() {
	HANDLE hMutex = CreateMutex(nullptr, FALSE, szPinyinMutexName);
	if (hMutex == nullptr) {
		return true;
	}

	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		CIpcClient client(getEngineServerName());
		client.Call(L"showSettingMenu");
		CloseHandle(hMutex);
		return true;
	}
	return false;
}

void initializeConfig()
{
	// 本进程内嵌的 SettingsConfig 副本此前从未以用户配置路径 init（既有缺陷）：
	// 设置界面的 INI 写入实际丢失，只有共享内存通道生效，重启后设置回退。
	// 这里用与 start_runtime 相同的 LocalLow 配置路径初始化，保证 INI 与共享内存双通道一致。
	sConfig->init((hlp::getLocalLowCoralPinyinPath() / "config" / "coralpinyinConfig.ini").string());
}

int main(int argc, char* argv[])
{
	setlocale(LC_ALL, "en_US.UTF-8");
	int startupType = 0; // 默认自动启动
	for (int i = 1; i < argc; i++) {
		if (argv[i] == nullptr) {
			continue;
		}
		std::string arg = argv[i];
		if (arg == "--startup-type=0") {
			startupType = 0;// 自动启动
			break;
		}
		else if (arg == "--startup-type=1") {
			startupType = 1;// 手动启动
			break;
		}
		else if (arg == "--startup-type=2") {
			startupType = 2;// 系统启动
			break;
		}
		else if (arg == "--startup-type=3") {
			startupType = 3;// 安装后启动
			break;
		}
		else if (arg == "-showsetting")
		{
			startupType = 4;//弹出设置窗口
			break;
		}
	}
	LOG_TRACE("CoralPinyin starting with startup type: " + std::to_string(startupType));

	coral::diagnostics::install_crash_reporter(coral::diagnostics::CrashModule::kSettings);
	if (_IsMultiProcess())
	{
		return -1;
	}

	// 公共控件（进度条/组合框等原生控件视觉）
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
	::InitCommonControlsEx(&icc);

	hlp::initCoralPinyinLog(hlp::LogPathType::AppType);

	if (!appInit())
	{
		LOG_ERROR("CoralPinyin: app host window creation failed");
		logShutdown();
		return -1;
	}
	// 安装程序与登录自启动项会在选择输入法之前启动此用户进程。
	PrelaunchRuntimeProcesses();

	HRESULT engineHr = CPinyinModule::Instance().start();
	if (FAILED(engineHr))
		LOG_ERROR("CoralPinyin: engine start failed");

	SettingMenu::getMenu();
	initializeConfig();
    if (!appStartSpeech()) LOG_ERROR("Speech runtime initialization failed");

	if (startupType == 4)
	{
		SettingMenu::getMenu()->showSettingMenu();
	}

	// 更新自动检查由 apphost 定时器驱动；离线版不自动下载细胞词库。

	LOG_TRACE("CoralPinyin: All modules initialized successfully");

	// Win32 消息循环（替代 app.exec()）
	MSG msg;
	while (::GetMessageW(&msg, nullptr, 0, 0) > 0)
	{
        if (appProcessSpeechMessage(msg)) continue;
		::TranslateMessage(&msg);
		::DispatchMessageW(&msg);
	}

    appStopSpeech();
    coral::updates::UpdateCoordinator::instance().stop();
    coral::lexicon_catalog::OnlineLexiconCoordinator::instance().stop();
	CPinyinModule::Instance().term();
	appShutdown();
	logShutdown(); // 短生命周期注意：进程常驻，稳妥起见仍显式关闭
	return 0;
}

#include "pch.h"
#include "apphost.h"
#include "pinyin.h"
#include "clipboardhistory.h"
#include "ui/settingmenu.h"
#include "update_coordinator.h"
#include "speech_runtime.h"

namespace
{
	constexpr UINT WM_APP_RUNTASK = WM_APP + 1;

	// 定时器 ID 与周期（保持原 QTimer 间隔）
	enum TimerId
	{
		TID_MAINTAIN_LEXICON = 1,   // 60s   维护用户词库
		TID_STORE_LEXICON = 2,      // 600s  落盘用户词库
		TID_PLACEHOLDER = 3,        // 3600s 拉取 placeholder 配置
		TID_TODAY_WORD = 4,         // 3600s 上报今日输入字数
		TID_UPDATE_AUTO = 5,        // 每 60s 调度，协调器按持久时间限制为 24h 一次
	};

	constexpr UINT INTERVAL_HOURLY = 3600 * 1000;
	constexpr UINT INTERVAL_UPDATE_FIRST = 60 * 1000;

	HWND g_hAppWnd = nullptr;
    std::unique_ptr<coral::speech::win32::SpeechRuntime> g_speech;
	std::mutex g_taskMutex;
	std::vector<std::function<void()>> g_tasks;

	void runPendingTasks()
	{
		std::vector<std::function<void()>> local;
		{
			std::lock_guard<std::mutex> lock(g_taskMutex);
			local.swap(g_tasks);
		}
		for (auto& task : local)
		{
			try { task(); }
			catch (...) { LOG_ERROR("apphost: task exception"); }
		}
	}

	LRESULT CALLBACK AppWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_APP_RUNTASK:
			runPendingTasks();
			return 0;
		case WM_CLIPBOARDUPDATE:
			ClipboardHistory::instance().onClipboardUpdate();
			return 0;
		case WM_TIMER:
			switch (wParam)
			{
			case TID_MAINTAIN_LEXICON:
				CPinyinModule::Instance().maintainUserLexicon();
				return 0;
			case TID_STORE_LEXICON:
				CPinyinModule::Instance().flush_user_lexicon();
				return 0;
			case TID_PLACEHOLDER:
				SettingMenu::getMenu()->fetchPlaceholderData();
				return 0;
			case TID_TODAY_WORD:
				return 0;
			case TID_UPDATE_AUTO:
				coral::updates::UpdateCoordinator::instance().on_auto_check_timer();
				return 0;
			}
			break;
		case WM_DESTROY:
			ClipboardHistory::instance().detach();
			for (UINT id = TID_MAINTAIN_LEXICON; id <= TID_UPDATE_AUTO; ++id)
				::KillTimer(hWnd, id);
			g_hAppWnd = nullptr;
			return 0;
		}
		return ::DefWindowProc(hWnd, msg, wParam, lParam);
	}
}

bool appInit()
{
	WNDCLASSW wc = { 0 };
	wc.lpfnWndProc = AppWndProc;
	wc.hInstance = ::GetModuleHandleW(nullptr);
	wc.lpszClassName = L"CoralPinyinAppHost";
	::RegisterClassW(&wc);

	// 隐藏消息窗口（不可见，不出现在任务栏）
	g_hAppWnd = ::CreateWindowExW(0, wc.lpszClassName, L"CoralPinyinAppHost",
		WS_OVERLAPPED, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
	if (!g_hAppWnd)
		return false;

	// UserHistoryManager 现在负责紧凑/检查点调度。不要再启用
	// 旧的 60s/600s 宿主定时器，否则两个独立调度器会
	// 竞争刷新同一个变更队列。在 ABI/UI 过渡期，消息分支
	// 仍保留给显式调用者使用。
	::SetTimer(g_hAppWnd, TID_PLACEHOLDER, INTERVAL_HOURLY, nullptr);
	::SetTimer(g_hAppWnd, TID_TODAY_WORD, INTERVAL_HOURLY, nullptr);
	::SetTimer(g_hAppWnd, TID_UPDATE_AUTO, INTERVAL_UPDATE_FIRST, nullptr);
	ClipboardHistory::instance().attach(g_hAppWnd);
	return true;
}

void appShutdown()
{
    appStopSpeech();
	if (g_hAppWnd)
	{
		::DestroyWindow(g_hAppWnd);
		g_hAppWnd = nullptr;
	}
}

bool appStartSpeech()
{
    // 语音输入临时屏蔽：不创建宿主、键盘监听或录音线程，保留代码以便恢复。
    if (!coral::speech::win32::kSpeechFeatureEnabled) return true;
    coral::speech::win32::SpeechServices services;
    services.settings = [] {
        return coral::speech::win32::SpeechSettings{sConfig->get_speech_enabled(), sConfig->get_speech_key_configured(),
            sConfig->get_speech_shortcut(), sConfig->get_speech_credential_revision(), sConfig->get_speech_push_to_talk()};
    };
    services.credentials = [] {
        auto pair = sConfig->get_speech_credentials();
        return coral::speech::TencentCredentials{std::move(pair.first), std::move(pair.second), {}};
    };
    g_speech = std::make_unique<coral::speech::win32::SpeechRuntime>(std::move(services));
    if (!g_speech->start()) { g_speech.reset(); return false; }
    return true;
}
void appStopSpeech() { if (g_speech) { g_speech->stop(); g_speech.reset(); } }
bool appProcessSpeechMessage(MSG& message) { return g_speech && g_speech->process_message(message); }

HWND appGetHwnd()
{
	return g_hAppWnd;
}

void appPostTask(std::function<void()> task)
{
	{
		std::lock_guard<std::mutex> lock(g_taskMutex);
		g_tasks.push_back(std::move(task));
	}
	if (g_hAppWnd)
		::PostMessage(g_hAppWnd, WM_APP_RUNTASK, 0, 0);
}

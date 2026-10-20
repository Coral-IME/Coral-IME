#include "clipboard_monitor.h"

constexpr int RECENTLY_RECORD_SIZE = 20;
CircularList ClipboardMonitor::data_(RECENTLY_RECORD_SIZE);

ClipboardMonitor& get_k_clipboard_monitor_instance()
{
	static ClipboardMonitor instance;
	return instance;
}

ClipboardMonitor::ClipboardMonitor()
{
};

ClipboardMonitor::~ClipboardMonitor()
{

};

std::vector<std::string> ClipboardMonitor::get_recently_data()
{
	return data_.get_recently_data();
}

bool ClipboardMonitor::start()
{
	if (running_)
	{
		return true;
	}
	running_ = true;
	monitor_thread_ = std::thread(&ClipboardMonitor::run, this);
	if (monitor_thread_.joinable())
	{
		return true;
	}
	return false;
}
void ClipboardMonitor::stop()
{
	if (running_)
	{
		running_ = false;
#if defined(_WIN32) || defined(_MSC_VER)
		if (hwnd_)
		{
			PostMessage(hwnd_, WM_CLOSE, 0, 0);
		}
#endif 
		if (monitor_thread_.joinable())
		{
			monitor_thread_.join();
		}
	}
}

void ClipboardMonitor::run()
{
#if defined(_WIN32) || defined(_MSC_VER)
	const wchar_t CLASS_NAME[] = L"ClipboardMonitorClass";
	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	hwnd_ = CreateWindowEx(
		0,
		CLASS_NAME,
		L"Clipboard Monitor",
		0,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		nullptr, nullptr, GetModuleHandle(nullptr), nullptr
	);

	if (hwnd_ == nullptr)
	{
		running_ = false;
		return;
	}

	if (!AddClipboardFormatListener(hwnd_))
	{
		running_ = false;
		return;
	}

	MSG msg = {};
	while (running_ && GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	DestroyWindow(hwnd_);
	hwnd_ = nullptr;
#elif defined(__linux__) || defined(__ANDROID__)
//todo



#endif 
}
#if defined(_WIN32) || defined(_MSC_VER)
LRESULT ClipboardMonitor::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_CLIPBOARDUPDATE:
		if (OpenClipboard(nullptr))
		{
			HANDLE hClipboardData = GetClipboardData(CF_UNICODETEXT);
			if (hClipboardData)
			{
				wchar_t* pwchData = static_cast<wchar_t*>(GlobalLock(hClipboardData));
				if (pwchData)
				{
					std::wstring clipboardText(pwchData);
					GlobalUnlock(hClipboardData);
					data_.push(clipboardText);
				}
			}
			CloseClipboard();
		}
		break;
	case WM_DESTROY:
		RemoveClipboardFormatListener(hwnd);
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
	return 0;
}
#endif

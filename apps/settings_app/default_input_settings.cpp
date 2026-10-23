#include "default_input_settings.h"

#include <UIAutomation.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <chrono>
#include <string>

namespace coral::settings_app
{
namespace
{
using Microsoft::WRL::ComPtr;

bool is_windows_process(DWORD pid, const wchar_t* relative_path)
{
    wchar_t directory[MAX_PATH]{};
    if (!GetWindowsDirectoryW(directory, MAX_PATH)) return false;
    const std::wstring expected = std::wstring(directory) + relative_path;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{};
    DWORD length = 32768;
    const bool matches = QueryFullProcessImageNameW(process, 0, path, &length)
        && _wcsicmp(path, expected.c_str()) == 0;
    CloseHandle(process);
    return matches;
}

bool is_settings_process(DWORD pid)
{
    return is_windows_process(pid, L"\\ImmersiveControlPanel\\SystemSettings.exe");
}

ComPtr<IUIAutomationElement> find_control(IUIAutomation* automation, IUIAutomationElement* root,
    const wchar_t* id)
{
    VARIANT value{};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(id);
    if (!value.bstrVal) return {};
    ComPtr<IUIAutomationCondition> condition;
    const HRESULT created = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    VariantClear(&value);
    ComPtr<IUIAutomationElement> element;
    if (SUCCEEDED(created)) root->FindFirst(TreeScope_Descendants, condition.Get(), &element);
    int pid = 0;
    // 框架窗口可能承载其他应用，必须核对实际控件属于 Windows 设置。
    if (!element || FAILED(element->get_CurrentProcessId(&pid)) || !is_settings_process(pid)) return {};
    return element;
}

struct ComScope
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
};
}

DefaultInputSettings::~DefaultInputSettings() { stop(); }

void DefaultInputSettings::stop()
{
    cancelled_ = true;
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool DefaultInputSettings::start(HWND owner)
{
    if (state() == State::running) return true;
    stop();
    cancelled_ = false;
    state_ = State::running;
    // keyboard-advanced 在 Windows 11 会落到语言和区域；先打开输入页，再调用其导航入口。
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", L"ms-settings:typing",
        nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
        state_ = State::failed;
        return false;
    }
    try {
        worker_ = std::thread([this] {
            try { navigate(); }
            catch (...) { state_ = State::failed; }
        });
    } catch (...) {
        state_ = State::failed;
        return false;
    }
    return true;
}

void DefaultInputSettings::navigate()
{
    ComScope com;
    ComPtr<IUIAutomation2> automation;
    if (FAILED(com.result) || FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)))) {
        state_ = State::failed;
        return;
    }
    automation->put_ConnectionTimeout(500);
    automation->put_TransactionTimeout(500);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    HWND settings_window = nullptr;
    bool invoked = false;
    while (!cancelled_ && std::chrono::steady_clock::now() < deadline) {
        const HWND foreground = GetForegroundWindow();
        if (settings_window && foreground != settings_window) {
            // 包括切换到另一个框架承载的应用；不再操作先前的系统窗口。
            state_ = State::cancelled;
            return;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        if (is_settings_process(pid) || is_windows_process(pid, L"\\System32\\ApplicationFrameHost.exe")) {
            ComPtr<IUIAutomationElement> root;
            if (SUCCEEDED(automation->ElementFromHandle(foreground, &root)) && root) {
                if (invoked && find_control(automation.Get(), root.Get(),
                    L"SystemSettings_Keyboard_DefaultInputMethodList_ComboBox")) {
                    state_ = State::succeeded;
                    return;
                }
                auto link = find_control(automation.Get(), root.Get(),
                    L"SystemSettings_Keyboard_Additional_Settings_DeviceTyping_ButtonEntityItem");
                if (!link) link = find_control(automation.Get(), root.Get(),
                    L"SystemSettings_XLinks_CPL_LanguageAdvancedSettings_Link");
                if (link) {
                    settings_window = foreground;
                    if (!invoked && !cancelled_) {
                        ComPtr<IUIAutomationInvokePattern> action;
                        // 只调用精确标识的页面入口，不发送按键、不修改下拉框或开关。
                        link->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&action));
                        if (!action) {
                            const auto button = find_control(automation.Get(), link.Get(), L"EntityItemButton");
                            if (button) button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&action));
                        }
                        if (cancelled_ || GetForegroundWindow() != foreground) {
                            state_ = State::cancelled;
                            return;
                        }
                        if (!action || FAILED(action->Invoke())) {
                            state_ = State::failed;
                            return;
                        }
                        invoked = true;
                    }
                }
            }
        }
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(100), [this] { return cancelled_.load(); });
    }
    state_ = cancelled_ ? State::cancelled : State::failed;
}
}

#include "speech_runtime.h"
#include "coral/speech/shortcut.h"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace coral::speech::win32 {
namespace {
enum class Stage { kReady, kRecording, kRecognizing, kFinished };
using Microsoft::WRL::ComPtr;
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) return {};
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}
std::wstring failure_text(const Result& result) {
    switch (result.failure) {
    case Failure::kMissingCredentials: return L"请先在语音设置中保存凭据";
    case Failure::kCredentials: return L"语音凭据验证失败，请检查设置";
    case Failure::kTimeout: return L"识别超时，请重新按住快捷键说话";
    case Failure::kNetwork: return L"网络连接失败，请稍后重试";
    case Failure::kInvalidAudio: return L"录音太短或无效，请重新说话";
    case Failure::kRejected: return L"语音服务未完成识别，请检查账户状态";
    default: return L"识别未完成，请重新按住快捷键说话";
    }
}
bool modifiers_down() {
    return ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_MENU) |
        GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
}
bool copy_text(HWND owner, const std::wstring& text) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!memory) return false;
    void* bytes = GlobalLock(memory);
    if (!bytes) { GlobalFree(memory); return false; }
    std::memcpy(bytes, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(memory);
    if (!OpenClipboard(owner)) { GlobalFree(memory); return false; }
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (!copied) GlobalFree(memory);
    return copied;
}
bool protected_control(HWND focus) {
    wchar_t name[64]{};
    GetClassNameW(focus, name, 64);
    const auto style = GetWindowLongPtrW(focus, GWL_STYLE);
    if ((_wcsicmp(name, L"Edit") == 0 || _wcsnicmp(name, L"RichEdit", 8) == 0) &&
        (style & (ES_READONLY | ES_PASSWORD))) return true;
    return false;
}
// 此函数仅在无窗口的识别工作线程调用，UIA 不初始化或改变宿主界面线程的 COM。
bool has_caret(HWND focus, const GUITHREADINFO& gui) {
    if (protected_control(focus)) return false;
    if (gui.hwndCaret && (gui.hwndCaret == focus || IsChild(focus, gui.hwndCaret))) return true;
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)))) return false;
    ComPtr<IUIAutomation2> timed;
    if (SUCCEEDED(automation.As(&timed))) {
        timed->put_ConnectionTimeout(1000);
        timed->put_TransactionTimeout(1000);
    }
    ComPtr<IUIAutomationElement> element;
    if (FAILED(automation->GetFocusedElement(&element)) || !element) return false;
    BOOL password = FALSE;
    if (FAILED(element->get_CurrentIsPassword(&password)) || password) return false;
    ComPtr<IUIAutomationTextPattern2> pattern;
    if (FAILED(element->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&pattern))) || !pattern) return false;
    BOOL active = FALSE;
    ComPtr<IUIAutomationTextRange> range;
    if (FAILED(pattern->GetCaretRange(&active, &range)) || !active || !range) return false;
    VARIANT readonly;
    VariantInit(&readonly);
    const auto status = range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &readonly);
    const bool editable = SUCCEEDED(status) && readonly.vt == VT_BOOL && readonly.boolVal == VARIANT_FALSE;
    VariantClear(&readonly);
    return editable;
}
}
class SpeechRuntime::Impl {
public:
    explicit Impl(SpeechServices services) : services_(std::move(services)) {}
    ~Impl() { stop(); }
    bool start() {
        if (host_) return true;
        if (!services_.settings || !services_.credentials || services_.instance_name.empty() ||
            FindWindowExW(HWND_MESSAGE, nullptr, kSpeechHostClass, services_.instance_name.c_str())) return false;
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = window_proc;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.lpszClassName = kSpeechHostClass;
        RegisterClassW(&cls);
        cls.lpszClassName = kSpeechWindowClass;
        RegisterClassW(&cls);
        host_ = CreateWindowExW(0, kSpeechHostClass, services_.instance_name.c_str(), 0,
            0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, this);
        if (!host_) { stop(); return false; }
        SetTimer(host_, 1, 50, nullptr);
        refresh_settings();
        return true;
    }
    void stop() {
        close();
        stop_keyboard();
        if (worker_.joinable()) worker_.join();
        running_ = false;
        done_ = false;
        if (window_) { DestroyWindow(window_); window_ = nullptr; }
        if (host_) { DestroyWindow(host_); host_ = nullptr; }
    }
    void open(bool microphone_test) {
        // 未设计的大窗和点击录音入口保持屏蔽；检测仍复用不抢焦点的小控件。
        if (!microphone_test || !host_ || running_ || inserting_) return;
        refresh_settings();
        testing_ = true;
        begin();
    }
    bool process_message(MSG& message) {
        if (message.hwnd == window_ && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            close(); return true;
        }
        return false;
    }
    HWND window() const { return window_; }
    bool busy() const { return running_.load() || inserting_; }
private:
    void remember_target() {
        target_ = GetForegroundWindow();
        target_thread_ = GetWindowThreadProcessId(target_, &target_process_);
        GUITHREADINFO gui{sizeof(gui)};
        target_focus_ = target_thread_ && GetGUIThreadInfo(target_thread_, &gui) ? gui.hwndFocus : nullptr;
    }
    void push_to_talk_begin() {
        refresh_settings();
        if (!settings_.enabled || !settings_.configured || running_ || inserting_) return;
        testing_ = false;
        remember_target();
        begin();
    }
    void push_to_talk_end() {
        if (push_to_talk_active_) { push_to_talk_active_ = false; stop_recording_ = true; }
    }
    int scaled(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }
    bool create_window() {
        if (window_) return true;
        const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        dpi_ = target_ ? GetDpiForWindow(target_) : GetDpiForSystem();
        if (!dpi_) dpi_ = 96;
        window_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            kSpeechWindowClass, L"语音输入", WS_POPUP, 0, 0, scaled(56), scaled(56),
            nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (window_) dpi_ = GetDpiForWindow(window_);
        if (previous) SetThreadDpiAwarenessContext(previous);
        return window_ != nullptr;
    }
    void position_window() {
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(target_ ? target_ : GetForegroundWindow(), MONITOR_DEFAULTTONEAREST), &monitor);
        const int width = scaled(toast_ ? 360 : 56), height = scaled(56);
        SetWindowPos(window_, HWND_TOPMOST,
            monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
            monitor.rcWork.bottom - height - scaled(24), width, height, SWP_NOACTIVATE);
        const auto region = CreateRoundRectRgn(0, 0, width + 1, height + 1, scaled(28), scaled(28));
        if (region && !SetWindowRgn(window_, region, TRUE)) DeleteObject(region);
    }
    void show_widget(const std::wstring& status, bool toast) {
        if (!create_window()) return;
        toast_ = toast;
        status_ = status;
        SetWindowTextW(window_, status.c_str());
        position_window();
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        InvalidateRect(window_, nullptr, FALSE);
        toast_deadline_ = toast ? GetTickCount64() + 3200 : 0;
    }
    void notify(const std::wstring& status) {
        stage_ = Stage::kFinished;
        push_to_talk_active_ = false;
        show_widget(status, true);
    }
    void begin() {
        if (worker_.joinable()) worker_.join();
        cancelled_ = false;
        stop_recording_ = false;
        done_ = false;
        inserting_ = false;
        copy_pending_ = false;
        clipboard_retry_ = false;
        clipboard_deadline_ = 0;
        pending_text_.clear();
        target_editable_ = false;
        job_revision_ = settings_.revision;
        job_shortcut_ = settings_.shortcut;
        auto credentials = testing_ ? TencentCredentials{} : services_.credentials();
        if (!testing_ && (credentials.secret_id.empty() || credentials.secret_key.empty())) {
            notify(L"请先在语音设置中完整保存凭据"); return;
        }
        const auto microphone = services_.microphone ? services_.microphone() : query_microphone();
        if (!microphone.available) { notify(microphone.message); return; }
        if (!create_window()) return;
        duration_ = 0;
        level_ = 0;
        stage_ = Stage::kRecording;
        push_to_talk_active_ = !testing_;
        show_widget(testing_ ? L"正在检测麦克风" : L"正在录音，松开快捷键结束", false);
        running_ = true;
        const bool test = testing_;
        try {
            worker_ = std::thread([this, test, target = target_, focus = target_focus_, thread = target_thread_,
                credentials = std::move(credentials)]() mutable {
                Result recognized;
                std::wstring error;
                unsigned peak = 0;
                bool editable = false;
                try {
                    auto capture = services_.capture ? services_.capture : capture_microphone;
                    auto captured = capture(stop_recording_, cancelled_, test ? 5 : 60,
                        [this](CaptureProgress progress) { duration_ = progress.duration_ms; level_ = progress.level; });
                    peak = captured.peak_level;
                    error = std::move(captured.error);
                    if (captured.cancelled || cancelled_) recognized.failure = Failure::kCancelled;
                    else if (error.empty() && !test) {
                        stage_ = Stage::kRecognizing;
                        RequestControl control;
                        control.cancelled = &cancelled_;
                        recognized = services_.recognize ? services_.recognize(captured.audio, credentials, control) :
                            TencentRecognizer(credentials).recognize(captured.audio, control);
                        if (recognized.ok() && !recognized.text.empty() && !cancelled_ && services_.enable_text_insertion) {
                            const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                            if (SUCCEEDED(com)) {
                                GUITHREADINFO gui{sizeof(gui)};
                                if (target && focus && GetForegroundWindow() == target &&
                                    GetGUIThreadInfo(thread, &gui) && gui.hwndFocus == focus)
                                    editable = has_caret(focus, gui);
                                CoUninitialize();
                            }
                        }
                    }
                } catch (...) { recognized.failure = Failure::kInternal; }
                if (!credentials.secret_key.empty()) SecureZeroMemory(credentials.secret_key.data(), credentials.secret_key.size());
                {
                    std::lock_guard<std::mutex> lock(result_mutex_);
                    result_ = std::move(recognized);
                    capture_error_ = std::move(error);
                    peak_ = peak;
                    target_editable_ = editable;
                }
                done_ = true;
            });
        } catch (...) { running_ = false; notify(L"无法启动录音，请重试"); }
    }
    void refresh_settings() {
        const auto next = services_.settings();
        if (!testing_ && (running_ || inserting_ || (window_ && IsWindowVisible(window_))) &&
            (!next.enabled || !next.configured || next.revision != job_revision_ || next.shortcut != job_shortcut_)) close();
        if (next.shortcut != settings_.shortcut) {
            std::lock_guard<std::mutex> lock(keyboard_mutex_);
            tracker_.reset();
            shortcut_ = next.shortcut;
            // 已吞下的键继续吞掉其松开事件，避免中途改配置后弹出开始菜单。
        }
        settings_ = next;
        if (services_.enable_keyboard && settings_.enabled && settings_.configured) start_keyboard();
        else stop_keyboard();
    }
    void tick() {
        refresh_settings();
        if (toast_deadline_ && GetTickCount64() >= toast_deadline_) {
            toast_deadline_ = 0;
            ShowWindow(window_, SW_HIDE);
        }
        if (done_.exchange(false)) {
            if (worker_.joinable()) worker_.join();
            push_to_talk_active_ = false;
            stage_ = Stage::kFinished;
            running_ = false;
            if (cancelled_) return;
            Result result;
            std::wstring error;
            unsigned peak;
            { std::lock_guard<std::mutex> lock(result_mutex_); result = result_; error = capture_error_; peak = peak_; }
            if (!error.empty()) notify(error);
            else if (testing_) notify(peak > 1 ? L"麦克风正常，已检测到声音" : L"声音很小，请检查麦克风音量");
            else if (!result.ok()) notify(failure_text(result));
            else {
                pending_text_ = wide(result.text);
                if (pending_text_.empty()) { notify(L"没有识别到文字，请重新说话"); return; }
                inserting_ = true;
                delivery_deadline_ = GetTickCount64() + 1500;
            }
        }
        if (inserting_) deliver();
        else if (running_ && window_ && !toast_) {
            if (stage_ == Stage::kRecognizing) {
                status_ = L"正在识别";
                SetWindowTextW(window_, status_.c_str());
            }
            InvalidateRect(window_, nullptr, FALSE);
        }
    }
    bool target_current() const {
        DWORD process = 0;
        GUITHREADINFO gui{sizeof(gui)};
        return target_ && target_focus_ && IsWindow(target_) && IsWindow(target_focus_) &&
            GetForegroundWindow() == target_ && GetWindowThreadProcessId(target_, &process) == target_thread_ &&
            process == target_process_ && GetGUIThreadInfo(target_thread_, &gui) && gui.hwndFocus == target_focus_ &&
            IsWindowEnabled(target_focus_) && GetAncestor(target_focus_, GA_ROOT) == target_ &&
            target_editable_ && !protected_control(target_focus_);
    }
    void deliver() {
        if (copy_pending_) { fallback(L"文字已复制到剪贴板"); return; }
        // 不切换前台、不强行恢复焦点；等待修饰键松开后再次核对目标再注入。
        if (modifiers_down() && GetTickCount64() < delivery_deadline_) return;
        const bool can_insert = services_.enable_text_insertion && !modifiers_down() && target_current();
        inserting_ = false;
        if (can_insert) {
            std::vector<INPUT> inputs(pending_text_.size() * 2);
            for (std::size_t i = 0; i < pending_text_.size(); ++i) {
                inputs[i * 2].type = INPUT_KEYBOARD;
                inputs[i * 2].ki.wScan = pending_text_[i];
                inputs[i * 2].ki.dwFlags = KEYEVENTF_UNICODE;
                inputs[i * 2 + 1] = inputs[i * 2];
                inputs[i * 2 + 1].ki.dwFlags |= KEYEVENTF_KEYUP;
            }
            // 工作线程已经检查插入点；提交之前再次复核前台和焦点，防止期间发生切换。
            GUITHREADINFO gui{sizeof(gui)};
            if (GetForegroundWindow() == target_ && GetGUIThreadInfo(target_thread_, &gui) && gui.hwndFocus == target_focus_) {
                const auto count = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
                if (count == inputs.size()) {
                    pending_text_.clear(); ShowWindow(window_, SW_HIDE); return;
                }
                if (count) { fallback(L"部分文字未输入，全文已复制，请核对"); return; }
            }
        }
        fallback(L"文字已复制到剪贴板");
    }
    void fallback(const std::wstring& status) {
        inserting_ = false;
        copy_pending_ = true;
        const bool copied = services_.copy_text ? services_.copy_text(pending_text_) : copy_text(window_, pending_text_);
        if (copied) { clipboard_deadline_ = 0; pending_text_.clear(); notify(status); }
        else {
            // 剪贴板被占用时有限重试；超时保留内存结果，由轻提示提供复制重试。
            if (!clipboard_deadline_) clipboard_deadline_ = GetTickCount64() + 1500;
            if (GetTickCount64() < clipboard_deadline_) inserting_ = true;
            else { clipboard_deadline_ = 0; notify(L"剪贴板不可用，点击此提示重试复制"); clipboard_retry_ = true; toast_deadline_ = 0; }
        }
    }
    void close() {
        push_to_talk_active_ = false;
        cancelled_ = true;
        stop_recording_ = true;
        inserting_ = false;
        clipboard_retry_ = false;
        clipboard_deadline_ = 0;
        pending_text_.clear();
        toast_deadline_ = 0;
        if (window_) ShowWindow(window_, SW_HIDE);
    }
    void paint(HDC dc) {
        RECT rect{}; GetClientRect(window_, &rect);
        HBRUSH background = CreateSolidBrush(RGB(35, 39, 47));
        FillRect(dc, &rect, background); DeleteObject(background);
        if (toast_) {
            HFONT font = CreateFontW(-scaled(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            const auto old = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(250, 250, 250));
            rect.left += scaled(16); rect.right -= scaled(16);
            RECT measured = rect;
            DrawTextW(dc, status_.c_str(), -1, &measured, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
            rect.top = (std::max<LONG>)(scaled(6), (rect.bottom - measured.bottom) / 2);
            rect.bottom -= scaled(6);
            DrawTextW(dc, status_.c_str(), -1, &rect, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
            SelectObject(dc, old); DeleteObject(font);
        } else {
            HPEN pen = CreatePen(PS_SOLID, scaled(2), RGB(255, 141, 119));
            const auto old_pen = SelectObject(dc, pen);
            const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            RoundRect(dc, scaled(23), scaled(12), scaled(33), scaled(31), scaled(10), scaled(10));
            Arc(dc, scaled(18), scaled(19), scaled(38), scaled(38), scaled(18), scaled(25), scaled(38), scaled(25));
            MoveToEx(dc, scaled(28), scaled(37), nullptr); LineTo(dc, scaled(28), scaled(42));
            MoveToEx(dc, scaled(23), scaled(42), nullptr); LineTo(dc, scaled(33), scaled(42));
            SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(pen);
            HBRUSH dot = CreateSolidBrush(RGB(255, 141, 119));
            const auto old_dot = SelectObject(dc, dot); const auto old_line = SelectObject(dc, GetStockObject(NULL_PEN));
            if (stage_ == Stage::kRecognizing) {
                const int x = 20 + static_cast<int>((GetTickCount64() / 200) % 3) * 7;
                Ellipse(dc, scaled(x), scaled(47), scaled(x + 3), scaled(50));
            } else {
                const int width = 4 + static_cast<int>(level_.load() / 5);
                RoundRect(dc, scaled(28 - width / 2), scaled(47), scaled(28 + width / 2), scaled(49), scaled(2), scaled(2));
            }
            SelectObject(dc, old_line); SelectObject(dc, old_dot); DeleteObject(dot);
        }
    }
    void start_keyboard() {
        if (keyboard_thread_.joinable()) return;
        keyboard_ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!keyboard_ready_) return;
        keyboard_stopping_ = false;
        try { keyboard_thread_ = std::thread([this] {
            MSG message{};
            PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
            keyboard_id_ = GetCurrentThreadId();
            keyboard_owner_.store(this);
            const auto hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, GetModuleHandleW(nullptr), 0);
            SetEvent(keyboard_ready_);
            if (hook && !keyboard_stopping_)
                while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
            if (hook) UnhookWindowsHookEx(hook);
            keyboard_owner_.store(nullptr);
        }); } catch (...) { CloseHandle(keyboard_ready_); keyboard_ready_ = nullptr; return; }
        WaitForSingleObject(keyboard_ready_, 2000);
    }
    void stop_keyboard() {
        keyboard_stopping_ = true;
        if (keyboard_thread_.joinable()) {
            if (keyboard_id_) PostThreadMessageW(keyboard_id_, WM_QUIT, 0, 0);
            keyboard_thread_.join();
        }
        keyboard_id_ = 0;
        if (keyboard_ready_) { CloseHandle(keyboard_ready_); keyboard_ready_ = nullptr; }
        std::lock_guard<std::mutex> lock(keyboard_mutex_);
        tracker_.reset();
        swallowed_.fill(false);
    }
    bool keyboard_key(unsigned key, bool down) {
        if (key >= swallowed_.size()) return false;
        const auto& option = speech_shortcut(shortcut_);
        const auto event = tracker_.update(key, down, option);
        if (down && tracker_.active() && (key == VK_LWIN || key == VK_RWIN)) swallowed_[key] = true;
        if (event == PushToTalkEvent::kBegin) {
            if (option.modifiers & 2) {
                swallowed_[VK_LWIN] = tracker_.down(VK_LWIN);
                swallowed_[VK_RWIN] = tracker_.down(VK_RWIN);
            }
            if (option.key) swallowed_[option.key] = true;
            PostMessageW(host_, kSpeechPushToTalkBeginMessage, 0, 0);
        } else if (event == PushToTalkEvent::kEnd) PostMessageW(host_, kSpeechPushToTalkEndMessage, 0, 0);
        else if (event == PushToTalkEvent::kCancel) PostMessageW(host_, kSpeechCancelMessage, 0, 0);
        const bool swallow = swallowed_[key];
        if (!down) swallowed_[key] = false;
        return swallow;
    }
    static LRESULT CALLBACK keyboard_proc(int code, WPARAM message, LPARAM data) {
        auto* self = keyboard_owner_.load();
        if (code == HC_ACTION && self) {
            const auto& key = *reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
            if (!(key.flags & LLKHF_INJECTED)) {
                std::lock_guard<std::mutex> lock(self->keyboard_mutex_);
                if (self->keyboard_key(key.vkCode, message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) return 1;
            }
        }
        return CallNextHookEx(nullptr, code, message, data);
    }
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wparam, lparam);
        if (message == kSpeechOpenMessage) return 0;
        if (message == kSpeechTestMessage) { self->open(true); return 0; }
        if (message == kSpeechPushToTalkBeginMessage) { self->push_to_talk_begin(); return 0; }
        if (message == kSpeechPushToTalkEndMessage) { self->push_to_talk_end(); return 0; }
        if (message == kSpeechCancelMessage || message == WM_CLOSE) { self->close(); return 0; }
        if (message == WM_TIMER && window == self->host_) { self->tick(); return 0; }
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_NCHITTEST) return self->clipboard_retry_ ? HTCLIENT : HTTRANSPARENT;
        if (message == WM_LBUTTONUP && self->clipboard_retry_) {
            self->refresh_settings();
            if (!self->clipboard_retry_ || self->pending_text_.empty()) return 0;
            self->clipboard_retry_ = false;
            self->clipboard_deadline_ = 0;
            self->inserting_ = true;
            self->delivery_deadline_ = 0;
            self->fallback(L"文字已复制到剪贴板"); return 0;
        }
        if (message == WM_DPICHANGED && window == self->window_) {
            self->dpi_ = HIWORD(wparam); self->position_window(); return 0;
        }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); self->paint(dc); EndPaint(window, &paint); return 0;
        }
        if (message == WM_PRINTCLIENT) { self->paint(reinterpret_cast<HDC>(wparam)); return 0; }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    SpeechServices services_;
    SpeechSettings settings_;
    HWND host_ = nullptr, window_ = nullptr, target_ = nullptr, target_focus_ = nullptr;
    DWORD target_thread_ = 0, target_process_ = 0;
    UINT dpi_ = 96;
    bool testing_ = false, inserting_ = false, toast_ = false;
    bool push_to_talk_active_ = false, clipboard_retry_ = false, copy_pending_ = false;
    ULONGLONG toast_deadline_ = 0, delivery_deadline_ = 0, clipboard_deadline_ = 0;
    std::wstring status_, pending_text_;
    std::string job_revision_, job_shortcut_;
    std::thread worker_;
    std::atomic<bool> running_{false}, done_{false}, stop_recording_{false}, cancelled_{false};
    std::atomic<Stage> stage_{Stage::kReady};
    std::atomic<unsigned> duration_{0}, level_{0};
    std::mutex result_mutex_;
    Result result_;
    std::wstring capture_error_;
    unsigned peak_ = 0;
    bool target_editable_ = false;
    std::thread keyboard_thread_;
    std::atomic<DWORD> keyboard_id_{0};
    HANDLE keyboard_ready_ = nullptr;
    std::atomic<bool> keyboard_stopping_{false};
    std::mutex keyboard_mutex_;
    PushToTalkTracker tracker_;
    std::string shortcut_ = "ctrl_win_shift";
    std::array<bool, 256> swallowed_{};
    static inline std::atomic<Impl*> keyboard_owner_{nullptr};
};
SpeechRuntime::SpeechRuntime(SpeechServices services) : impl_(std::make_unique<Impl>(std::move(services))) {}
SpeechRuntime::~SpeechRuntime() = default;
bool SpeechRuntime::start() { return impl_->start(); }
void SpeechRuntime::stop() { impl_->stop(); }
void SpeechRuntime::open(bool microphone_test) { impl_->open(microphone_test); }
bool SpeechRuntime::process_message(MSG& message) { return impl_->process_message(message); }
HWND SpeechRuntime::window() const { return impl_->window(); }
bool SpeechRuntime::busy() const { return impl_->busy(); }
}

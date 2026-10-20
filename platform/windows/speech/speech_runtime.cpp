#include "speech_runtime.h"
#include "microphone_capture.h"
#include "coral/speech/shortcut.h"
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>

namespace coral::speech::win32 {
namespace {
enum Control : int { kStatus = 0x6100, kMeter, kTime, kText, kPrimary, kInsert, kCopy, kCancel };
enum class Stage { kReady, kRecording, kRecognizing, kFinished };
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) return {};
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}
std::wstring failure_text(const Result& result) {
    switch (result.failure) {
    case Failure::kCredentials: return L"腾讯云凭据验证失败，请检查 SecretId 和 SecretKey。";
    case Failure::kMissingCredentials: return L"请先在设置中完整保存语音输入凭据。";
    case Failure::kTimeout: return L"识别超时，录音未自动重试，请重新录音。";
    case Failure::kNetwork: return L"网络连接失败，请检查网络后重新录音。";
    case Failure::kCancelled: return L"已取消本次录音或识别。";
    case Failure::kInvalidAudio: return L"录音数据无效，请稍等片刻再结束录音。";
    case Failure::kRejected:
        if (result.provider_code == "FailedOperation.UserNotRegistered") return L"请先在腾讯云控制台开通语音识别服务。";
        if (result.provider_code.find("Amount") != std::string::npos || result.provider_code.find("Isolate") != std::string::npos)
            return L"腾讯云语音服务额度不足或已停用，请检查账户状态。";
        return L"腾讯云未完成识别，请检查服务状态后重试。";
    default: return L"识别未完成，请重新录音。";
    }
}
std::wstring edited_text(HWND window) {
    const auto length = GetWindowTextLengthW(GetDlgItem(window, kText));
    if (length <= 0 || length > 64000) return {};
    std::wstring text(length + 1, L'\0');
    GetDlgItemTextW(window, kText, text.data(), static_cast<int>(text.size()));
    text.resize(length);
    return text;
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
        host_ = CreateWindowExW(0, kSpeechHostClass, services_.instance_name.c_str(), 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, this);
        if (!host_) return false;
        SetTimer(host_, 1, 500, nullptr);
        SetTimer(host_, 2, 100, nullptr);
        refresh_settings();
        return true;
    }
    void stop() {
        push_to_talk_active_ = false;
        cancelled_ = true;
        stop_recording_ = true;
        stop_keyboard();
        if (worker_.joinable()) worker_.join();
        if (window_) { DestroyWindow(window_); window_ = nullptr; }
        if (host_) { DestroyWindow(host_); host_ = nullptr; }
        if (font_) { DeleteObject(font_); font_ = nullptr; }
        if (background_) { DeleteObject(background_); background_ = nullptr; }
    }
    void open(bool microphone_test) {
        if (!host_) return;
        refresh_settings();
        if (window_ && IsWindowVisible(window_)) {
            if (!microphone_test && !testing_ && stage_ == Stage::kRecording) stop_recording_ = true;
            SetForegroundWindow(window_);
            return;
        }
        if (running_) return;
        if (worker_.joinable()) worker_.join();
        testing_ = microphone_test;
        job_revision_ = settings_.revision;
        remember_target();
        ready_text_.clear();
        if (!microphone_test && !settings_.enabled) ready_text_ = L"请先在设置 → 语音输入中打开开关。";
        auto credentials = microphone_test ? TencentCredentials{} : services_.credentials();
        if (!microphone_test && settings_.enabled && (credentials.secret_id.empty() || credentials.secret_key.empty()))
            ready_text_ = L"请先在设置 → 语音输入中保存 SecretId 和 SecretKey。";
        const auto microphone = services_.microphone ? services_.microphone() : query_microphone();
        if (ready_text_.empty() && !microphone.available) ready_text_ = microphone.message;
        if (!create_window()) return;
        ShowWindow(window_, SW_SHOWNORMAL);
        SetForegroundWindow(window_);
        if (!ready_text_.empty()) {
            stage_ = Stage::kReady;
            SetDlgItemTextW(window_, kStatus, ready_text_.c_str());
            SetDlgItemTextW(window_, kPrimary, L"重试");
            EnableWindow(GetDlgItem(window_, kPrimary), TRUE);
        } else begin(std::move(credentials));
    }
    bool process_message(MSG& message) {
        if (!window_ || !IsWindowVisible(window_) ||
            (message.hwnd != window_ && !IsChild(window_, message.hwnd))) return false;
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { close(); return true; }
        return IsDialogMessageW(window_, &message) != FALSE;
    }
    HWND window() const { return window_; }
    bool busy() const { return running_.load(); }
private:
    // 记录按下发言开始前的输入目标；小窗自身在前台时保留上一次目标。
    void remember_target() {
        const auto foreground = GetForegroundWindow();
        if (window_ && (foreground == window_ || IsChild(window_, foreground))) return;
        target_ = foreground;
        target_thread_ = GetWindowThreadProcessId(target_, &target_process_);
        GUITHREADINFO gui{ sizeof(gui) };
        target_focus_ = GetGUIThreadInfo(target_thread_, &gui) ? gui.hwndFocus : nullptr;
    }
    // 按下发言开始：首次按下打开既有小窗并立即录音，结果窗仍打开时开始下一段。
    void push_to_talk_begin() {
        refresh_settings();
        if (!settings_.push_to_talk || !settings_.enabled || !settings_.configured) return;
        if (push_to_talk_active_ || running_) return;
        if (!window_ || !IsWindowVisible(window_)) {
            open(false);
        } else {
            testing_ = false;
            remember_target();
            begin(services_.credentials());
        }
        push_to_talk_active_ = stage_ == Stage::kRecording;
    }
    // 松开任一快捷键：结束采集并交给既有识别流程；关闭或停用后迟到事件不再生效。
    void push_to_talk_end() {
        if (!push_to_talk_active_) return;
        push_to_talk_active_ = false;
        if (window_ && IsWindowVisible(window_) && stage_ == Stage::kRecording) stop_recording_ = true;
    }
    bool create_window() {
        if (window_) {
            SetDlgItemTextW(window_, kText, L"");
            EnableWindow(GetDlgItem(window_, kInsert), FALSE);
            EnableWindow(GetDlgItem(window_, kCopy), FALSE);
            return true;
        }
        const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        dpi_ = target_ ? GetDpiForWindow(target_) : GetDpiForSystem();
        if (!dpi_) dpi_ = 96;
        RECT size{0, 0, scaled(480), scaled(330)};
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
        const DWORD extended = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        AdjustWindowRectExForDpi(&size, style, FALSE, extended, dpi_);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(target_, MONITOR_DEFAULTTONEAREST), &monitor);
        const int width = size.right - size.left, height = size.bottom - size.top;
        window_ = CreateWindowExW(extended, kSpeechWindowClass, L"CORAL 语音输入", style,
            monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
            monitor.rcWork.bottom - height - scaled(36), width, height,
            nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (window_) {
            dpi_ = GetDpiForWindow(window_);
            RECT actual{0,0,scaled(480),scaled(330)};
            AdjustWindowRectExForDpi(&actual,style,FALSE,extended,dpi_);
            const int actual_width=actual.right-actual.left,actual_height=actual.bottom-actual.top;
            SetWindowPos(window_,nullptr,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-actual_width)/2,
                monitor.rcWork.bottom-actual_height-scaled(36),actual_width,actual_height,SWP_NOZORDER|SWP_NOACTIVATE);
        }
        if (previous) SetThreadDpiAwarenessContext(previous);
        return window_ != nullptr;
    }
    int scaled(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }
    void make_controls(HWND window) {
        dpi_ = GetDpiForWindow(window);
        auto child = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
            CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text,
                WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        };
        child(L"STATIC", L"", 0, kStatus);
        child(PROGRESS_CLASSW, L"", PBS_SMOOTH, kMeter);
        child(L"STATIC", L"", 0, kTime);
        child(L"EDIT", L"", ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, kText);
        SendDlgItemMessageW(window, kText, EM_SETLIMITTEXT, 64000, 0);
        child(L"BUTTON", L"完成并识别", BS_DEFPUSHBUTTON | WS_TABSTOP, kPrimary);
        child(L"BUTTON", L"输入文字", BS_PUSHBUTTON | WS_TABSTOP, kInsert);
        child(L"BUTTON", L"复制", BS_PUSHBUTTON | WS_TABSTOP, kCopy);
        child(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, kCancel);
        EnableWindow(GetDlgItem(window, kInsert), FALSE);
        EnableWindow(GetDlgItem(window, kCopy), FALSE);
        layout(window);
    }
    void layout(HWND window) {
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-scaled(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        auto place = [&](int id, int x, int y, int w, int h) {
            const auto child = GetDlgItem(window, id);
            MoveWindow(child, scaled(x), scaled(y), scaled(w), scaled(h), TRUE);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        };
        place(kStatus, 24, 24, 432, 46);
        place(kMeter, 24, 83, 300, 10);
        place(kTime, 338, 78, 118, 26);
        place(kText, 24, 120, 432, 124);
        place(kPrimary, 24, 268, 130, 36);
        place(kInsert, 166, 268, 100, 36);
        place(kCopy, 278, 268, 80, 36);
        place(kCancel, 370, 268, 86, 36);
    }
    void begin(TencentCredentials credentials) {
        if (running_) return;
        if (worker_.joinable()) worker_.join();
        cancelled_ = false;
        stop_recording_ = false;
        running_ = true;
        done_ = false;
        duration_ = 0;
        level_ = 0;
        stage_ = Stage::kRecording;
        job_revision_ = settings_.revision;
        SetDlgItemTextW(window_, kStatus, testing_ ? L"正在检测麦克风，请说句话，观察音量条。" :
            (settings_.push_to_talk ? L"正在聆听…松开 Ctrl + Win 结束并识别。" : L"正在聆听…说完后点击“完成并识别”。"));
        SetDlgItemTextW(window_, kPrimary, testing_ ? L"结束检测" :
            (settings_.push_to_talk ? L"结束并识别" : L"完成并识别"));
        SetDlgItemTextW(window_, kText, L"");
        EnableWindow(GetDlgItem(window_, kPrimary), TRUE);
        EnableWindow(GetDlgItem(window_, kInsert), FALSE);
        EnableWindow(GetDlgItem(window_, kCopy), FALSE);
        const bool test = testing_;
        try {
            worker_ = std::thread([this, test, credentials = std::move(credentials)]() mutable {
                Result recognized;
                std::wstring error;
                unsigned peak = 0;
                try {
                    auto capture = services_.capture ? services_.capture : capture_microphone;
                    auto captured = capture(stop_recording_, cancelled_, test ? 5 : 60,
                        [this](CaptureProgress progress) { duration_ = progress.duration_ms; level_ = progress.level; });
                    peak = captured.peak_level;
                    error = std::move(captured.error);
                    if (captured.cancelled) recognized.failure = Failure::kCancelled;
                    else if (error.empty() && !test) {
                        stage_ = Stage::kRecognizing;
                        RequestControl control;
                        control.cancelled = &cancelled_;
                        recognized = services_.recognize ? services_.recognize(captured.audio, credentials, control) :
                            TencentRecognizer(credentials).recognize(captured.audio, control);
                    }
                } catch (...) { recognized.failure = Failure::kInternal; }
                // 凭据仅在本次工作线程内持有，不通过窗口消息或日志传递。
                if (!credentials.secret_key.empty()) SecureZeroMemory(credentials.secret_key.data(), credentials.secret_key.size());
                {
                    std::lock_guard<std::mutex> lock(result_mutex_);
                    result_ = std::move(recognized);
                    capture_error_ = std::move(error);
                    peak_ = peak;
                }
                running_ = false;
                done_ = true;
            });
        } catch (...) {
            running_ = false;
            stage_ = Stage::kReady;
            SetDlgItemTextW(window_, kStatus, L"无法启动录音任务，请重试。");
        }
    }
    void refresh_settings() {
        const auto next = services_.settings();
        if (!testing_ && window_ && IsWindowVisible(window_) && (!next.enabled || next.revision != job_revision_)) close();
        if (next.shortcut != settings_.shortcut || next.push_to_talk != settings_.push_to_talk) {
            std::lock_guard<std::mutex> lock(keyboard_mutex_);
            tracker_.reset();
            push_to_talk_tracker_.reset();
            shortcut_ = next.shortcut;
            push_to_talk_ = next.push_to_talk;
            swallow_win_ = false;
        }
        // 录音期间关闭按下发言：结束当前采集并走既有识别流程，避免只能等超时。
        if (!next.push_to_talk && push_to_talk_active_) {
            push_to_talk_active_ = false;
            if (window_ && IsWindowVisible(window_) && stage_ == Stage::kRecording) stop_recording_ = true;
        }
        settings_ = next;
        if (services_.enable_keyboard && settings_.enabled && settings_.configured) start_keyboard();
        else stop_keyboard();
    }
    void tick() {
        // 展示结果之前重新读取开关和凭据版本，封住配置轮询间隔内的迟到结果。
        refresh_settings();
        if (!window_ || !IsWindowVisible(window_)) {
            if (done_.exchange(false) && worker_.joinable()) worker_.join();
            return;
        }
        const auto stage = stage_.load();
        SendDlgItemMessageW(window_, kMeter, PBM_SETPOS, level_.load(), 0);
        const auto duration = duration_.load();
        const auto time = std::to_wstring(duration / 1000) + L" / " + (testing_ ? L"5 秒" : L"60 秒");
        SetDlgItemTextW(window_, kTime, time.c_str());
        if (stage == Stage::kRecognizing) {
            SetDlgItemTextW(window_, kStatus, L"录音已结束，正在识别…");
            EnableWindow(GetDlgItem(window_, kPrimary), FALSE);
        }
        if (!done_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
        stage_ = Stage::kFinished;
        Result result;
        std::wstring capture_error;
        unsigned peak;
        { std::lock_guard<std::mutex> lock(result_mutex_); result = result_; capture_error = capture_error_; peak = peak_; }
        SetDlgItemTextW(window_, kPrimary, testing_ ? L"再次检测" : L"重新录音");
        EnableWindow(GetDlgItem(window_, kPrimary), TRUE);
        if (cancelled_) return;
        if (!capture_error.empty()) SetDlgItemTextW(window_, kStatus, capture_error.c_str());
        else if (testing_) SetDlgItemTextW(window_, kStatus, peak > 1 ? L"麦克风正常，已检测到声音。检测录音不会上传。" : L"麦克风可打开，但声音很小。请检查静音、音量或输入设备。");
        else if (!result.ok()) SetDlgItemTextW(window_, kStatus, failure_text(result).c_str());
        else {
            const auto text = wide(result.text);
            SetDlgItemTextW(window_, kText, text.c_str());
            SetDlgItemTextW(window_, kStatus, text.empty() ? L"没有识别到文字，请重新录音。" : L"识别完成，可修改文字后输入到原窗口，或复制。 ");
            EnableWindow(GetDlgItem(window_, kInsert), !text.empty() && target_ && target_focus_);
            EnableWindow(GetDlgItem(window_, kCopy), !text.empty());
        }
    }
    void close() {
        push_to_talk_active_ = false;
        cancelled_ = true;
        stop_recording_ = true;
        inserting_ = false;
        if (window_) { KillTimer(window_, 3); ShowWindow(window_, SW_HIDE); }
    }
    void retry() {
        refresh_settings();
        if (!testing_ && !settings_.enabled) {
            SetDlgItemTextW(window_, kStatus, L"请先在设置 → 语音输入中打开开关。"); return;
        }
        auto credentials = testing_ ? TencentCredentials{} : services_.credentials();
        if (!testing_ && (credentials.secret_id.empty() || credentials.secret_key.empty())) {
            SetDlgItemTextW(window_, kStatus, L"请先在设置中完整保存 SecretId 和 SecretKey。"); return;
        }
        begin(std::move(credentials));
    }
    void copy() {
        const auto text = edited_text(window_);
        if (text.empty()) return;
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (!memory) return;
        void* destination = GlobalLock(memory);
        if (!destination) { GlobalFree(memory); return; }
        memcpy(destination, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(memory);
        if (!OpenClipboard(window_)) { GlobalFree(memory); SetDlgItemTextW(window_, kStatus, L"剪贴板暂时不可用，请稍后重试。"); return; }
        const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
        CloseClipboard();
        if (!copied) GlobalFree(memory);
        SetDlgItemTextW(window_, kStatus, copied ? L"文字已复制。" : L"复制失败，请稍后重试。");
    }
    void insert() {
        const auto current = services_.settings();
        if (!current.enabled || current.revision != job_revision_) { close(); return; }
        if (running_ || inserting_ || edited_text(window_).empty()) return;
        if ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_MENU) |
            GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) {
            SetDlgItemTextW(window_, kStatus, L"请先松开快捷键，再点击“输入文字”。"); return;
        }
        DWORD process = 0;
        if (!IsWindow(target_) || !IsWindow(target_focus_) ||
            GetWindowThreadProcessId(target_, &process) != target_thread_ || process != target_process_ ||
            GetAncestor(target_focus_, GA_ROOT) != target_) {
            SetDlgItemTextW(window_, kStatus, L"原输入窗口已变化，请使用“复制”后手动粘贴。"); return;
        }
        if (!SetForegroundWindow(target_)) { SetDlgItemTextW(window_, kStatus, L"无法返回原窗口，请使用“复制”。"); return; }
        inserting_ = true;
        SetTimer(window_, 3, 100, nullptr);
    }
    void finish_insert() {
        KillTimer(window_, 3);
        inserting_ = false;
        const auto current = services_.settings();
        if (!current.enabled || current.revision != job_revision_) { close(); return; }
        if ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_MENU) |
            GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) {
            SetDlgItemTextW(window_, kStatus, L"请先松开快捷键，再点击“输入文字”。"); return;
        }
        DWORD process = 0;
        GUITHREADINFO gui{sizeof(gui)};
        if (GetForegroundWindow() != target_ || GetWindowThreadProcessId(target_, &process) != target_thread_ ||
            process != target_process_ || !GetGUIThreadInfo(target_thread_, &gui) || gui.hwndFocus != target_focus_) {
            SetDlgItemTextW(window_, kStatus, L"原输入位置已变化，请使用“复制”后手动粘贴。"); return;
        }
        const auto text = edited_text(window_);
        if (text.empty()) return;
        std::vector<INPUT> inputs(text.size() * 2);
        for (std::size_t i = 0; i < text.size(); ++i) {
            inputs[i * 2].type = INPUT_KEYBOARD;
            inputs[i * 2].ki.wScan = text[i];
            inputs[i * 2].ki.dwFlags = KEYEVENTF_UNICODE;
            inputs[i * 2 + 1] = inputs[i * 2];
            inputs[i * 2 + 1].ki.dwFlags |= KEYEVENTF_KEYUP;
        }
        if (SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size()) close();
        else SetDlgItemTextW(window_, kStatus, L"未能完整输入，可能受窗口权限限制。请核对原窗口，必要时手动复制。");
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
            if (hook && !keyboard_stopping_) {
                while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
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
        push_to_talk_tracker_.reset();
        swallowed_key_ = 0;
        swallow_win_ = false;
    }
    // 返回 true 表示吞掉该按键；按住 Win 时吞掉其松开，避免弹出开始菜单。
    bool push_to_talk_key(unsigned key, bool down) {
        const bool win_key = key == VK_LWIN || key == VK_RWIN;
        if (win_key && swallow_win_) {
            const auto event = push_to_talk_tracker_.update(key, down);
            if (event == PushToTalkEvent::kEnd) PostMessageW(host_, kSpeechPushToTalkEndMessage, 0, 0);
            if (!down && !push_to_talk_tracker_.down(VK_LWIN) && !push_to_talk_tracker_.down(VK_RWIN))
                swallow_win_ = false;
            return true;
        }
        const auto event = push_to_talk_tracker_.update(key, down);
        if (event == PushToTalkEvent::kBegin) {
            if (win_key || push_to_talk_tracker_.down(VK_LWIN) || push_to_talk_tracker_.down(VK_RWIN))
                swallow_win_ = true;
            PostMessageW(host_, kSpeechPushToTalkBeginMessage, 0, 0);
        } else if (event == PushToTalkEvent::kEnd) {
            PostMessageW(host_, kSpeechPushToTalkEndMessage, 0, 0);
        }
        return win_key && swallow_win_;
    }
    static LRESULT CALLBACK keyboard_proc(int code, WPARAM message, LPARAM data) {
        auto* self = keyboard_owner_.load();
        if (code == HC_ACTION && self) {
            const auto& key = *reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
            if (!(key.flags & LLKHF_INJECTED)) {
                const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
                std::lock_guard<std::mutex> lock(self->keyboard_mutex_);
                if (self->push_to_talk_) {
                    if (self->push_to_talk_key(key.vkCode, down)) return 1;
                } else {
                    const auto& option = speech_shortcut(self->shortcut_);
                    const bool fire = self->tracker_.update(key.vkCode, down, option);
                    if (fire) PostMessageW(self->host_, kSpeechOpenMessage, 0, 0);
                    if (fire && option.key) { self->swallowed_key_ = key.vkCode; return 1; }
                    if (self->swallowed_key_ == key.vkCode) {
                        if (!down) self->swallowed_key_ = 0;
                        return 1;
                    }
                }
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
        if (message == kSpeechOpenMessage || message == kSpeechTestMessage) { self->open(message == kSpeechTestMessage); return 0; }
        if (message == kSpeechPushToTalkBeginMessage) { self->push_to_talk_begin(); return 0; }
        if (message == kSpeechPushToTalkEndMessage) { self->push_to_talk_end(); return 0; }
        if (message == WM_CREATE && reinterpret_cast<CREATESTRUCTW*>(lparam)->hwndParent != HWND_MESSAGE) { self->make_controls(window); return 0; }
        if (message == WM_TIMER) {
            if (window == self->host_) { if (wparam == 1) self->refresh_settings(); else self->tick(); }
            else if (wparam == 3) self->finish_insert();
            return 0;
        }
        if (message == WM_COMMAND) {
            switch (LOWORD(wparam)) {
            case kPrimary:
                if (self->stage_ == Stage::kRecording) self->stop_recording_ = true;
                else self->retry();
                return 0;
            case kInsert: self->insert(); return 0;
            case kCopy: self->copy(); return 0;
            case kCancel: self->close(); return 0;
            }
        }
        if (message == WM_PRINTCLIENT) {
            if (!self->background_) self->background_ = CreateSolidBrush(RGB(255,249,246));
            RECT rect{}; GetClientRect(window,&rect);
            FillRect(reinterpret_cast<HDC>(wparam),&rect,self->background_); return 0;
        }
        if (message == WM_CLOSE) { self->close(); return 0; }
        if (message == WM_KEYDOWN && wparam == VK_ESCAPE) { self->close(); return 0; }
        if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN || message == WM_ERASEBKGND) {
            if (!self->background_) self->background_ = CreateSolidBrush(RGB(255,249,246));
            if (message == WM_ERASEBKGND) {
                RECT rect{}; GetClientRect(window, &rect);
                FillRect(reinterpret_cast<HDC>(wparam), &rect, self->background_); return 1;
            }
            SetBkColor(reinterpret_cast<HDC>(wparam), RGB(255,249,246));
            SetTextColor(reinterpret_cast<HDC>(wparam), RGB(48,36,31));
            return reinterpret_cast<LRESULT>(self->background_);
        }
        if (message == WM_DPICHANGED && window != self->host_) {
            self->dpi_ = HIWORD(wparam);
            const auto& rect = *reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
            self->layout(window); return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    SpeechServices services_;
    SpeechSettings settings_;
    HWND host_ = nullptr, window_ = nullptr, target_ = nullptr, target_focus_ = nullptr;
    DWORD target_thread_ = 0, target_process_ = 0;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HBRUSH background_ = nullptr;
    bool testing_ = false, inserting_ = false;
    bool push_to_talk_active_ = false;
    std::wstring ready_text_;
    std::string job_revision_;
    std::thread worker_;
    std::atomic<bool> running_{false}, done_{false}, stop_recording_{false}, cancelled_{false};
    std::atomic<Stage> stage_{Stage::kReady};
    std::atomic<unsigned> duration_{0}, level_{0};
    std::mutex result_mutex_;
    Result result_;
    std::wstring capture_error_;
    unsigned peak_ = 0;
    std::thread keyboard_thread_;
    std::atomic<DWORD> keyboard_id_{0};
    HANDLE keyboard_ready_ = nullptr;
    std::atomic<bool> keyboard_stopping_{false};
    std::mutex keyboard_mutex_;
    ShortcutTracker tracker_;
    PushToTalkTracker push_to_talk_tracker_;
    std::string shortcut_ = "ctrl_win_shift";
    // 以下两项只在 keyboard_mutex_ 保护下读写。
    bool push_to_talk_ = false;
    bool swallow_win_ = false;
    unsigned swallowed_key_ = 0;
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

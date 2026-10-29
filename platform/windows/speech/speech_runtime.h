#ifndef CORAL_PLATFORM_WINDOWS_SPEECH_SPEECH_RUNTIME_H_INCLUDED
#define CORAL_PLATFORM_WINDOWS_SPEECH_SPEECH_RUNTIME_H_INCLUDED
#include "coral/speech/tencent_recognizer.h"
#include "microphone_capture.h"
#include "test_environment.h"
#include <functional>
#include <memory>
#include <string>
#include <windows.h>
namespace coral::speech::win32 {
// 开发期开放语音；以后改为 false 并重新构建即可统一屏蔽。
inline constexpr bool kSpeechFeatureEnabled = true;
// 大窗及未经设计的悬浮栏点击入口暂不开放。
inline constexpr bool kSpeechToolbarEnabled = false;
inline constexpr wchar_t kSpeechHostClass[] = L"CoralPinyin.SpeechHost.v1";
inline constexpr wchar_t kSpeechWindowClass[] = L"CoralPinyin.SpeechWindow.v1";
inline constexpr wchar_t kSpeechHostTitle[] = L"CoralSpeech";
inline std::wstring speech_host_title() {
    const auto name = coral::testing::objectName("CoralSpeech");
    return std::wstring(name.begin(), name.end());
}
inline constexpr UINT kSpeechOpenMessage = WM_APP + 0x521;
inline constexpr UINT kSpeechTestMessage = WM_APP + 0x522;
inline constexpr UINT kSpeechPushToTalkBeginMessage = WM_APP + 0x523;
inline constexpr UINT kSpeechPushToTalkEndMessage = WM_APP + 0x524;
inline constexpr UINT kSpeechCancelMessage = WM_APP + 0x525;
struct SpeechSettings {
    bool enabled = false;
    bool configured = false;
    std::string shortcut = "ctrl_win_shift";
    std::string revision;
};
struct SpeechServices {
    std::wstring instance_name = speech_host_title();
    bool enable_keyboard = true;
    // 自动测试可禁止真实键盘注入，只检查隔离的剪贴板替身。
    bool enable_text_insertion = true;
    std::function<SpeechSettings()> settings;
    std::function<TencentCredentials()> credentials;
    // 测试可替换识别后端，默认使用真实腾讯云模块。
    std::function<Result(const AudioClip&, const TencentCredentials&, const RequestControl&)> recognize;
    // 测试替换剪贴板写入，隔离用户的真实剪贴板。
    std::function<bool(const std::wstring&)> copy_text;
    std::function<MicrophoneInfo()> microphone;
    std::function<CaptureResult(const std::atomic<bool>&, const std::atomic<bool>&, unsigned,
        const std::function<void(CaptureProgress)>&)> capture;
};
class SpeechRuntime {
public:
    explicit SpeechRuntime(SpeechServices services);
    ~SpeechRuntime();
    SpeechRuntime(const SpeechRuntime&) = delete;
    SpeechRuntime& operator=(const SpeechRuntime&) = delete;
    bool start();
    void stop();
    void open(bool microphone_test = false);
    bool process_message(MSG& message);
    HWND window() const;
    bool busy() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
// 跨进程消息仅请求打开界面，不携带凭据、文件、文本或输入目标。
inline bool request_speech_window(bool microphone_test = false) {
    const auto host = FindWindowExW(HWND_MESSAGE, nullptr, kSpeechHostClass, speech_host_title().c_str());
    return host && PostMessageW(host, microphone_test ? kSpeechTestMessage : kSpeechOpenMessage, 0, 0);
}
}
#endif

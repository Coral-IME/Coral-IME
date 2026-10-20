#ifndef CORAL_PLATFORM_WINDOWS_SPEECH_MICROPHONE_CAPTURE_H_INCLUDED
#define CORAL_PLATFORM_WINDOWS_SPEECH_MICROPHONE_CAPTURE_H_INCLUDED
#include "coral/speech/recognizer.h"
#include <atomic>
#include <functional>
#include <string>
namespace coral::speech::win32 {
struct MicrophoneInfo { bool available = false; std::wstring name; std::wstring message; };
MicrophoneInfo query_microphone();
struct CaptureProgress { unsigned duration_ms = 0; unsigned level = 0; };
struct CaptureResult { AudioClip audio; std::wstring error; bool cancelled = false; unsigned peak_level = 0; };
CaptureResult capture_microphone(const std::atomic<bool>& stop, const std::atomic<bool>& cancelled,
    unsigned max_seconds, const std::function<void(CaptureProgress)>& progress);
}
#endif

#ifndef CORAL_INCLUDE_CORAL_SPEECH_RECOGNIZER_H_INCLUDED
#define CORAL_INCLUDE_CORAL_SPEECH_RECOGNIZER_H_INCLUDED

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace coral::speech {
constexpr std::size_t kMaxAudioBytes = 3 * 1024 * 1024 / 4 * 3;
constexpr std::size_t kMaxResponseBytes = 1024 * 1024;
enum class AudioFormat { kWav, kPcm };
enum class Failure {
    kNone, kInvalidAudio, kMissingCredentials, kCancelled, kTimeout,
    kNetwork, kHttp, kCredentials, kRejected, kInvalidResponse, kInternal
};
struct AudioClip {
    std::vector<std::uint8_t> bytes;
    AudioFormat format = AudioFormat::kWav;
    // PCM 为单声道、有符号 16 位小端；WAV 从文件头读取采样率。
    std::uint32_t sample_rate = 16000;
};
struct Result {
    Failure failure = Failure::kNone;
    std::string text;
    std::string request_id;
    std::string provider_code;
    std::uint64_t audio_duration_ms = 0;
    bool ok() const noexcept { return failure == Failure::kNone; }
};
struct RequestControl {
    std::chrono::milliseconds timeout{30000};
    // 借用标志仅需在本次阻塞调用期间存活；不拥有后台线程。
    const std::atomic<bool>* cancelled = nullptr;
};
class Recognizer {
public:
    virtual ~Recognizer() = default;
    // 调用方在工作线程使用；返回完整 UTF-8 文本，不产生增量事件。
    virtual Result recognize(const AudioClip& audio, const RequestControl& control = {}) const = 0;
};
// 读取失败不修改输出；限制大小后再分配，不根据文件扩展名推断编码。
Failure load_audio_file(const std::filesystem::path& path, AudioFormat format,
    AudioClip& output, std::uint32_t sample_rate = 16000);
}
#endif

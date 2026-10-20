#include "coral/speech/tencent_recognizer.h"
#include <cstdlib>
#include <iostream>

namespace {
std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    using namespace coral::speech;
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: coral_speech_probe <audio.wav|audio.pcm> [16k_zh|8k_zh|...] [wav|pcm]\n";
        return 2;
    }
    TencentOptions options;
    if (argc >= 3) options.engine = std::filesystem::path(argv[2]).u8string();
    const std::string format = argc == 4 ? std::filesystem::path(argv[3]).u8string() : "wav";
    if (format != "wav" && format != "pcm") { std::cerr << "Unsupported audio format\n"; return 2; }
    TencentCredentials credentials{environment("CORAL_TENCENT_SECRET_ID"),
        environment("CORAL_TENCENT_SECRET_KEY"), environment("CORAL_TENCENT_TOKEN")};
    if (credentials.secret_id.empty() || credentials.secret_key.empty()) {
        std::cerr << "Set CORAL_TENCENT_SECRET_ID and CORAL_TENCENT_SECRET_KEY in the environment\n";
        return 2;
    }
    AudioClip audio;
    const auto rate = options.engine.rfind("8k_", 0) == 0 ? 8000u : 16000u;
#ifdef _WIN32
    const std::filesystem::path path(argv[1]);
#else
    const auto path = std::filesystem::u8path(argv[1]);
#endif
    if (load_audio_file(path, format == "wav" ? AudioFormat::kWav : AudioFormat::kPcm, audio, rate) != Failure::kNone) {
        std::cerr << "Cannot read audio or file exceeds upload limit\n"; return 2;
    }
    TencentRecognizer recognizer(std::move(credentials), options);
    const auto result = recognizer.recognize(audio);
    if (!result.ok()) {
        // 不打印密钥、签名、音频、响应正文或供应商错误消息。
        std::cerr << "Recognition failed: " << static_cast<int>(result.failure)
            << " provider_code=" << result.provider_code << " request_id=" << result.request_id << '\n';
        return 1;
    }
    std::cout << result.text << '\n';
    std::cerr << "duration_ms=" << result.audio_duration_ms << " request_id=" << result.request_id << '\n';
    return 0;
}

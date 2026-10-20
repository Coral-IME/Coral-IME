#include "tencent_protocol.h"
#include <cstring>
#include <fstream>

namespace coral::speech {
Failure load_audio_file(const std::filesystem::path& path, AudioFormat format,
    AudioClip& output, std::uint32_t sample_rate) {
    try {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return Failure::kInvalidAudio;
        const auto length = file.tellg();
        if (length <= 0 || length > static_cast<std::streamoff>(kMaxAudioBytes)) return Failure::kInvalidAudio;
        AudioClip audio;
        audio.format = format;
        audio.sample_rate = sample_rate;
        audio.bytes.resize(static_cast<std::size_t>(length));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(audio.bytes.data()), length) || file.peek() != EOF)
            return Failure::kInvalidAudio;
        output = std::move(audio);
        return Failure::kNone;
    } catch (...) { return Failure::kInvalidAudio; }
}
namespace detail {
bool valid_audio(const AudioClip& audio, const TencentOptions& options) {
    const auto& bytes = audio.bytes;
    if (bytes.empty() || bytes.size() > kMaxAudioBytes) return false;
    const auto rate = options.engine.rfind("8k_", 0) == 0 ? 8000u :
        options.engine.rfind("16k_", 0) == 0 ? 16000u : 0u;
    if (!rate) return false;
    if (audio.format == AudioFormat::kPcm)
        return audio.sample_rate == rate && bytes.size() % 2 == 0 && bytes.size() <= rate * 2 * 60;
    if (audio.format != AudioFormat::kWav || bytes.size() < 12) return false;
    auto u16 = [&](std::size_t pos) { return std::uint32_t(bytes[pos]) | (std::uint32_t(bytes[pos + 1]) << 8); };
    auto u32 = [&](std::size_t pos) { return u16(pos) | (u16(pos + 2) << 16); };
    if (std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4) ||
        u32(4) != bytes.size() - 8) return false;
    bool have_format = false, have_data = false;
    std::size_t pos = 12;
    while (pos < bytes.size()) {
        if (bytes.size() - pos < 8) return false;
        const auto size = u32(pos + 4);
        const auto start = pos + 8;
        if (size > bytes.size() - start) return false;
        if (!std::memcmp(bytes.data() + pos, "fmt ", 4)) {
            if (have_format || size < 16 || u16(start) != 1 || u16(start + 2) != 1 ||
                u32(start + 4) != rate || u32(start + 8) != rate * 2 ||
                u16(start + 12) != 2 || u16(start + 14) != 16) return false;
            have_format = true;
        } else if (!std::memcmp(bytes.data() + pos, "data", 4)) {
            if (have_data || !size || size % 2 || size > rate * 2 * 60) return false;
            have_data = true;
        }
        const auto padded = std::size_t(size) + (size & 1u);
        if (padded > bytes.size() - start) return false;
        pos = start + padded;
    }
    return have_format && have_data;
}
}
}

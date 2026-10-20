#include "microphone_capture.h"
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace coral::speech::win32 {
namespace {
WAVEFORMATEX capture_format() {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 16000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = 32000;
    return format;
}
std::wstring error_text(MMRESULT code) {
    if (code == MMSYSERR_ALLOCATED) return L"麦克风被占用，请关闭其他录音程序后重试。";
    if (code == MMSYSERR_NODRIVER || code == MMSYSERR_BADDEVICEID) return L"未找到可用麦克风，请连接设备后重试。";
    if (code == WAVERR_BADFORMAT) return L"麦克风不支持所需的录音格式，请更换默认输入设备。";
    return L"无法打开麦克风，请检查 Windows 麦克风隐私权限和默认输入设备。";
}
struct WaveInput {
    HWAVEIN input = nullptr;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::array<std::array<char, 3200>, 4> buffers{};
    std::array<WAVEHDR, 4> headers{};
    ~WaveInput() {
        if (input) {
            waveInReset(input);
            for (auto& header : headers)
                if (header.dwFlags & WHDR_PREPARED) waveInUnprepareHeader(input, &header, sizeof(header));
            waveInClose(input);
        }
        if (event) CloseHandle(event);
    }
};
}
MicrophoneInfo query_microphone() {
    if (!waveInGetNumDevs()) return {false, {}, L"未检测到麦克风，请连接输入设备。"};
    auto format = capture_format();
    const auto code = waveInOpen(nullptr, WAVE_MAPPER, &format, 0, 0, WAVE_FORMAT_QUERY);
    if (code != MMSYSERR_NOERROR) return {false, {}, error_text(code)};
    // MAPPER 的实际设备由录音打开后确认；检测页使用默认输入设备描述。
    return {true, L"Windows 默认麦克风", L"可进行录音检测；点击后观察音量条。"};
}
CaptureResult capture_microphone(const std::atomic<bool>& stop, const std::atomic<bool>& cancelled,
    unsigned max_seconds, const std::function<void(CaptureProgress)>& progress) {
    CaptureResult result;
    result.audio.format = AudioFormat::kPcm;
    result.audio.sample_rate = 16000;
    if (cancelled.load()) { result.cancelled = true; return result; }
    if (!max_seconds || max_seconds > 60) { result.error = L"录音时长设置无效。"; return result; }
    WaveInput wave;
    if (!wave.event) { result.error = L"无法初始化录音，请重试。"; return result; }
    auto format = capture_format();
    const auto code = waveInOpen(&wave.input, WAVE_MAPPER, &format,
        reinterpret_cast<DWORD_PTR>(wave.event), 0, CALLBACK_EVENT);
    if (code != MMSYSERR_NOERROR) { result.error = error_text(code); return result; }
    for (std::size_t i = 0; i < wave.headers.size(); ++i) {
        auto& header = wave.headers[i];
        header.lpData = wave.buffers[i].data();
        header.dwBufferLength = static_cast<DWORD>(wave.buffers[i].size());
        if (waveInPrepareHeader(wave.input, &header, sizeof(header)) != MMSYSERR_NOERROR ||
            waveInAddBuffer(wave.input, &header, sizeof(header)) != MMSYSERR_NOERROR) {
            result.error = L"无法分配录音缓冲区，请重试。"; return result;
        }
    }
    if (waveInStart(wave.input) != MMSYSERR_NOERROR) { result.error = L"麦克风无法开始录音。"; return result; }
    const auto started = std::chrono::steady_clock::now();
    const std::size_t limit = std::size_t(max_seconds) * 32000;
    result.audio.bytes.reserve(limit);
    auto append = [&](const WAVEHDR& header) {
        const auto length = (std::min)(std::size_t(header.dwBytesRecorded), limit - result.audio.bytes.size()) & ~std::size_t(1);
        if (length > header.dwBufferLength) { result.error = L"录音设备返回无效数据。"; return; }
        double energy = 0;
        for (std::size_t pos = 0; pos < length; pos += 2) {
            std::int16_t sample = 0;
            std::memcpy(&sample, header.lpData + pos, 2);
            energy += double(sample) * sample;
        }
        const unsigned level = length ? static_cast<unsigned>((std::min)(100.0, std::sqrt(energy / (length / 2)) / 32768.0 * 400)) : 0;
        result.peak_level = (std::max)(result.peak_level, level);
        result.audio.bytes.insert(result.audio.bytes.end(), header.lpData, header.lpData + length);
        progress({static_cast<unsigned>(result.audio.bytes.size() / 32), level});
    };
    std::size_t next = 0;
    while (!stop.load() && !cancelled.load() && result.audio.bytes.size() < limit &&
        std::chrono::steady_clock::now() - started < std::chrono::seconds(max_seconds)) {
        if (WaitForSingleObject(wave.event, 50) == WAIT_FAILED) { result.error = L"读取麦克风数据失败。"; break; }
        for (std::size_t count = 0; count < wave.headers.size(); ++count) {
            auto& header = wave.headers[next];
            if (!(header.dwFlags & WHDR_DONE)) break;
            append(header);
            if (!result.error.empty()) break;
            header.dwBytesRecorded = 0;
            if (waveInAddBuffer(wave.input, &header, sizeof(header)) != MMSYSERR_NOERROR) {
                result.error = L"录音设备已断开，请重新连接。"; break;
            }
            next = (next + 1) % wave.headers.size();
        }
        if (!result.error.empty()) break;
    }
    // reset 等待所有缓冲区返回，按提交顺序收取最后不足一帧的数据。
    if (waveInReset(wave.input) != MMSYSERR_NOERROR) result.error = L"麦克风停止录音失败。";
    for (std::size_t count = 0; count < wave.headers.size() && result.error.empty(); ++count)
        append(wave.headers[(next + count) % wave.headers.size()]);
    result.cancelled = cancelled.load();
    if (result.audio.bytes.empty() && !result.cancelled && result.error.empty()) result.error = L"没有收到录音数据，请检查麦克风权限。";
    if (result.cancelled || !result.error.empty()) result.audio.bytes.clear();
    return result;
}
}

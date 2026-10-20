#include "coral/updates/download_wire.h"
#include <algorithm>

namespace coral::updates {
namespace {
void store(std::uint8_t* output, std::uint64_t value, unsigned bytes) noexcept {
    for (unsigned index = 0; index < bytes; ++index) { output[index] = static_cast<std::uint8_t>(value); value >>= 8; }
}
std::uint64_t load(const std::uint8_t* input, unsigned bytes) noexcept {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < bytes; ++index) value |= static_cast<std::uint64_t>(input[index]) << (index * 8);
    return value;
}
bool nonzero(const RequestNonce& nonce) noexcept {
    return std::any_of(nonce.begin(), nonce.end(), [](auto byte) { return byte != 0; });
}
std::uint32_t checksum(const std::uint8_t* bytes, std::size_t size) noexcept {
    std::uint32_t crc = UINT32_MAX;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= index >= 12 && index < 16 ? 0 : bytes[index];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return crc ^ UINT32_MAX;
}
void header(std::uint8_t* bytes, std::uint16_t kind, std::size_t size, const RequestNonce& nonce) noexcept {
    bytes[0] = 'C'; bytes[1] = 'U'; bytes[2] = 'P'; bytes[3] = '1';
    store(bytes + 4, 1, 2); store(bytes + 6, kind, 2); store(bytes + 8, size, 4);
    std::copy(nonce.begin(), nonce.end(), bytes + 16);
}
bool valid_header(const std::uint8_t* bytes, std::size_t size, std::uint16_t kind, std::size_t required) noexcept {
    return bytes && size == required && bytes[0] == 'C' && bytes[1] == 'U' && bytes[2] == 'P' && bytes[3] == '1'
        && load(bytes + 4, 2) == 1 && load(bytes + 6, 2) == kind && load(bytes + 8, 4) == required
        && load(bytes + 12, 4) == checksum(bytes, size);
}
bool valid_progress(const DownloadProgress& progress) noexcept {
    if (!nonzero(progress.nonce) || !progress.sequence || progress.percent > 100
        || static_cast<std::uint32_t>(progress.error) > static_cast<std::uint32_t>(DownloadError::kTimeout)) return false;
    if (progress.stage == DownloadStage::kFailed) return progress.error != DownloadError::kNone
        && progress.error != DownloadError::kCancelled && progress.percent < 100;
    if (progress.stage == DownloadStage::kCancelled) return progress.error == DownloadError::kCancelled && progress.percent < 100;
    if (progress.error != DownloadError::kNone) return false;
    switch (progress.stage) {
    case DownloadStage::kStarting: case DownloadStage::kLocating: return progress.percent == 0;
    case DownloadStage::kDownloading: return progress.percent <= 80;
    case DownloadStage::kVerifying: return progress.percent == 90;
    case DownloadStage::kPublishing: return progress.percent == 95;
    case DownloadStage::kReady: return progress.percent == 100;
    default: return false;
    }
}
}
bool valid_download_request(const DownloadRequest& request) noexcept {
    return nonzero(request.nonce) && request.release.release_id && request.release.release_id <= INT32_MAX
        && request.release.package.byte_count && request.release.package.byte_count <= kMaximumPackageBytes;
}
bool terminal_stage(DownloadStage stage) noexcept {
    return stage == DownloadStage::kReady || stage == DownloadStage::kFailed || stage == DownloadStage::kCancelled;
}
bool encode_download_request(const DownloadRequest& request, RequestFrame& result) noexcept {
    if (!valid_download_request(request)) return false;
    RequestFrame frame{}; header(frame.data(), 1, frame.size(), request.nonce);
    store(frame.data() + 32, request.release.release_id, 4);
    for (unsigned index = 0; index < 4; ++index) store(frame.data() + 36 + index * 2, request.release.version.components[index], 2);
    store(frame.data() + 44, request.release.package.byte_count, 8);
    std::copy(request.release.package.sha256.begin(), request.release.package.sha256.end(), frame.begin() + 52);
    store(frame.data() + 12, checksum(frame.data(), frame.size()), 4); result = frame; return true;
}
bool decode_download_request(const std::uint8_t* bytes, std::size_t size, DownloadRequest& result) {
    if (!valid_header(bytes, size, 1, kDownloadRequestBytes) || load(bytes + 84, 4)) return false;
    DownloadRequest request;
    std::copy(bytes + 16, bytes + 32, request.nonce.begin());
    request.release.release_id = static_cast<std::uint32_t>(load(bytes + 32, 4));
    for (unsigned index = 0; index < 4; ++index) request.release.version.components[index] = static_cast<std::uint16_t>(load(bytes + 36 + index * 2, 2));
    request.release.package.byte_count = load(bytes + 44, 8);
    std::copy(bytes + 52, bytes + 84, request.release.package.sha256.begin());
    if (!valid_download_request(request)) return false;
    result = std::move(request); return true;
}
bool encode_download_progress(const DownloadProgress& progress, ProgressFrame& result) noexcept {
    if (!valid_progress(progress)) return false;
    ProgressFrame frame{}; header(frame.data(), 2, frame.size(), progress.nonce);
    store(frame.data() + 32, static_cast<std::uint32_t>(progress.stage), 4); store(frame.data() + 36, progress.percent, 4);
    store(frame.data() + 40, static_cast<std::uint32_t>(progress.error), 4); store(frame.data() + 48, progress.sequence, 8);
    store(frame.data() + 12, checksum(frame.data(), frame.size()), 4); result = frame; return true;
}
bool decode_download_progress(const std::uint8_t* bytes, std::size_t size, DownloadProgress& result) noexcept {
    if (!valid_header(bytes, size, 2, kDownloadProgressBytes) || load(bytes + 44, 4)) return false;
    DownloadProgress progress;
    std::copy(bytes + 16, bytes + 32, progress.nonce.begin());
    progress.stage = static_cast<DownloadStage>(load(bytes + 32, 4)); progress.percent = static_cast<std::uint32_t>(load(bytes + 36, 4));
    progress.error = static_cast<DownloadError>(load(bytes + 40, 4)); progress.sequence = load(bytes + 48, 8);
    if (!valid_progress(progress)) return false;
    result = progress; return true;
}
ProgressAcceptance ProgressTracker::accept(const DownloadProgress& progress) noexcept {
    if (!valid_progress(progress) || progress.nonce != nonce_) return ProgressAcceptance::kRejected;
    if (current_) {
        if (progress.sequence < current_->sequence) return ProgressAcceptance::kUnchanged;
        if (progress.sequence == current_->sequence) return progress.stage == current_->stage && progress.percent == current_->percent
            && progress.error == current_->error ? ProgressAcceptance::kUnchanged : ProgressAcceptance::kRejected;
        if (terminal_stage(current_->stage) || progress.percent < current_->percent
            || (!terminal_stage(progress.stage) && progress.stage < current_->stage)) return ProgressAcceptance::kRejected;
    }
    current_ = progress; return ProgressAcceptance::kAccepted;
}
}

#pragma once
#include "coral/updates/release_metadata.h"
#include <array>
#include <optional>

namespace coral::updates {
using RequestNonce = std::array<std::uint8_t, 16>;
struct DownloadRequest { RequestNonce nonce{}; ReleaseMetadata release; };
enum class DownloadStage : std::uint32_t { kStarting = 1, kLocating, kDownloading, kVerifying, kPublishing, kReady, kFailed, kCancelled };
enum class DownloadError : std::uint32_t { kNone, kCancelled, kInvalidRequest, kLocationFailed, kDownloadFailed, kTooLarge,
    kPrepareFailed, kVerificationFailed, kPublishFailed, kChannelFailed, kAgentExited, kTimeout };
struct DownloadProgress {
    RequestNonce nonce{};
    DownloadStage stage = DownloadStage::kStarting;
    std::uint32_t percent = 0;
    DownloadError error = DownloadError::kNone;
    std::uint64_t sequence = 0;
};
inline constexpr std::size_t kDownloadRequestBytes = 88;
inline constexpr std::size_t kDownloadProgressBytes = 56;
using RequestFrame = std::array<std::uint8_t, kDownloadRequestBytes>;
using ProgressFrame = std::array<std::uint8_t, kDownloadProgressBytes>;
bool valid_download_request(const DownloadRequest& request) noexcept;
bool terminal_stage(DownloadStage stage) noexcept;
bool encode_download_request(const DownloadRequest& request, RequestFrame& result) noexcept;
bool decode_download_request(const std::uint8_t* bytes, std::size_t size, DownloadRequest& result);
bool encode_download_progress(const DownloadProgress& progress, ProgressFrame& result) noexcept;
bool decode_download_progress(const std::uint8_t* bytes, std::size_t size, DownloadProgress& result) noexcept;
enum class ProgressAcceptance { kAccepted, kUnchanged, kRejected };
class ProgressTracker {
public:
    explicit ProgressTracker(RequestNonce nonce) : nonce_(nonce) {}
    ProgressAcceptance accept(const DownloadProgress& progress) noexcept;
    const std::optional<DownloadProgress>& current() const noexcept { return current_; }
private:
    RequestNonce nonce_;
    std::optional<DownloadProgress> current_;
};
}

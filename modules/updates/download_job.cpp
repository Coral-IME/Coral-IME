#include "coral/updates/download_job.h"
#include <algorithm>

namespace coral::updates {
DownloadError execute_download(const DownloadRequest& request, DownloadActions& actions) {
    if (!valid_download_request(request)) return DownloadError::kInvalidRequest;
    DownloadProgress progress; progress.nonce = request.nonce; progress.sequence = 1; // 父进程初始化快照 1。
    std::filesystem::path pending;
    struct PendingGuard {
        DownloadActions& actions; std::filesystem::path& pending;
        ~PendingGuard() { if (!pending.empty()) actions.discard(pending); }
    } guard{actions, pending};
    const auto report = [&](DownloadStage stage, std::uint32_t percent, DownloadError error = DownloadError::kNone) {
        progress.stage = stage; progress.percent = percent; progress.error = error;
        if (progress.sequence == UINT64_MAX) return false;
        ++progress.sequence; return actions.report(progress);
    };
    const auto fail = [&](DownloadError error) {
        try { report(error == DownloadError::kCancelled ? DownloadStage::kCancelled : DownloadStage::kFailed, progress.percent, error); } catch (...) {}
        return error;
    };
    try {
        if (actions.cancelled()) return fail(DownloadError::kCancelled);
        if (!report(DownloadStage::kStarting, 0) || !report(DownloadStage::kLocating, 0)) return fail(DownloadError::kChannelFailed);
        const auto location = actions.locate(request.release.release_id);
        if (actions.cancelled()) return fail(DownloadError::kCancelled);
        if (location.error != DownloadError::kNone) return fail(location.error);
        std::string url;
        if (!parse_download_location(location.body, url)) return fail(DownloadError::kLocationFailed);
        pending = actions.reserve_pending();
        if (pending.empty()) return fail(DownloadError::kPrepareFailed);
        if (!report(DownloadStage::kDownloading, 0)) return fail(DownloadError::kChannelFailed);
        bool reporting_failed = false;
        const auto transferred = actions.download(url, pending, request.release.package.byte_count, [&](std::uint32_t percent) {
            if (actions.cancelled()) return false;
            percent = (std::min)(percent, 80u);
            if (percent <= progress.percent) return true;
            const bool reported = report(DownloadStage::kDownloading, percent);
            reporting_failed = reporting_failed || !reported;
            return reported;
        });
        if (actions.cancelled()) return fail(DownloadError::kCancelled);
        if (reporting_failed) return fail(DownloadError::kChannelFailed);
        if (transferred != DownloadError::kNone) return fail(transferred);
        if (!report(DownloadStage::kVerifying, 90)) return fail(DownloadError::kChannelFailed);
        const bool verified = actions.verify(pending, request.release.package);
        if (actions.cancelled()) return fail(DownloadError::kCancelled);
        if (!verified) return fail(DownloadError::kVerificationFailed);
        if (!report(DownloadStage::kPublishing, 95)) return fail(DownloadError::kChannelFailed);
        if (actions.cancelled()) return fail(DownloadError::kCancelled);
        if (!actions.publish(pending, request.release)) return fail(actions.cancelled() ? DownloadError::kCancelled : DownloadError::kPublishFailed);
        pending.clear(); // 发布是提交边界；之后的取消无法撤销它。
        try { report(DownloadStage::kReady, 100); } catch (...) {}
        return DownloadError::kNone;
    } catch (...) { return fail(DownloadError::kDownloadFailed); }
}
}

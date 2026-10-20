#include "coral/updates/download_job.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace coral::updates;
void require(bool value) { if(!value) throw std::runtime_error("Download job fixture failed"); }
class Actions final : public DownloadActions {
public:
    bool cancel=false, verify_ok=true, publish_ok=true, reserve_ok=true, cancel_download=false, cancel_verify=false, cancel_commit=false, drop_ready=false;
    int discarded=0, published=0, verified=0; std::uint32_t located=0;
    DownloadError transfer=DownloadError::kNone;
    std::string location="{\"data\":{\"url\":\"https://example.test/artifact\"}}";
    std::vector<DownloadProgress> snapshots;
    bool cancelled() override { return cancel; }
    bool report(const DownloadProgress& value) override {
        snapshots.push_back(value);
        if(drop_ready && value.stage==DownloadStage::kReady) throw std::runtime_error("lost post-commit reply");
        return true;
    }
    LocationReply locate(std::uint32_t id) override { located=id; return {DownloadError::kNone,location}; }
    std::filesystem::path reserve_pending() override { return reserve_ok ? "C:/lab/pending.tmp" : ""; }
    DownloadError download(const std::string&, const std::filesystem::path&, std::uint64_t limit,
        const std::function<bool(std::uint32_t)>& progress) override {
        require(limit==3); progress(40); progress(30); cancel=cancel_download; return transfer;
    }
    bool verify(const std::filesystem::path&,const PackageFingerprint& package) override { ++verified; require(package.byte_count==3); cancel=cancel_verify; return verify_ok; }
    bool publish(const std::filesystem::path&,const ReleaseMetadata& release) override { ++published; require(release.release_id==7); cancel=cancel_commit; return publish_ok; }
    void discard(const std::filesystem::path&) noexcept override { ++discarded; }
};
}
int main() {
    try {
        DownloadRequest request; request.nonce[0]=1; request.release.release_id=7; request.release.package.byte_count=3;
        Actions happy; require(execute_download(request,happy)==DownloadError::kNone && happy.located==7 && happy.published==1 && !happy.discarded);
        ProgressTracker tracker(request.nonce); for(const auto& value:happy.snapshots) require(tracker.accept(value)==ProgressAcceptance::kAccepted);
        require(happy.snapshots.back().stage==DownloadStage::kReady && happy.snapshots.back().percent==100);
        Actions early; early.cancel=true; require(execute_download(request,early)==DownloadError::kCancelled && early.located==0);
        Actions malformed; malformed.location="garbage"; require(execute_download(request,malformed)==DownloadError::kLocationFailed && !malformed.discarded);
        Actions missing; missing.reserve_ok=false; require(execute_download(request,missing)==DownloadError::kPrepareFailed && !missing.discarded);
        Actions oversized; oversized.transfer=DownloadError::kTooLarge; require(execute_download(request,oversized)==DownloadError::kTooLarge && oversized.discarded==1 && !oversized.verified);
        Actions bad_hash; bad_hash.verify_ok=false; require(execute_download(request,bad_hash)==DownloadError::kVerificationFailed && !bad_hash.published && bad_hash.discarded==1);
        Actions publish_failed; publish_failed.publish_ok=false; require(execute_download(request,publish_failed)==DownloadError::kPublishFailed && publish_failed.discarded==1);
        Actions cancelled; cancelled.cancel_download=true; require(execute_download(request,cancelled)==DownloadError::kCancelled && !cancelled.published && cancelled.discarded==1);
        Actions during_hash; during_hash.cancel_verify=true; require(execute_download(request,during_hash)==DownloadError::kCancelled && !during_hash.published);
        Actions committed; committed.cancel_commit=true; require(execute_download(request,committed)==DownloadError::kNone && committed.discarded==0);
        Actions lost_reply; lost_reply.drop_ready=true; require(execute_download(request,lost_reply)==DownloadError::kNone && lost_reply.discarded==0);
        auto invalid=request; invalid.nonce={}; Actions untouched; require(execute_download(invalid,untouched)==DownloadError::kInvalidRequest && untouched.snapshots.empty());
        std::cout<<"Pinned release, rollback/cancel boundaries and lost post-commit response passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    return 0;
}

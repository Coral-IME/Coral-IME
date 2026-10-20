#include "coral/updates/download_wire.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value) { if (!value) throw std::runtime_error("CUP1 fixture failed"); }
}
int main() {
    using namespace coral::updates;
    try {
        // 独立的小端序夹具；CRC32 由 Python zlib 生成，而非本编解码器。
        const std::string hex = "435550310100010058000000b30c158c0102030405060708090a0b0c0d0e0f100700000001000200030004000300000000000000"
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad00000000";
        RequestFrame fixture{};
        for (std::size_t index = 0; index < fixture.size(); ++index) fixture[index] = static_cast<std::uint8_t>(std::stoul(hex.substr(index*2,2),nullptr,16));
        DownloadRequest request;
        require(decode_download_request(fixture.data(), fixture.size(), request));
        require(request.release.release_id == 7 && request.release.version.components == std::array<std::uint16_t,4>{1,2,3,4}
            && request.release.package.byte_count == 3 && request.release.package.sha256[31] == 0xad && request.nonce[15] == 16);
        RequestFrame encoded{}; require(encode_download_request(request, encoded) && encoded == fixture);
        DownloadRequest saved = request;
        for (std::size_t length = 0; length < fixture.size(); ++length) {
            require(!decode_download_request(fixture.data(), length, saved)); require(saved.release.release_id == 7);
        }
        for (std::size_t index = 0; index < fixture.size(); ++index) {
            auto corrupt = fixture; corrupt[index] ^= 1;
            require(!decode_download_request(corrupt.data(), corrupt.size(), saved) && saved.nonce == request.nonce);
        }
        auto invalid = request; invalid.nonce = {}; require(!encode_download_request(invalid, encoded) && encoded == fixture);
        invalid = request; invalid.release.release_id = 0; require(!encode_download_request(invalid, encoded));
        invalid = request; invalid.release.package.byte_count = kMaximumPackageBytes + 1; require(!encode_download_request(invalid, encoded));
        DownloadProgress progress{request.nonce, DownloadStage::kStarting,0,DownloadError::kNone,1};
        ProgressFrame frame{}; require(encode_download_progress(progress, frame));
        DownloadProgress decoded; require(decode_download_progress(frame.data(),frame.size(),decoded) && decoded.sequence==1);
        for(std::size_t length=0;length<frame.size();++length) require(!decode_download_progress(frame.data(),length,decoded) && decoded.sequence==1);
        ProgressTracker tracker(request.nonce);
        require(tracker.accept(progress)==ProgressAcceptance::kAccepted);
        require(tracker.accept(progress)==ProgressAcceptance::kUnchanged);
        progress.stage=DownloadStage::kDownloading; progress.percent=40; progress.sequence=2;
        require(tracker.accept(progress)==ProgressAcceptance::kAccepted);
        auto changed=progress; changed.percent=41; require(tracker.accept(changed)==ProgressAcceptance::kRejected);
        changed=progress; changed.sequence=3; changed.percent=30; require(tracker.accept(changed)==ProgressAcceptance::kRejected);
        changed=progress; changed.nonce[0]^=1; require(tracker.accept(changed)==ProgressAcceptance::kRejected);
        changed=progress; changed.sequence=1; require(tracker.accept(changed)==ProgressAcceptance::kUnchanged);
        progress.stage=DownloadStage::kReady; progress.percent=100; progress.sequence=8;
        require(tracker.accept(progress)==ProgressAcceptance::kAccepted); // 中间快照可以跳过。
        changed=progress; changed.sequence=9; require(tracker.accept(changed)==ProgressAcceptance::kRejected);
        changed=progress; changed.percent=99; require(!encode_download_progress(changed,frame));
        changed.stage=DownloadStage::kFailed; changed.error=DownloadError::kPublishFailed;
        require(encode_download_progress(changed,frame)); require(tracker.accept(changed)==ProgressAcceptance::kRejected);
        changed.stage=static_cast<DownloadStage>(99); require(!encode_download_progress(changed,frame));
        std::cout << "Independent CUP1 bytes, corruption/truncation and correlated monotonic/terminal snapshots passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    return 0;
}

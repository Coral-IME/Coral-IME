#pragma once
#include "coral/updates/download_wire.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace coral::updates {
enum class HttpPolicy { kHttpsOnly, kLoopbackTest };
struct HttpReply { DownloadError error = DownloadError::kNone; std::string body; std::uint64_t bytes = 0; };
class UpdateHttp {
public:
    explicit UpdateHttp(HttpPolicy policy = HttpPolicy::kHttpsOnly) : policy_(policy) {}
    HttpReply get(const std::string& url, const std::function<bool()>& cancelled, std::uint32_t timeout_ms = 30000);
    HttpReply save(const std::string& url, const std::filesystem::path& file, std::uint64_t limit,
        const std::function<bool()>& cancelled, const std::function<bool(std::uint32_t)>& progress, std::uint32_t timeout_ms = 600000);
private: HttpPolicy policy_;
};
class MetadataRequest {
public:
    static std::unique_ptr<MetadataRequest> start(std::string url, std::function<void(HttpReply)> callback);
    ~MetadataRequest();
    MetadataRequest(const MetadataRequest&) = delete; MetadataRequest& operator=(const MetadataRequest&) = delete;
    void cancel() noexcept;
    bool finished() const noexcept;
    bool callback_failed() const noexcept;
private:
    struct State;
    explicit MetadataRequest(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};
}

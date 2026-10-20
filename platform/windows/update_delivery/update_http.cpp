#include "update_http.h"
#include <curl/curl.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

namespace coral::updates {
namespace {
std::mutex runtime_mutex;
unsigned runtime_users = 0;
class CurlLease {
public:
    CurlLease() {
        std::lock_guard<std::mutex> lock(runtime_mutex);
        if (!runtime_users && ::curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return;
        ++runtime_users; acquired_ = true;
    }
    ~CurlLease() {
        std::lock_guard<std::mutex> lock(runtime_mutex);
        if (acquired_ && !--runtime_users) ::curl_global_cleanup();
    }
    bool acquired() const { return acquired_; }
private: bool acquired_ = false;
};
struct CurlObjects {
    CURL* easy = ::curl_easy_init(); CURLM* multi = ::curl_multi_init(); bool added = false;
    ~CurlObjects() {
        if (added) ::curl_multi_remove_handle(multi, easy);
        if (multi) ::curl_multi_cleanup(multi);
        if (easy) ::curl_easy_cleanup(easy);
    }
};
struct Sink {
    HttpReply reply;
    std::ofstream* file = nullptr;
    std::uint64_t limit = 0;
    const std::function<bool()>* cancelled = nullptr;
    const std::function<bool(std::uint32_t)>* progress = nullptr;
};
bool allowed_url(const std::string& url, HttpPolicy policy) {
    if (url.empty() || url.size() > 8192 || url.find('\0') != std::string::npos) return false;
    for (const unsigned char character : url) if (character <= 32 || character == 127 || character == '\\') return false;
    if (policy == HttpPolicy::kHttpsOnly) return url.rfind("https://", 0) == 0;
    if (url.rfind("http://127.0.0.1", 0) != 0) return false;
    const auto end = url.find('/', 15);
    const auto authority = url.substr(7, end == std::string::npos ? std::string::npos : end - 7);
    if (authority == "127.0.0.1") return true;
    if (authority.rfind("127.0.0.1:", 0) != 0 || authority.size() <= 10) return false;
    for (std::size_t index = 10; index < authority.size(); ++index) if (authority[index] < '0' || authority[index] > '9') return false;
    return true;
}
size_t receive(char* bytes, size_t size, size_t count, void* context) noexcept {
    auto& sink = *static_cast<Sink*>(context);
    try {
        if ((*sink.cancelled)()) { sink.reply.error = DownloadError::kCancelled; return 0; }
        if (size && count > SIZE_MAX / size) { sink.reply.error = DownloadError::kTooLarge; return 0; }
        const auto total = size * count;
        if (total > sink.limit - sink.reply.bytes) { sink.reply.error = DownloadError::kTooLarge; return 0; }
        if (sink.file) {
            sink.file->write(bytes, static_cast<std::streamsize>(total));
            if (!*sink.file) { sink.reply.error = DownloadError::kDownloadFailed; return 0; }
        } else sink.reply.body.append(bytes, total);
        sink.reply.bytes += total;
        if (sink.progress && !(*sink.progress)(static_cast<std::uint32_t>(sink.reply.bytes * 80 / sink.limit))) {
            sink.reply.error = DownloadError::kCancelled; return 0;
        }
        return total;
    } catch (...) { sink.reply.error = DownloadError::kDownloadFailed; return 0; }
}
HttpReply transfer(const std::string& url, Sink& sink, HttpPolicy policy, std::uint32_t timeout_ms) {
    if (!timeout_ms || timeout_ms > 600000 || !sink.limit || !allowed_url(url, policy)) { sink.reply.error = DownloadError::kInvalidRequest; return sink.reply; }
    if ((*sink.cancelled)()) { sink.reply.error = DownloadError::kCancelled; return sink.reply; }
    CurlLease lease; CurlObjects objects;
    if (!lease.acquired() || !objects.easy || !objects.multi) { sink.reply.error = DownloadError::kDownloadFailed; return sink.reply; }
    const char* protocols = policy == HttpPolicy::kHttpsOnly ? "https" : "http";
    const auto configured =
        ::curl_easy_setopt(objects.easy, CURLOPT_URL, url.c_str()) == CURLE_OK
        && ::curl_easy_setopt(objects.easy, CURLOPT_PROTOCOLS_STR, protocols) == CURLE_OK
        && ::curl_easy_setopt(objects.easy, CURLOPT_FOLLOWLOCATION, 0L) == CURLE_OK
        && ::curl_easy_setopt(objects.easy, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK
        && ::curl_easy_setopt(objects.easy, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK
        && ::curl_easy_setopt(objects.easy, CURLOPT_NOSIGNAL, 1L) == CURLE_OK
        && ::curl_easy_setopt(objects.easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L) == CURLE_OK
        && ::curl_easy_setopt(objects.easy, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms)) == CURLE_OK
        && ::curl_easy_setopt(objects.easy, CURLOPT_WRITEFUNCTION, receive) == CURLE_OK
        && ::curl_easy_setopt(objects.easy, CURLOPT_WRITEDATA, &sink) == CURLE_OK;
    if (!configured || (policy == HttpPolicy::kLoopbackTest && ::curl_easy_setopt(objects.easy, CURLOPT_PROXY, "") != CURLE_OK)
        || ::curl_multi_add_handle(objects.multi, objects.easy) != CURLM_OK) { sink.reply.error = DownloadError::kDownloadFailed; return sink.reply; }
    objects.added = true;
    const auto begin = std::chrono::steady_clock::now();
    for (;;) {
        if ((*sink.cancelled)()) { sink.reply.error = DownloadError::kCancelled; break; }
        if (std::chrono::steady_clock::now() - begin >= std::chrono::milliseconds(timeout_ms)) { sink.reply.error = DownloadError::kTimeout; break; }
        int running = 0;
        if (::curl_multi_perform(objects.multi, &running) != CURLM_OK) { sink.reply.error = DownloadError::kDownloadFailed; break; }
        if (!running) {
            int queued = 0; const auto message = ::curl_multi_info_read(objects.multi, &queued);
            long status = 0; ::curl_easy_getinfo(objects.easy, CURLINFO_RESPONSE_CODE, &status);
            if (sink.reply.error == DownloadError::kNone && (!message || message->msg != CURLMSG_DONE
                || message->data.result != CURLE_OK || status < 200 || status >= 300))
                sink.reply.error = message && message->msg == CURLMSG_DONE && message->data.result == CURLE_OPERATION_TIMEDOUT
                    ? DownloadError::kTimeout : DownloadError::kDownloadFailed;
            break;
        }
        if (::curl_multi_poll(objects.multi, nullptr, 0, 100, nullptr) != CURLM_OK) { sink.reply.error = DownloadError::kDownloadFailed; break; }
    }
    return sink.reply;
}
}
HttpReply UpdateHttp::get(const std::string& url, const std::function<bool()>& cancelled, std::uint32_t timeout_ms) {
    Sink sink; sink.limit = kMaximumMetadataBytes; sink.cancelled = &cancelled;
    try { return transfer(url, sink, policy_, timeout_ms); }
    catch (...) { sink.reply.error = DownloadError::kDownloadFailed; return sink.reply; }
}
HttpReply UpdateHttp::save(const std::string& url, const std::filesystem::path& file, std::uint64_t limit,
    const std::function<bool()>& cancelled, const std::function<bool(std::uint32_t)>& progress, std::uint32_t timeout_ms) {
    Sink sink; sink.limit = limit; sink.cancelled = &cancelled; sink.progress = &progress;
    if (!file.is_absolute() || !limit || limit > kMaximumPackageBytes || !allowed_url(url, policy_)) { sink.reply.error = DownloadError::kInvalidRequest; return sink.reply; }
    try {
        std::ofstream output(file, std::ios::binary | std::ios::trunc); sink.file = &output;
        if (!output) { sink.reply.error = DownloadError::kDownloadFailed; return sink.reply; }
        transfer(url, sink, policy_, timeout_ms); output.flush();
        if (!output && sink.reply.error == DownloadError::kNone) sink.reply.error = DownloadError::kDownloadFailed;
        output.close();
        if (output.fail() && sink.reply.error == DownloadError::kNone) sink.reply.error = DownloadError::kDownloadFailed;
        if (sink.reply.error == DownloadError::kNone && sink.reply.bytes != limit) sink.reply.error = DownloadError::kDownloadFailed;
        return sink.reply;
    } catch (...) { sink.reply.error = DownloadError::kDownloadFailed; return sink.reply; }
}
struct MetadataRequest::State {
    std::atomic<bool> cancelled{false}, finished{false}, callback_failed{false};
    std::thread thread;
};
MetadataRequest::MetadataRequest(std::unique_ptr<State> state) : state_(std::move(state)) {}
std::unique_ptr<MetadataRequest> MetadataRequest::start(std::string url, std::function<void(HttpReply)> callback) {
    auto state = std::make_unique<State>(); auto* shared = state.get();
    auto request = std::unique_ptr<MetadataRequest>(new MetadataRequest(std::move(state)));
    shared->thread = std::thread([shared, url = std::move(url), callback = std::move(callback)] {
        UpdateHttp http; const auto reply = http.get(url, [shared] { return shared->cancelled.load(); });
        try { if (callback) callback(reply); } catch (...) { shared->callback_failed.store(true); }
        shared->finished.store(true);
    });
    return request;
}
MetadataRequest::~MetadataRequest() { cancel(); if (state_->thread.joinable()) state_->thread.join(); }
void MetadataRequest::cancel() noexcept { state_->cancelled.store(true); }
bool MetadataRequest::finished() const noexcept { return state_->finished.load(); }
bool MetadataRequest::callback_failed() const noexcept { return state_->callback_failed.load(); }
}

#include "tencent_protocol.h"
#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <limits>

namespace coral::speech::detail {
namespace {
struct CurlRuntime {
    bool ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    ~CurlRuntime() { if (ready) curl_global_cleanup(); }
};
struct Transfer {
    CURL* easy = curl_easy_init();
    CURLM* multi = curl_multi_init();
    curl_slist* headers = nullptr;
    bool added = false;
    bool overflow = false;
    std::string body;
    ~Transfer() {
        if (added) curl_multi_remove_handle(multi, easy);
        if (multi) curl_multi_cleanup(multi);
        if (easy) curl_easy_cleanup(easy);
        if (headers) curl_slist_free_all(headers);
    }
};
std::size_t receive(char* bytes, std::size_t size, std::size_t count, void* context) noexcept {
    auto& transfer = *static_cast<Transfer*>(context);
    if (size && count > (std::numeric_limits<std::size_t>::max)() / size) return 0;
    const auto length = size * count;
    if (length > kMaxResponseBytes - transfer.body.size()) { transfer.overflow = true; return 0; }
    try { transfer.body.append(bytes, length); return length; } catch (...) { return 0; }
}
}
HttpResponse send_https(const HttpRequest& request, const RequestControl& control) {
    static CurlRuntime runtime;
    if (!runtime.ready) return {Failure::kNetwork};
    Transfer transfer;
    if (!transfer.easy || !transfer.multi) return {Failure::kNetwork};
    for (const auto& header : request.headers) {
        auto* updated = curl_slist_append(transfer.headers, (header.first + ": " + header.second).c_str());
        if (!updated) return {Failure::kInternal};
        transfer.headers = updated;
    }
    const auto timeout = static_cast<long>(control.timeout.count());
    const bool configured =
        curl_easy_setopt(transfer.easy, CURLOPT_URL, request.url.c_str()) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_PROTOCOLS_STR, "https") == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_FOLLOWLOCATION, 0L) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_CONNECTTIMEOUT_MS, (std::min)(5000L, timeout)) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_TIMEOUT_MS, timeout) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_HTTPHEADER, transfer.headers) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_POSTFIELDS, request.body.data()) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size())) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_WRITEFUNCTION, receive) == CURLE_OK &&
        curl_easy_setopt(transfer.easy, CURLOPT_WRITEDATA, &transfer) == CURLE_OK;
    if (!configured) return {Failure::kInternal};
    if (curl_multi_add_handle(transfer.multi, transfer.easy) != CURLM_OK) return {Failure::kNetwork};
    transfer.added = true;
    const auto deadline = std::chrono::steady_clock::now() + control.timeout;
    for (;;) {
        if (is_cancelled(control)) return {Failure::kCancelled};
        if (std::chrono::steady_clock::now() >= deadline) return {Failure::kTimeout};
        int running = 0;
        if (curl_multi_perform(transfer.multi, &running) != CURLM_OK) return {Failure::kNetwork};
        int pending = 0;
        while (const auto* message = curl_multi_info_read(transfer.multi, &pending)) {
            if (message->msg != CURLMSG_DONE) continue;
            if (transfer.overflow) return {Failure::kInvalidResponse};
            if (message->data.result == CURLE_OPERATION_TIMEDOUT) return {Failure::kTimeout};
            if (message->data.result != CURLE_OK) return {Failure::kNetwork};
            long status = 0;
            if (curl_easy_getinfo(transfer.easy, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK) return {Failure::kNetwork};
            return {Failure::kNone, status, std::move(transfer.body)};
        }
        if (!running) return {Failure::kNetwork};
        if (curl_multi_poll(transfer.multi, nullptr, 0, 50, nullptr) != CURLM_OK) return {Failure::kNetwork};
    }
}
}

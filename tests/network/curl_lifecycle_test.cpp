// 此编译单元调用真实 libcurl；另两个生产编译单元通过替身注入初始化失败。
#undef curl_global_init
#undef curl_global_cleanup
#undef curl_easy_init
#undef curl_multi_init
#undef curl_easy_escape
#undef curl_easy_unescape
#include <curl/curl.h>
#include "coralnet.h"
#include "update_http.h"
#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::atomic<bool> fail_init{true}, unowned_use{false};
std::atomic<int> init_calls{0}, cleanup_calls{0}, references{0};
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool owned() {
    if (references.load() > 0) return true;
    unowned_use.store(true);
    return false;
}
void check_final_cleanup() {
    if (references.load() != 0 || cleanup_calls.load() != 2 || unowned_use.load()) {
        std::cerr << "exit cleanup must release exactly the successful coralnet reference\n";
        std::_Exit(1);
    }
}
}

extern "C" CURLcode coral_test_global_init(long flags) {
    ++init_calls;
    if (fail_init.load()) return CURLE_FAILED_INIT;
    const auto status = curl_global_init(flags);
    if (status == CURLE_OK) ++references;
    return status;
}
extern "C" void coral_test_global_cleanup() {
    ++cleanup_calls;
    if (references.fetch_sub(1) <= 0) unowned_use.store(true);
    curl_global_cleanup();
}
extern "C" CURL* coral_test_easy_init() {
    return owned() ? curl_easy_init() : nullptr;
}
extern "C" CURLM* coral_test_multi_init() {
    return owned() ? curl_multi_init() : nullptr;
}
extern "C" char* coral_test_easy_escape(CURL* easy, const char* text, int length) {
    return owned() ? curl_easy_escape(easy, text, length) : nullptr;
}
extern "C" char* coral_test_easy_unescape(CURL* easy, const char* text, int length, int* output_length) {
    return owned() ? curl_easy_unescape(easy, text, length, output_length) : nullptr;
}

int main() {
    try {
        // 先登记观察者，使其在生产代码的退出清理之后核对最终引用。
        require(std::atexit(check_final_cleanup) == 0, "register final reference check");
        coralnet::RequestOptions options;
        options.url = "://";
        coralnet::NetResult result;
        coralnet::syncRequest(coralnet::HttpMethod::Get, options, &result);
        require(!result.success, "initialization failure must fail the request");
        require(coralnet::urlEncode("a b") == "a b" && coralnet::urlDecode("a%20b") == "a%20b",
            "URL helpers retain their failure fallback");
        auto asynchronous = std::async(std::launch::async, [&] {
            std::promise<coralnet::NetResult> completion;
            auto completed = completion.get_future();
            auto request = coralnet::asyncRequest(coralnet::HttpMethod::Get, options,
                [&](const std::string&, coralnet::NetResult reply) { completion.set_value(std::move(reply)); });
            return completed.get();
        });
        require(!asynchronous.get().success, "asynchronous initialization failure is delivered");
        coral::updates::UpdateHttp updates;
        const auto failed = updates.get("https://127.0.0.1:1/", [] { return false; }, 100);
        require(failed.error == coral::updates::DownloadError::kDownloadFailed, "update initialization failure is delivered");
        require(!unowned_use.load() && references.load() == 0 && cleanup_calls.load() == 0,
            "failed global initialization must not use curl objects or release an unowned reference");

        fail_init.store(false);
        const auto before = init_calls.load();
        std::vector<std::future<void>> workers;
        for (int index = 0; index < 16; ++index) {
            workers.push_back(std::async(std::launch::async, [] {
                require(coralnet::urlEncode("a b") == "a%20b", "initialization retry and URL encoding");
                require(coralnet::urlDecode("a%20b") == "a b", "URL decoding after initialization retry");
            }));
        }
        for (auto& worker : workers) worker.get();
        require(init_calls.load() == before + 1 && references.load() == 1 && cleanup_calls.load() == 0,
            "concurrent coralnet callers share one successful lifetime reference");
        const auto stable = init_calls.load();
        coralnet::globalInit();
        coralnet::globalShutdown();
        require(init_calls.load() == stable && references.load() == 1, "legacy shutdown cannot invalidate live users");
        const auto update = updates.get("https://127.0.0.1:1/", [] { return false; }, 100);
        require(update.error != coral::updates::DownloadError::kNone && cleanup_calls.load() == 1 && references.load() == 1,
            "update failure releases only its own global reference");
        require(!unowned_use.load(), "all curl operations retain an owning reference");
        std::cout << "initialization failure guards, retry, concurrent initialization and paired cleanup passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

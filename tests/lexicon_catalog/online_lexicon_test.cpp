#include "pch.h"
#include "online_lexicon_coordinator.h"
#include <iostream>
#include <stdexcept>

namespace fixture {
struct Request {
    coralnet::ResponseCallback response;
    coralnet::DownloadCallbacks download;
    std::string url, file;
    bool running = true, stopped = false, delivered = false;
};
std::vector<std::shared_ptr<Request>> requests;
std::vector<std::function<void()>> tasks;
std::filesystem::path root;
unsigned live_handles = 0, directory_calls = 0, compile_calls = 0;
bool allow_files = false;
void require(bool value) { if (!value) throw std::runtime_error("Online lexicon fixture failed"); }
void deliver(const std::shared_ptr<Request>& request, std::string body, bool success = true) {
    request->running = false; request->delivered = true;
    const coralnet::NetResult result(success, success ? coralnet::NetError::None : coralnet::NetError::Aborted, 200, {});
    if (request->response) request->response(body, result);
    if (request->download.onFinish) request->download.onFinish(result);
}
class Handle final : public coralnet::HttpRequest {
public:
    explicit Handle(std::shared_ptr<Request> request) : request_(std::move(request)) { ++live_handles; }
    ~Handle() override { stop(); if (!request_->delivered) deliver(request_, {}, false); --live_handles; }
    void stop() override { request_->stopped = true; request_->running = false; }
    bool isRunning() const override { return request_->running; }
private: std::shared_ptr<Request> request_;
};
void drain() {
    auto queued = std::move(tasks); tasks.clear();
    for (auto& task : queued) task();
}
const std::string catalog = "{\"data\":{\"data\":[{\"id\":7,\"title\":\"人工样本\",\"fileSize\":3,\"fileSha256\":\""
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}]}}";
}
namespace coralnet {
std::unique_ptr<HttpRequest> asyncRequest(HttpMethod method, const RequestOptions& options, ResponseCallback response, ProgressCallback) {
    fixture::require(method == HttpMethod::Get && options.timeoutMs == 10000);
    auto request = std::make_shared<fixture::Request>(); request->url = options.url; request->response = std::move(response);
    fixture::requests.push_back(request); return std::make_unique<fixture::Handle>(request);
}
std::unique_ptr<HttpRequest> asyncDownload(const std::string& url, const std::string& file, DownloadCallbacks callbacks, int timeout) {
    fixture::require(timeout == 0); // 保留旧传输层：不声称具备新的受限下载器。
    auto request = std::make_shared<fixture::Request>(); request->url = url; request->file = file; request->download = std::move(callbacks);
    fixture::requests.push_back(request); return std::make_unique<fixture::Handle>(request);
}
}
void appPostTask(std::function<void()> task) { fixture::tasks.push_back(std::move(task)); }
std::string wstrToUtf8(const std::wstring& text) {
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(bytes, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), bytes, nullptr, nullptr);
    return result;
}
namespace hlp {
std::filesystem::path getLocalLowCoralPinyinPath() {
    ++fixture::directory_calls; fixture::require(fixture::allow_files); return fixture::root;
}
}
namespace coral::runtime {
bool compile_lexicon_directory(const char*) { ++fixture::compile_calls; return false; }
}
// 与生产同一来源，仅替换 HTTP/应用队列和隔离的用户路径适配器。
#include "../../apps/settings_app/online_lexicon_coordinator.cpp"

int main(int argc, char** argv) {
    try {
        using namespace fixture;
        using namespace coral::lexicon_catalog;
        require(argc == 2);
        root = std::filesystem::absolute(std::filesystem::u8path(argv[1]))
            / ("run_" + std::to_string(::GetCurrentProcessId()) + "_" + std::to_string(::GetTickCount64()));
        std::filesystem::create_directories(root);
        {
            OnlineLexiconCoordinator disabled{LexiconService{}};
            disabled.check_now(); disabled.on_auto_check_timer(); disabled.stop();
            require(requests.empty() && !directory_calls && !compile_calls && !live_handles);
        }
        if (!configured_lexicon_service().available()) {
            OnlineLexiconCoordinator disabled;
            disabled.check_now(); disabled.on_auto_check_timer();
            require(requests.empty() && !directory_calls);
        }
        const LexiconService service("https://new.example.test/base");
        {
            OnlineLexiconCoordinator coordinator(service);
            coordinator.check_now(); require(requests.size() == 1 && live_handles == 1);
            const auto first = requests.back();
            require(first->url == "https://new.example.test/base/api/v1/dict?autoDownload=1");
            coordinator.on_auto_check_timer(); require(requests.size() == 1); // 进行中的请求不会重复发起。
            deliver(first, catalog); require(tasks.size() == 1);
            coordinator.stop(); require(!live_handles && first->stopped);
            drain(); deliver(first, catalog); drain();
            require(!directory_calls && !compile_calls && tasks.empty());
            coordinator.check_now(); const auto second = requests.back();
            deliver(first, catalog); require(tasks.empty()); // 旧一轮不能影响新一轮。
            deliver(second, "{\"data\":{\"data\":[]}}"); drain();
            require(!directory_calls);
            coordinator.check_now(); const auto third = requests.back();
            require(second->stopped && third != second);
            deliver(third, catalog); require(tasks.size() == 1);
        }
        drain(); require(!directory_calls && !compile_calls && !live_handles); // 析构后排队的任务被丢弃。
        deliver(requests.back(), catalog); require(tasks.empty()); // 析构后的工作线程回调被丢弃。
        allow_files = true;
        {
            OnlineLexiconCoordinator coordinator(service);
            coordinator.check_now(); deliver(requests.back(), catalog); drain();
            const auto location = requests.back();
            const auto calls = directory_calls; const auto count = requests.size();
            coordinator.stop();
            deliver(location, "{\"data\":{\"url\":\"https://new.example.test/late.csv\"}}"); drain();
            require(!live_handles && directory_calls == calls && requests.size() == count);
        }
        {
            OnlineLexiconCoordinator coordinator(service);
            coordinator.check_now(); deliver(requests.back(), catalog); drain();
            deliver(requests.back(), "{\"data\":{\"url\":\"https://new.example.test/late.csv\"}}"); drain();
            const auto download = requests.back(); const auto csv = std::filesystem::u8path(download->file);
            require(csv.parent_path() == root / "cell");
            { std::ofstream output(csv, std::ios::binary); output << "owned pending sentinel"; }
            const auto calls = directory_calls;
            coordinator.stop(); deliver(download, {}); drain();
            std::ifstream input(csv, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            require(bytes == "owned pending sentinel" && !compile_calls && calls == directory_calls && !live_handles);
            input.close(); std::filesystem::remove(csv); // 只匹配测试自身精确的待处理路径。
        }
        {
            OnlineLexiconCoordinator coordinator(service);
            coordinator.check_now(); deliver(requests.back(), catalog); drain();
            require(requests.back()->url == "https://new.example.test/base/api/v1/dict/getDownloadUrl?Id=7");
            deliver(requests.back(), "{\"data\":{\"url\":\"https://new.example.test/7.csv\"}}"); drain();
            const auto file = requests.back(); require(file->url == "https://new.example.test/7.csv");
            const auto csv = std::filesystem::u8path(file->file);
            require(csv.parent_path() == root / "cell" && csv.filename() == "7.kcell.tmp");
            { std::ofstream output(csv, std::ios::binary); output << "abc"; }
            deliver(file, {}); drain();
            std::ifstream input(root / "cell/7.kcell", std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(input)), {});
            require(bytes == std::string("\x22\x0d\x11", 3));
            require(compile_calls == 1 && !std::filesystem::exists(csv) && !std::filesystem::exists(root / "cell/7.csv"));
            require(!std::filesystem::exists(root / "cell/lexicon_record.json")); // 编译失败时不保存新记录。
            coordinator.stop(); require(!live_handles);
        }
        require(tasks.empty() && !live_handles);
        std::cout << "Disabled gates, stale callbacks, owned requests and artificial three-byte catalog flow passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

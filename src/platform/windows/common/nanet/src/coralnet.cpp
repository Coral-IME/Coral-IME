// coralnet 内部：libcurl 封装与任务队列
//
// 架构：每个异步请求一个独立线程（简单可靠；当前使用场景为低频请求，
// 更新下载 / dump 上传 / 在线搜词，无高并发需求。若未来需要并发优化，
// 可平滑替换为单线程 curl multi + epoll 式轮询，对外接口不变）。
#include "coralnet.h"

#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>

namespace coralnet {

namespace {

// ---------- 全局初始化 ----------

std::mutex curl_runtime_mutex;
bool curl_runtime_ready = false;

bool ensureGlobalInit()
{
    std::lock_guard<std::mutex> lock(curl_runtime_mutex);
    if (curl_runtime_ready) return true;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return false;
    // 仅为成功获取的引用登记清理；失败可由后续调用重试。
    if (std::atexit([]() { curl_global_cleanup(); }) != 0) {
        curl_global_cleanup();
        return false;
    }
    curl_runtime_ready = true;
    return true;
}

// ---------- curl 错误映射 ----------

NetError mapCurlError(CURLcode code)
{
    switch (code) {
    case CURLE_OPERATION_TIMEDOUT: return NetError::Timeout;
    case CURLE_COULDNT_CONNECT:    return NetError::ConnectionRefused;
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_CERTPROBLEM:    return NetError::SslError;
    case CURLE_ABORTED_BY_CALLBACK:
        return NetError::Aborted;
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
        return NetError::DnsError;
    case CURLE_WRITE_ERROR:
    case CURLE_READ_ERROR:
    case CURLE_FAILED_INIT:
        return NetError::IOError;
    default: return NetError::Unknown;
    }
}

// ---------- 请求任务基类 ----------

// 封装一次 curl easy 请求的生命周期与回调分发。
// 子类/使用方通过 sinks 决定应答数据的去向（内存 / 分块 / 文件）。
class CurlTask
{
public:
    CurlTask(RequestOptions options, ResponseCallback onResponse,
             ProgressCallback onProgress)
        : m_options(std::move(options))
        , m_onResponse(std::move(onResponse))
        , m_onProgress(std::move(onProgress))
    {
    }

    virtual ~CurlTask() { stop(); }

    void stop()
    {
        m_aborted = true;
        // curl 回调中检查 m_aborted 实现取消；简单起见不强制 interrupt
    }

    bool isRunning() const { return m_running.load(); }

    // 在当前线程执行（由外层线程包装器调用）
    virtual void run()
    {
        m_running = true;
        NetResult result;
        do {
            if (!ensureGlobalInit()) {
                result = NetResult(false, NetError::IOError, 0, "curl_global_init failed");
                break;
            }
            m_curl = curl_easy_init();
            if (!m_curl) {
                result = NetResult(false, NetError::Unknown, 0, "curl_easy_init failed");
                break;
            }
            if (!setup(m_curl, result))
                break;
            CURLcode rc = curl_easy_perform(m_curl);
            if (rc != CURLE_OK) {
                result = NetResult(false, mapCurlError(rc), 0, curl_easy_strerror(rc));
                break;
            }
            long status = 0;
            curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &status);
            if (status >= 200 && status < 300) {
                result = NetResult(true, NetError::None, status, "");
            } else {
                result = NetResult(false, NetError::HttpError, status,
                                   "HTTP status " + std::to_string(status));
            }
        } while (false);
        if (m_curl) {
            curl_easy_cleanup(m_curl);
            m_curl = nullptr;
        }
        m_running = false;
        // 回调统一在 finally 分发（含 stop 时的 Aborted 改写）
        if (m_aborted)
            result = NetResult(false, NetError::Aborted, result.httpStatus, "aborted");
        dispatchResult(serializeBody(), std::move(result));
    }

protected:
    // 结果分发：默认走 onResponse，子类可改写（如 DownloadTask 自行记录）
    virtual void dispatchResult(const std::string& body, NetResult result)
    {
        if (m_onResponse)
            m_onResponse(body, std::move(result));
    }

    // 子类配置额外选项（mime、写函数等）
    virtual bool setup(CURL* curl, NetResult& setupResult) = 0;
    // 最终交给 onResponse 的应答体（流式/文件模式返回空）
    virtual std::string serializeBody() { return std::move(m_body); }

    static size_t writeToString(void* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* self = static_cast<CurlTask*>(userdata);
        if (self->m_aborted)
            return 0; // 返回 0 触发 CURLE_WRITE_ERROR -> 不会误报，stop 会改写为 Aborted
        auto* s = &self->m_body;
        s->append(static_cast<const char*>(ptr), size * nmemb);
        return size * nmemb;
    }

    static int xferInfo(void* userdata, curl_off_t dlTotal, curl_off_t dlNow,
                        curl_off_t upTotal, curl_off_t upNow)
    {
        auto* self = static_cast<CurlTask*>(userdata);
        if (self->m_aborted)
            return 1; // 非零中止
        if (self->m_onProgress)
            self->m_onProgress(static_cast<int64_t>(dlNow), static_cast<int64_t>(dlTotal),
                               static_cast<int64_t>(upNow), static_cast<int64_t>(upTotal));
        return 0;
    }

    // 公共选项（URL/method/headers/timeout/ssl/redirect）
    bool setupCommon(CURL* curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, m_options.url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfo);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,
                         m_options.followRedirects ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,
                         m_options.verifyPeer ? 1L : 0L);
        if (m_options.timeoutMs > 0) {
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                             static_cast<long>(m_options.timeoutMs));
        }
        if (!m_options.headers.empty()) {
            m_headerList = nullptr;
            for (auto& kv : m_options.headers) {
                std::string line = kv.first + ": " + kv.second;
                m_headerList = curl_slist_append(m_headerList, line.c_str());
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, m_headerList);
        }
        return true;
    }

    void cleanupHeaders()
    {
        if (m_headerList) {
            curl_slist_free_all(m_headerList);
            m_headerList = nullptr;
        }
    }

    RequestOptions m_options;
    ResponseCallback m_onResponse;
    ProgressCallback m_onProgress;
    std::string m_body;
    CURL* m_curl = nullptr;
    curl_slist* m_headerList = nullptr;
    std::atomic<bool> m_aborted{false};
    std::atomic<bool> m_running{false};
};

// ---------- 普通/流式请求 ----------

class SimpleTask : public CurlTask
{
public:
    SimpleTask(HttpMethod method, RequestOptions options, ResponseCallback onResponse,
               ProgressCallback onProgress, ChunkCallback onChunk = nullptr)
        : CurlTask(std::move(options), std::move(onResponse), std::move(onProgress))
        , m_method(method)
        , m_onChunk(std::move(onChunk))
    {}

    static size_t writeToChunks(void* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* self = static_cast<SimpleTask*>(userdata);
        if (self->m_aborted)
            return 0;
        size_t total = size * nmemb;
        if (self->m_onChunk) {
            if (!self->m_onChunk(static_cast<const char*>(ptr), total))
                return 0; // 调用方要求中止
        }
        return total;
    }

protected:
    bool setup(CURL* curl, NetResult&) override
    {
        if (!setupCommon(curl))
            return false;
        if (m_onChunk) {
            // 流式：替换写函数为分块分发
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToChunks);
        }
        if (m_method == HttpMethod::Post) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, m_options.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(m_options.body.size()));
        }
        return true;
    }

private:
    HttpMethod m_method;
    ChunkCallback m_onChunk;
};

// ---------- multipart 上传 ----------

class UploadTask : public CurlTask
{
public:
    UploadTask(RequestOptions options, std::vector<MimePart> parts,
               ResponseCallback onResponse, ProgressCallback onProgress)
        : CurlTask(std::move(options), std::move(onResponse), std::move(onProgress))
        , m_parts(std::move(parts))
    {}

    ~UploadTask() override
    {
        if (m_mime) {
            curl_mime_free(m_mime);
            m_mime = nullptr;
        }
        cleanupHeaders();
    }

    static size_t readCallback(char* /*buffer*/, size_t /*size*/, size_t /*nmemb*/,
                               void* /*userdata*/)
    {
        // curl mime（curl_mime_add_file）自行读文件，无需自定义读回调
        return CURL_READFUNC_ABORT; // 不应被调用
    }

protected:
    bool setup(CURL* curl, NetResult& result) override
    {
        if (!setupCommon(curl))
            return false;
        m_mime = curl_mime_init(curl);
        if (!m_mime) {
            result = NetResult(false, NetError::Unknown, 0, "curl_mime_init failed");
            return false;
        }
        for (auto& part : m_parts) {
            curl_mimepart* cp = curl_mime_addpart(m_mime);
            curl_mime_name(cp, part.name.c_str());
            if (!part.filePath.empty()) {
                curl_mime_filedata(cp, part.filePath.c_str());
                if (!part.filename.empty())
                    curl_mime_filename(cp, part.filename.c_str());
            } else {
                curl_mime_data(cp, part.value.c_str(), part.value.size());
            }
            if (!part.contentType.empty())
                curl_mime_type(cp, part.contentType.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, m_mime);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        return true;
    }

private:
    std::vector<MimePart> m_parts;
    curl_mime* m_mime = nullptr;
};

// ---------- 文件下载 ----------

class DownloadTask : public CurlTask
{
public:
    DownloadTask(std::string url, std::string localPath, DownloadCallbacks callbacks,
                 int timeoutMs)
        : CurlTask(RequestOptions{std::move(url), {}, timeoutMs}, nullptr, nullptr)
        , m_localPath(std::move(localPath))
        , m_callbacks(std::move(callbacks))
    {}

    static size_t write_to_file(void* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* self = static_cast<DownloadTask*>(userdata);
        if (self->m_aborted || !self->m_file.is_open())
            return 0;
        self->m_file.write(static_cast<const char*>(ptr), size * nmemb);
        return self->m_file.good() ? size * nmemb : 0;
    }

    static int dlProgress(void* userdata, curl_off_t dlTotal, curl_off_t dlNow,
                          curl_off_t /*upTotal*/, curl_off_t /*upNow*/)
    {
        auto* self = static_cast<DownloadTask*>(userdata);
        if (self->m_aborted)
            return 1;
        if (self->m_callbacks.onProgress) {
            int percent = dlTotal > 0
                ? static_cast<int>(dlNow * 100 / dlTotal) : 0;
            self->m_callbacks.onProgress(percent, dlTotal > 0);
        }
        return 0;
    }

protected:
    bool setup(CURL* curl, NetResult& result) override
    {
        m_file.open(m_localPath, std::ios::binary | std::ios::trunc);
        if (!m_file.is_open()) {
            result = NetResult(false, NetError::IOError, 0,
                               "cannot open file: " + m_localPath);
            return false;
        }
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, dlProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
        if (m_options.timeoutMs > 0)
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                             static_cast<long>(m_options.timeoutMs));
        return true;
    }

    std::string serializeBody() override { return {}; }

    void run() override
    {
        CurlTask::run();
        m_file.close();
        if (m_callbacks.onFinish)
            m_callbacks.onFinish(m_lastResult);
    }

protected:
    // run() 中 onResponse 为空，需自行记录最终结果
    void dispatchResult(const std::string&, NetResult result) override
    {
        m_lastResult = std::move(result);
    }

    std::string m_localPath;
    DownloadCallbacks m_callbacks;
    std::ofstream m_file;
    NetResult m_lastResult;
};

// ---------- 线程包装：异步句柄 ----------

class AsyncHandle : public HttpRequest
{
public:
    explicit AsyncHandle(std::unique_ptr<CurlTask> task)
    {
        m_task = std::move(task);
        m_thread = std::thread([this] { m_task->run(); });
    }

    ~AsyncHandle() override
    {
        stop();
        if (m_thread.joinable())
            m_thread.join();
    }

    void stop() override { m_task->stop(); }
    bool isRunning() const override { return m_task->isRunning(); }

private:
    std::unique_ptr<CurlTask> m_task;
    std::thread m_thread;
};

} // namespace

// ---------- 公共 API 实现 ----------

std::unique_ptr<HttpRequest> asyncRequest(HttpMethod method,
                                          const RequestOptions& options,
                                          ResponseCallback onResponse,
                                          ProgressCallback onProgress)
{
    return std::make_unique<AsyncHandle>(
        std::make_unique<SimpleTask>(method, options, std::move(onResponse),
                                     std::move(onProgress)));
}

std::unique_ptr<HttpRequest> asyncStreamRequest(HttpMethod method,
                                                const RequestOptions& options,
                                                ChunkCallback onChunk,
                                                ResponseCallback onResponse,
                                                ProgressCallback onProgress)
{
    return std::make_unique<AsyncHandle>(
        std::make_unique<SimpleTask>(method, options, std::move(onResponse),
                                     std::move(onProgress), std::move(onChunk)));
}

std::string syncRequest(HttpMethod method, const RequestOptions& options,
                        NetResult* result)
{
    std::string body;
    NetResult r;
    SimpleTask task(method, options,
                    [&](const std::string& b, NetResult res) { body = b; r = std::move(res); },
                    nullptr);
    task.run();
    if (result)
        *result = std::move(r);
    return body;
}

std::unique_ptr<HttpRequest> asyncUpload(const RequestOptions& options,
                                         const std::vector<MimePart>& parts,
                                         ResponseCallback onResponse,
                                         ProgressCallback onProgress)
{
    return std::make_unique<AsyncHandle>(
        std::make_unique<UploadTask>(options, parts, std::move(onResponse),
                                     std::move(onProgress)));
}

std::unique_ptr<HttpRequest> asyncDownload(const std::string& url,
                                           const std::string& localFilePath,
                                           DownloadCallbacks callbacks,
                                           int timeoutMs)
{
    return std::make_unique<AsyncHandle>(
        std::make_unique<DownloadTask>(url, localFilePath, std::move(callbacks),
                                       timeoutMs));
}

std::string urlEncode(const std::string& s)
{
    if (!ensureGlobalInit()) return s;
    char* escaped = curl_easy_escape(nullptr, s.c_str(), static_cast<int>(s.size()));
    std::string out = escaped ? escaped : s;
    if (escaped)
        curl_free(escaped);
    return out;
}

std::string urlDecode(const std::string& s)
{
    if (!ensureGlobalInit()) return s;
    char* decoded = curl_easy_unescape(nullptr, s.c_str(), static_cast<int>(s.size()),
                                       nullptr);
    std::string out = decoded ? decoded : s;
    if (decoded)
        curl_free(decoded);
    return out;
}

void globalInit() { ensureGlobalInit(); }
void globalShutdown()
{
    // 成功初始化后由退出回调释放引用；保留幂等空操作，不能在仍有请求时提前清理。
}

const char* toCString(NetError error)
{
    switch (error) {
    case NetError::None: return "NetError::None";
    case NetError::Timeout: return "NetError::Timeout";
    case NetError::ConnectionRefused: return "NetError::ConnectionRefused";
    case NetError::SslError: return "NetError::SslError";
    case NetError::Aborted: return "NetError::Aborted";
    case NetError::DnsError: return "NetError::DnsError";
    case NetError::IOError: return "NetError::IOError";
    case NetError::HttpError: return "NetError::HttpError";
    default: return "NetError::Unknown";
    }
}

} // 命名空间 coralnet 结束

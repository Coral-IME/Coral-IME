// coralnet：网络库公共接口（libcurl 实现）
//
// 设计原则：
// - 纯 std 接口（string/wstring + std::function 回调），不依赖 Qt
// - 覆盖原 web 库（HttpAccessManager/FileDownloader/UpdateChecker）与
//   crashdump 上传（QHttpMultiPart）的全部能力
// - 异步：内部线程池执行 curl multi，回调在工作线程触发，调用方自行处理跨线程
// - SSL 由 curl 内置 openssl 处理，调用方无需关心证书细节
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_COMMON_NANET_INCLUDE_CORALNET_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_COMMON_NANET_INCLUDE_CORALNET_H_INCLUDED

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace coralnet {

// ---------- 错误与结果 ----------

enum class NetError
{
    None = 0,
    Timeout,            // 超时
    ConnectionRefused,  // 连接被拒
    SslError,           // SSL/TLS 错误
    Aborted,            // 主动取消
    DnsError,           // 域名解析失败
    IOError,            // 本地 IO（写文件等）失败
    HttpError,          // 服务器返回非 2xx
    Unknown
};

const char* toCString(NetError error);

struct NetResult
{
    bool success = false;
    NetError errorCode = NetError::None;
    long httpStatus = 0;      // HTTP 状态码（请求到达服务器时有值）
    std::string errorMsg;     // 人类可读错误描述（curl 错误串 / HTTP 状态行）

    NetResult() = default;
    NetResult(bool s, NetError e, long status, std::string msg)
        : success(s), errorCode(e), httpStatus(status)
        , errorMsg(std::move(msg))
    {
    }
};

// ---------- 请求选项 ----------

enum class HttpMethod { Get, Post };

// 请求头（顺序无关，键值对）
using HttpHeaders = std::map<std::string, std::string>;

struct RequestOptions
{
    std::string url;                 // 完整 URL（utf-8）
    HttpHeaders headers;             // 附加请求头
    int timeoutMs = 10000;           // 总超时；0 = 不限制
    bool verifyPeer = true;          // SSL 校验（默认开，特殊场景可关）
    bool followRedirects = true;     // 跟随 30x 重定向
    // POST 请求体；GET 忽略。multipart 上传请用 MultipartForm
    std::string body;
};

// ---------- 通用 HTTP 请求 ----------

// 应答回调（整体缓冲在内存中，适合 JSON 等小应答）
using ResponseCallback = std::function<void(const std::string& body, NetResult result)>;

// 应答分块回调（大文件下载场景，调用方写入文件流）
// 返回 false 表示中止传输（等同 stop()）
using ChunkCallback = std::function<bool(const char* data, size_t len)>;

// 进度回调（dlNow/dlTotal 为下载侧；上传场景 upNow/upTotal 有效）
using ProgressCallback = std::function<void(int64_t dlNow, int64_t dlTotal,
                                            int64_t upNow, int64_t upTotal)>;

// 异步请求句柄：持有可取消；析构时自动取消未完成请求
class HttpRequest
{
public:
    virtual ~HttpRequest() = default;
    virtual void stop() = 0;             // 主动取消（触发回调，result=Aborted）
    virtual bool isRunning() const = 0;
};

// 发起异步 HTTP 请求
// onResponse 必填；onProgress 可选。回调在 coralnet 工作线程触发。
std::unique_ptr<HttpRequest> asyncRequest(HttpMethod method,
                                          const RequestOptions& options,
                                          ResponseCallback onResponse,
                                          ProgressCallback onProgress = nullptr);

// 发起流式异步请求（应答不分块缓存，经 onChunk 逐块给出）
// 注意：流式模式下 onResponse 的 body 为空，仍用于传递最终 NetResult
std::unique_ptr<HttpRequest> asyncStreamRequest(HttpMethod method,
                                                const RequestOptions& options,
                                                ChunkCallback onChunk,
                                                ResponseCallback onResponse,
                                                ProgressCallback onProgress = nullptr);

// 同步请求（便捷函数；内部在调用线程直接执行，勿在 UI 线程长超时使用）
std::string syncRequest(HttpMethod method, const RequestOptions& options,
                        NetResult* result = nullptr);

// ---------- multipart 表单上传（崩溃 dump 上报等） ----------

struct MimePart
{
    std::string name;                        // 字段名
    std::string value;                       // 文本值（与 file 二选一）
    std::string filePath;                    // 文件路径（utf-8；设置后忽略 value）
    std::string contentType;                 // 例 "application/octet-stream"；空 = 自动
    std::string filename;                    // 上报的文件名；空 = 取 filePath 文件名
};

std::unique_ptr<HttpRequest> asyncUpload(const RequestOptions& options,
                                         const std::vector<MimePart>& parts,
                                         ResponseCallback onResponse,
                                         ProgressCallback onProgress = nullptr);

// ---------- 文件下载 ----------

// 下载到本地文件（自动创建/覆盖）。onProgress 的 percent 为 0-100；
// fileSizeKnown=false 表示服务器未给出 Content-Length。
struct DownloadCallbacks
{
    std::function<void(int percent, bool fileSizeKnown)> onProgress;
    std::function<void(NetResult result)> onFinish;
};

std::unique_ptr<HttpRequest> asyncDownload(const std::string& url,
                                           const std::string& localFilePath,
                                           DownloadCallbacks callbacks,
                                           int timeoutMs = 0);

// ---------- URL 编码 / 简单工具 ----------

std::string urlEncode(const std::string& s);        // utf-8
std::string urlDecode(const std::string& s);

// ---------- 全局初始化（可选） ----------
// 首次调用任何接口时会自动初始化；显式调用可控制初始化时机。
// 初始化失败时请求返回 IO 错误，URL 工具保留输入，后续调用可重试。
// 成功引用在进程退出时释放；globalShutdown 保留为空操作，调用方须先停止并等待请求。
void globalInit();
void globalShutdown();

} // 命名空间 coralnet 结束

#endif // CORALNET_H

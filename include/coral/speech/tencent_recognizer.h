#ifndef CORAL_INCLUDE_CORAL_SPEECH_TENCENT_RECOGNIZER_H_INCLUDED
#define CORAL_INCLUDE_CORAL_SPEECH_TENCENT_RECOGNIZER_H_INCLUDED

#include "coral/speech/recognizer.h"
#include <functional>
#include <utility>

namespace coral::speech {
struct TencentCredentials {
    std::string secret_id;
    std::string secret_key;
    std::string token;
};
struct TencentOptions { std::string engine = "16k_zh"; };
struct HttpRequest {
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};
struct HttpResponse {
    Failure failure = Failure::kNone;
    long status = 0;
    std::string body;
};
// 替身可用于离线测试；真实传输必须遵守超时、取消、TLS 和响应上限。
using HttpTransport = std::function<HttpResponse(const HttpRequest&, const RequestControl&)>;
class TencentRecognizer final : public Recognizer {
public:
    explicit TencentRecognizer(TencentCredentials credentials, TencentOptions options = {},
        HttpTransport transport = {});
    Result recognize(const AudioClip& audio, const RequestControl& control = {}) const override;
private:
    TencentCredentials credentials_;
    TencentOptions options_;
    HttpTransport transport_;
};
}
#endif

#ifndef CORAL_MODULES_SPEECH_TENCENT_PROTOCOL_H_INCLUDED
#define CORAL_MODULES_SPEECH_TENCENT_PROTOCOL_H_INCLUDED
#include "coral/speech/tencent_recognizer.h"
#include <string_view>
namespace coral::speech::detail {
bool is_cancelled(const RequestControl& control);
std::string sha256(std::string_view data);
std::string hmac_sha256(std::string key, std::string_view data);
std::string hex(std::string_view bytes);
HttpRequest make_request(const AudioClip& audio, const TencentCredentials& credentials,
    const TencentOptions& options, std::int64_t timestamp);
Result parse_response(const HttpResponse& response);
bool valid_audio(const AudioClip& audio, const TencentOptions& options);
HttpResponse send_https(const HttpRequest& request, const RequestControl& control);
}
#endif

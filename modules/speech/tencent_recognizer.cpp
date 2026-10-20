#include "tencent_protocol.h"
#include <algorithm>
#include <chrono>

namespace coral::speech {
TencentRecognizer::TencentRecognizer(TencentCredentials credentials, TencentOptions options, HttpTransport transport)
    : credentials_(std::move(credentials)), options_(std::move(options)),
      transport_(transport ? std::move(transport) : detail::send_https) {}
Result TencentRecognizer::recognize(const AudioClip& audio, const RequestControl& control) const {
    try {
        const auto start = std::chrono::steady_clock::now();
        if (detail::is_cancelled(control)) return {Failure::kCancelled};
        if (control.timeout.count() <= 0 || control.timeout > std::chrono::minutes(5)) return {Failure::kTimeout};
        auto valid_header = [](const std::string& value) {
            return value.size() <= 4096 && std::all_of(value.begin(), value.end(),
                [](unsigned char ch) { return ch > 32 && ch < 127 && ch != ',' && ch != ';'; });
        };
        if (credentials_.secret_id.empty() || credentials_.secret_key.empty() ||
            !valid_header(credentials_.secret_id) || !valid_header(credentials_.secret_key) || !valid_header(credentials_.token))
            return {Failure::kMissingCredentials};
        if (!detail::valid_audio(audio, options_) || options_.engine.size() > 64 ||
            !std::all_of(options_.engine.begin(), options_.engine.end(), [](unsigned char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
            })) return {Failure::kInvalidAudio};
        const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto request = detail::make_request(audio, credentials_, options_, timestamp);
        if (detail::is_cancelled(control)) return {Failure::kCancelled};
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        if (elapsed >= control.timeout) return {Failure::kTimeout};
        auto remaining = control;
        remaining.timeout -= elapsed;
        const auto response = transport_(request, remaining);
        if (detail::is_cancelled(control)) return {Failure::kCancelled};
        if (std::chrono::steady_clock::now() - start >= control.timeout) return {Failure::kTimeout};
        return detail::parse_response(response);
    } catch (...) { return {Failure::kInternal}; }
}
}

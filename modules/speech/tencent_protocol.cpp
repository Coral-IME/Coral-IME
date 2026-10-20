#include "tencent_protocol.h"
#include <picosha2.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <stdexcept>

namespace coral::speech::detail {
bool is_cancelled(const RequestControl& control) {
    return control.cancelled && control.cancelled->load(std::memory_order_relaxed);
}
std::string sha256(std::string_view data) {
    std::string bytes(32, '\0');
    picosha2::hash256(data.begin(), data.end(), bytes.begin(), bytes.end());
    return bytes;
}
std::string hex(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char ch : bytes) { result += digits[ch >> 4]; result += digits[ch & 15]; }
    return result;
}
std::string hmac_sha256(std::string key, std::string_view data) {
    if (key.size() > 64) key = sha256(key);
    key.resize(64, '\0');
    std::string inner(64, '\0'), outer(64, '\0');
    for (std::size_t i = 0; i < 64; ++i) { inner[i] = key[i] ^ 0x36; outer[i] = key[i] ^ 0x5c; }
    inner.append(data.data(), data.size());
    return sha256(outer + sha256(inner));
}
namespace {
std::string base64(const std::vector<std::uint8_t>& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned value = (unsigned(bytes[i]) << 16) |
            (i + 1 < bytes.size() ? unsigned(bytes[i + 1]) << 8 : 0) |
            (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        output += alphabet[value >> 18]; output += alphabet[(value >> 12) & 63];
        output += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=';
        output += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
    }
    return output;
}
bool string_field(const rapidjson::Value& object, const char* name, std::string& output) {
    const auto field = object.FindMember(name);
    if (field == object.MemberEnd() || !field->value.IsString()) return false;
    output.assign(field->value.GetString(), field->value.GetStringLength());
    return output.find('\0') == std::string::npos;
}
bool unique_members(const rapidjson::Value& object) {
    if (object.MemberCount() > 64) return false;
    for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it)
        for (auto other = object.MemberBegin(); other != it; ++other)
            if (it->name == other->name) return false;
    return true;
}
bool diagnostic_field(const std::string& field) {
    if (field.empty() || field.size() > 256) return false;
    for (unsigned char ch : field)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')) return false;
    return true;
}
}
HttpRequest make_request(const AudioClip& audio, const TencentCredentials& credentials,
    const TencentOptions& options, std::int64_t timestamp) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    const auto encoded = base64(audio.bytes);
    writer.StartObject();
    writer.Key("EngSerViceType"); writer.String(options.engine.c_str());
    writer.Key("SourceType"); writer.Int(1);
    writer.Key("VoiceFormat"); writer.String(audio.format == AudioFormat::kWav ? "wav" : "pcm");
    writer.Key("Data"); writer.String(encoded.data(), static_cast<rapidjson::SizeType>(encoded.size()));
    writer.Key("DataLen"); writer.Uint64(audio.bytes.size());
    writer.EndObject();
    HttpRequest request;
    request.url = "https://asr.tencentcloudapi.com/";
    request.body.assign(buffer.GetString(), buffer.GetSize());
    const std::time_t clock_value = static_cast<std::time_t>(timestamp);
    // gmtime 的共享存储不跨调用持有；用互斥锁兼容所有 C++17 平台。
    static std::mutex time_mutex;
    std::tm utc{};
    {
        std::lock_guard<std::mutex> lock(time_mutex);
        const auto* converted = std::gmtime(&clock_value);
        if (!converted) throw std::runtime_error("invalid timestamp");
        utc = *converted;
    }
    std::ostringstream date;
    date << std::put_time(&utc, "%Y-%m-%d");
    const auto scope = date.str() + "/asr/tc3_request";
    const std::string canonical = "POST\n/\n\ncontent-type:application/json; charset=utf-8\nhost:asr.tencentcloudapi.com\n\ncontent-type;host\n" + hex(sha256(request.body));
    const auto to_sign = "TC3-HMAC-SHA256\n" + std::to_string(timestamp) + '\n' + scope + '\n' + hex(sha256(canonical));
    const auto date_key = hmac_sha256("TC3" + credentials.secret_key, date.str());
    const auto service_key = hmac_sha256(date_key, "asr");
    const auto signing_key = hmac_sha256(service_key, "tc3_request");
    const auto authorization = "TC3-HMAC-SHA256 Credential=" + credentials.secret_id + '/' + scope +
        ", SignedHeaders=content-type;host, Signature=" + hex(hmac_sha256(signing_key, to_sign));
    request.headers = {{"Content-Type", "application/json; charset=utf-8"}, {"Host", "asr.tencentcloudapi.com"},
        {"X-TC-Action", "SentenceRecognition"}, {"X-TC-Version", "2019-06-14"},
        {"X-TC-Timestamp", std::to_string(timestamp)}, {"Authorization", authorization}};
    if (!credentials.token.empty()) request.headers.emplace_back("X-TC-Token", credentials.token);
    return request;
}
Result parse_response(const HttpResponse& response) {
    if (response.failure != Failure::kNone) return {response.failure};
    if (response.body.size() > kMaxResponseBytes || response.body.find('\0') != std::string::npos)
        return {Failure::kInvalidResponse};
    rapidjson::Document json;
    json.Parse<rapidjson::kParseValidateEncodingFlag>(response.body.data(), response.body.size());
    if (json.HasParseError() || !json.IsObject() || !json.HasMember("Response") || !json["Response"].IsObject())
        return {response.status == 200 ? Failure::kInvalidResponse : Failure::kHttp};
    const auto& value = json["Response"];
    // 拒绝歧义键，不能因解析器取首项而把错误响应误判为成功。
    if (!unique_members(json) || !unique_members(value)) return {Failure::kInvalidResponse};
    Result result;
    if (!string_field(value, "RequestId", result.request_id) || !diagnostic_field(result.request_id)) return {Failure::kInvalidResponse};
    if (value.HasMember("Error")) {
        const auto& error = value["Error"];
        if (!error.IsObject() || !unique_members(error) || !string_field(error, "Code", result.provider_code) || !diagnostic_field(result.provider_code))
            return {Failure::kInvalidResponse};
        result.failure = result.provider_code.rfind("AuthFailure", 0) == 0 ? Failure::kCredentials : Failure::kRejected;
        return result;
    }
    if (response.status != 200) { result.failure = Failure::kHttp; return result; }
    if (!string_field(value, "Result", result.text) || !value.HasMember("AudioDuration") ||
        !value["AudioDuration"].IsUint64() || value["AudioDuration"].GetUint64() > 60000)
        return {Failure::kInvalidResponse};
    // 静音可以成功返回空文本。
    result.audio_duration_ms = value["AudioDuration"].GetUint64();
    return result;
}
}

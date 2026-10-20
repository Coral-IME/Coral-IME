#include <windows.h>
#include <bcrypt.h>
#include <curl/curl.h>
#include <rapidjson/document.h>
#include "aliyun_protocol.h"
#include "coral/translation/languages.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace coral::translation {
namespace {
constexpr std::size_t kMaxReply = 1024 * 1024;
Result failed(std::uint64_t id, Failure failure) { return {id, Status::kFailed, {}, failure}; }
bool cancelled(const Cancellation& flag) { return flag && flag->load(); }
std::string hex(const unsigned char* bytes, std::size_t count) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (std::size_t i = 0; i < count; ++i) {
        result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15];
    }
    return result;
}
std::string digest(const std::string& value, const std::string* secret = nullptr) {
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<unsigned char> object;
        ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } state;
    DWORD size = 0, returned = 0;
    if (BCryptOpenAlgorithmProvider(&state.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
            secret ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0) < 0 ||
        BCryptGetProperty(state.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &returned, 0) < 0)
        throw std::runtime_error("crypto initialization");
    state.object.resize(size);
    std::array<unsigned char, 32> output{};
    if (BCryptCreateHash(state.algorithm, &state.hash, state.object.data(), size,
            secret ? reinterpret_cast<PUCHAR>(const_cast<char*>(secret->data())) : nullptr,
            secret ? static_cast<ULONG>(secret->size()) : 0, 0) < 0 ||
        BCryptHashData(state.hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), 0) < 0 ||
        BCryptFinishHash(state.hash, output.data(), static_cast<ULONG>(output.size()), 0) < 0)
        throw std::runtime_error("crypto signing");
    // 哈希必须先于其对象缓冲区释放之前销毁。
    BCryptDestroyHash(state.hash); state.hash = nullptr;
    return hex(output.data(), output.size());
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("invalid unicode");
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr))
        throw std::runtime_error("invalid unicode");
    return result;
}
std::wstring wide(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size)) return {};
    return result;
}
bool valid_key(const std::string& value) {
    if (value.empty() || value.size() > 512) return false;
    for (unsigned char ch : value) if (ch <= 32 || ch >= 127 || ch == ',' || ch == ';') return false;
    return true;
}
std::string utc_now() {
    SYSTEMTIME time{}; GetSystemTime(&time);
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ", time.wYear,
        time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return buffer;
}
struct CurlLease {
    bool acquired = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    ~CurlLease() { if (acquired) curl_global_cleanup(); }
};
struct Transfer {
    CURL* easy = curl_easy_init();
    CURLM* multi = curl_multi_init();
    curl_slist* headers = nullptr;
    bool added = false;
    std::string response;
    ~Transfer() {
        if (added) curl_multi_remove_handle(multi, easy);
        if (multi) curl_multi_cleanup(multi);
        if (easy) curl_easy_cleanup(easy);
        if (headers) curl_slist_free_all(headers);
    }
};
size_t receive(char* bytes, size_t size, size_t count, void* opaque) noexcept {
    auto& state = *static_cast<Transfer*>(opaque);
    if (size && count > SIZE_MAX / size) return 0;
    const auto total = size * count;
    if (total > kMaxReply - state.response.size()) return 0;
    try { state.response.append(bytes, total); return total; } catch (...) { return 0; }
}
}

std::string encode_form(std::string_view value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string output;
    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') output += ch;
        else { output += '%'; output += digits[ch >> 4]; output += digits[ch & 15]; }
    }
    return output;
}
std::string sign_rpc(const std::string& method, const std::string& query, const std::string& body,
    std::map<std::string, std::string>& headers, const Credentials& credentials) {
    headers.erase("authorization");
    headers["x-acs-content-sha256"] = digest(body);
    std::string canonical_headers, names;
    for (const auto& entry : headers) {
        if (entry.first != "host" && entry.first != "content-type" && entry.first.rfind("x-acs-", 0) != 0) continue;
        canonical_headers += entry.first + ':' + entry.second + '\n';
        if (!names.empty()) names += ';';
        names += entry.first;
    }
    const auto canonical = method + "\n/\n" + query + '\n' + canonical_headers + '\n' + names + '\n' + headers["x-acs-content-sha256"];
    const auto signature = digest("ACS3-HMAC-SHA256\n" + digest(canonical), &credentials.access_key_secret);
    return "ACS3-HMAC-SHA256 Credential=" + credentials.access_key_id + ",SignedHeaders=" + names + ",Signature=" + signature;
}
Result parse_translation_response(std::uint64_t id, long http_status, const std::string& body) {
    if (body.size() > kMaxReply) return failed(id, Failure::kInvalidResponse);
    rapidjson::Document json;
    json.Parse<rapidjson::kParseValidateEncodingFlag>(body.data(), body.size());
    if (json.HasParseError() || !json.IsObject() || !json.HasMember("Code")) return failed(id, Failure::kInvalidResponse);
    const auto& value = json["Code"];
    std::string code;
    if (value.IsString()) code.assign(value.GetString(), value.GetStringLength());
    else if (value.IsInt()) code = std::to_string(value.GetInt());
    if (http_status != 200 || code != "200") {
        if (code.rfind("InvalidAccessKey", 0) == 0 || code.rfind("Signature", 0) == 0 || code == "IncompleteSignature") return failed(id, Failure::kCredentials);
        if (code == "10005") return failed(id, Failure::kLanguage);
        return failed(id, Failure::kRejected);
    }
    if (!json.HasMember("Data") || !json["Data"].IsObject() || !json["Data"].HasMember("Translated") || !json["Data"]["Translated"].IsString())
        return failed(id, Failure::kInvalidResponse);
    const auto& text = json["Data"]["Translated"];
    std::string translated(text.GetString(), text.GetStringLength());
    if (translated.find('\0') != std::string::npos || translated.find_first_not_of(" \t\r\n") == std::string::npos) return failed(id, Failure::kInvalidResponse);
    auto result = wide(translated);
    if (result.empty()) return failed(id, Failure::kInvalidResponse);
    return {id, Status::kReady, {std::move(result)}};
}
Result translate_general(const Request& request, const Credentials& credentials, const Cancellation& stop) {
    try {
        if (cancelled(stop)) return failed(request.id, Failure::kCancelled);
        if (!valid_key(credentials.access_key_id) || !valid_key(credentials.access_key_secret)) return {request.id, Status::kMissingKey, {}};
        if (request.text.empty() || request.text.size() > 5000 || request.source_language != "zh" || !supported_target(request.target_language))
            return failed(request.id, Failure::kLanguage);
        const auto body = "FormatType=text&SourceLanguage=zh&TargetLanguage=" + encode_form(request.target_language) +
            "&SourceText=" + encode_form(utf8(request.text)) + "&Scene=general";
        std::array<unsigned char, 16> random{};
        if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return failed(request.id, Failure::kInternal);
        std::map<std::string, std::string> headers{{"host", "mt.aliyuncs.com"}, {"x-acs-action", "TranslateGeneral"},
            {"x-acs-version", "2018-10-12"}, {"x-acs-date", utc_now()}, {"x-acs-signature-nonce", hex(random.data(), random.size())},
            {"content-type", "application/x-www-form-urlencoded"}};
        headers["authorization"] = sign_rpc("POST", "", body, headers, credentials);
        headers["accept"] = "application/json";
        CurlLease lease;
        if (!lease.acquired) return failed(request.id, Failure::kNetwork);
        Transfer transfer;
        if (!transfer.easy || !transfer.multi) return failed(request.id, Failure::kNetwork);
        for (const auto& entry : headers) {
            auto* appended = curl_slist_append(transfer.headers, (entry.first + ": " + entry.second).c_str());
            if (!appended) return failed(request.id, Failure::kInternal);
            transfer.headers = appended;
        }
        const bool configured =
            curl_easy_setopt(transfer.easy, CURLOPT_URL, "https://mt.aliyuncs.com/") == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_PROTOCOLS_STR, "https") == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_FOLLOWLOCATION, 0L) == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L) == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_TIMEOUT_MS, 15000L) == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_HTTPHEADER, transfer.headers) == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_POSTFIELDS, body.c_str()) == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size())) == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_WRITEFUNCTION, receive) == CURLE_OK &&
            curl_easy_setopt(transfer.easy, CURLOPT_WRITEDATA, &transfer) == CURLE_OK;
        if (!configured || curl_multi_add_handle(transfer.multi, transfer.easy) != CURLM_OK) return failed(request.id, Failure::kNetwork);
        transfer.added = true;
        const auto started = std::chrono::steady_clock::now();
        for (;;) {
            if (cancelled(stop)) return failed(request.id, Failure::kCancelled);
            if (std::chrono::steady_clock::now() - started > std::chrono::seconds(15)) return failed(request.id, Failure::kTimeout);
            int running = 0;
            if (curl_multi_perform(transfer.multi, &running) != CURLM_OK) return failed(request.id, Failure::kNetwork);
            if (!running) {
                int queued = 0;
                const auto* message = curl_multi_info_read(transfer.multi, &queued);
                if (!message || message->msg != CURLMSG_DONE) return failed(request.id, Failure::kNetwork);
                if (message->data.result != CURLE_OK) {
                    auto failure = failed(request.id, message->data.result == CURLE_OPERATION_TIMEDOUT ? Failure::kTimeout : Failure::kNetwork);
                    failure.transport_error = static_cast<int>(message->data.result);
                    return failure;
                }
                if (cancelled(stop)) return failed(request.id, Failure::kCancelled);
                long status = 0;
                curl_easy_getinfo(transfer.easy, CURLINFO_RESPONSE_CODE, &status);
                return parse_translation_response(request.id, status, transfer.response);
            }
            if (curl_multi_poll(transfer.multi, nullptr, 0, 100, nullptr) != CURLM_OK) return failed(request.id, Failure::kNetwork);
        }
    } catch (...) { return failed(request.id, Failure::kInternal); }
}
}

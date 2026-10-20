#include "aliyun_protocol.h"
#include "credential_protection.h"
#include "coral/translation/languages.h"
#include <iostream>
#include <stdexcept>

using namespace coral::translation;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    try {
        Credentials credentials{"YourAccessKeyId", "YourAccessKeySecret"};
        std::map<std::string, std::string> headers{{"host", "ecs.cn-shanghai.aliyuncs.com"},
            {"x-acs-action", "RunInstances"}, {"x-acs-version", "2014-05-26"},
            {"x-acs-date", "2023-10-26T10:22:32Z"}, {"x-acs-signature-nonce", "3156853299f313e23d1673dc12e1703d"}};
        const auto signature = sign_rpc("POST", "ImageId=win2019_1809_x64_dtc_zh-cn_40G_alibase_20230811.vhd&RegionId=cn-shanghai", "", headers, credentials);
        require(signature.find("Signature=06563a9e1b43f5dfe96b81484da74bceab24a1d853912eee15083a6f0f3283c0") != std::string::npos,
            "Native signature differs from official fixture");
        require(encode_form("你好 +&=%\n") == "%E4%BD%A0%E5%A5%BD%20%2B%26%3D%25%0A", "UTF-8 form encoding is incorrect");
        const auto success = parse_translation_response(7, 200, R"({"Code":200,"Data":{"Translated":"Hello\nWorld"}})");
        require(success.status == Status::kReady && success.translations.front() == L"Hello\nWorld", "Translation lost text or newlines");
        for (const auto* body : {"[]", "bad json", R"({"Code":200})", R"({"Code":200,"Data":{"Translated":""}})",
            R"({"Code":200,"Data":{"Translated":"\u0000"}})"})
            require(parse_translation_response(7, 200, body).status == Status::kFailed, "Invalid translation accepted");
        require(parse_translation_response(7, 200, R"({"Code":10005})").failure == Failure::kLanguage, "Language error misclassified");
        require(parse_translation_response(7, 403, R"({"Code":"InvalidAccessKeyId.NotFound","Message":"sensitive text"})").failure == Failure::kCredentials,
            "Credentials error misclassified");
        require(parse_translation_response(7, 200, R"({"Code":10010})").failure == Failure::kRejected, "Business failure treated as success");
        require(parse_translation_response(7, 500, R"({"Code":200,"Data":{"Translated":"Hello"}})").status == Status::kFailed, "HTTP error treated as success");
        const auto cancel = std::make_shared<std::atomic<bool>>(true);
        Request request; request.id = 7; request.text = L"你好";
        require(translate_general(request, credentials, cancel).failure == Failure::kCancelled, "Cancelled request was sent");
        require(translate_general(request, {}, {}).status == Status::kMissingKey, "Missing credentials were sent");
        request.target_language = "invalid";
        require(translate_general(request, credentials, {}).failure == Failure::kLanguage, "Invalid target was sent");
        require(request.source_language == "zh" && kTargetLanguages.size() == 15 && std::string(kTargetLanguages[0].code) == "en", "Wrong language defaults");
        std::string encrypted, decoded_id, decoded_secret;
        require(coral::preferences::protect_credentials(credentials.access_key_id, credentials.access_key_secret, encrypted), "DPAPI encryption failed");
        require(encrypted.find(credentials.access_key_id) == std::string::npos && encrypted.find(credentials.access_key_secret) == std::string::npos,
            "Plaintext credentials in persistent value");
        require(coral::preferences::unprotect_credentials(encrypted, decoded_id, decoded_secret) &&
            decoded_id == credentials.access_key_id && decoded_secret == credentials.access_key_secret, "DPAPI credentials cannot be restored");
        require(!coral::preferences::unprotect_credentials("corrupted", decoded_id, decoded_secret), "Corrupt credential blob accepted");
        require(!coral::preferences::protect_credentials("id-only", "", encrypted), "Partial credentials accepted");
        require(coral::preferences::protect_credentials("", "", encrypted) && encrypted.empty(), "Credentials cannot be cleared");
        std::cout << "[PASS] ACS3 fixture, UTF-8, response failures, language defaults, cancellation and DPAPI\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

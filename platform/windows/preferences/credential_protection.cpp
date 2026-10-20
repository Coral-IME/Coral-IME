#include "credential_protection.h"
#include <windows.h>
#include <wincrypt.h>
#include <vector>

namespace coral::preferences {
namespace {
bool valid(const std::string& value) {
    return !value.empty() && value.size() <= 512 && value.find_first_of("\r\n") == std::string::npos &&
        value.find('\0') == std::string::npos;
}
}
bool protect_credentials(const std::string& id, const std::string& secret, std::string& encrypted) {
    if (id.empty() && secret.empty()) { encrypted.clear(); return true; }
    if (!valid(id) || !valid(secret)) return false;
    std::string plaintext = id + '\n' + secret;
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()), reinterpret_cast<BYTE*>(plaintext.data())}, output{};
    const bool protected_ok = ::CryptProtectData(&input, L"Coral translation credentials", nullptr,
        nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) != FALSE;
    ::SecureZeroMemory(plaintext.data(), plaintext.size());
    if (!protected_ok) return false;
    DWORD count = 0;
    bool ok = ::CryptBinaryToStringA(output.pbData, output.cbData,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &count) != FALSE;
    std::string encoded(count, '\0');
    if (ok) ok = ::CryptBinaryToStringA(output.pbData, output.cbData,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &count) != FALSE;
    ::LocalFree(output.pbData);
    if (!ok) return false;
    encoded.resize(count);
    if (!encoded.empty() && encoded.back() == '\0') encoded.pop_back();
    encrypted = std::move(encoded);
    return true;
}
bool unprotect_credentials(const std::string& encrypted, std::string& id, std::string& secret) {
    if (encrypted.empty() || encrypted.size() > 8192) return false;
    DWORD count = 0;
    if (!::CryptStringToBinaryA(encrypted.c_str(), static_cast<DWORD>(encrypted.size()),
        CRYPT_STRING_BASE64, nullptr, &count, nullptr, nullptr)) return false;
    std::vector<BYTE> bytes(count);
    if (!::CryptStringToBinaryA(encrypted.c_str(), static_cast<DWORD>(encrypted.size()),
        CRYPT_STRING_BASE64, bytes.data(), &count, nullptr, nullptr)) return false;
    DATA_BLOB input{count, bytes.data()}, output{};
    if (!::CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &output)) return false;
    std::string plaintext(reinterpret_cast<const char*>(output.pbData), output.cbData);
    ::SecureZeroMemory(output.pbData, output.cbData);
    ::LocalFree(output.pbData);
    const auto separator = plaintext.find('\n');
    bool ok = separator != std::string::npos;
    if (ok) {
        auto decoded_id = plaintext.substr(0, separator), decoded_secret = plaintext.substr(separator + 1);
        ok = valid(decoded_id) && valid(decoded_secret);
        if (ok) { id = std::move(decoded_id); secret = std::move(decoded_secret); }
    }
    ::SecureZeroMemory(plaintext.data(), plaintext.size());
    return ok;
}
}

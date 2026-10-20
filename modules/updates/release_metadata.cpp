#include "coral/updates/release_metadata.h"
#include <rapidjson/document.h>
#include <algorithm>
#include <set>

namespace coral::updates {
namespace {
bool unique_object(const rapidjson::Value& value) {
    if (!value.IsObject()) return false;
    std::set<std::string> keys;
    for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
        const std::string key(member->name.GetString(), member->name.GetStringLength());
        if (key.find('\0') != std::string::npos || !keys.insert(key).second) return false;
    }
    return true;
}
bool parse_response(std::string_view json, rapidjson::Document& document) {
    if (json.empty() || json.size() > kMaximumMetadataBytes || json.find('\0') != std::string_view::npos) return false;
    document.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    return !document.HasParseError() && unique_object(document)
        && document.HasMember("data") && unique_object(document["data"]);
}
bool text_field(const rapidjson::Value& value, std::size_t maximum, std::string& result) {
    if (!value.IsString() || value.GetStringLength() > maximum) return false;
    std::string candidate(value.GetString(), value.GetStringLength());
    if (candidate.find('\0') != std::string::npos) return false;
    result = std::move(candidate);
    return true;
}
int hex_digit(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}
}

bool parse_release_version(std::string_view text, ReleaseVersion& result) noexcept {
    if (text.empty() || text.size() > 23) return false;
    ReleaseVersion candidate;
    std::size_t cursor = 0;
    for (unsigned part = 0; part != 4; ++part) {
        const auto begin = cursor;
        unsigned value = 0;
        while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
            value = value * 10 + static_cast<unsigned>(text[cursor++] - '0');
            if (value > 65535) return false;
        }
        if (cursor == begin || (cursor - begin > 1 && text[begin] == '0')) return false;
        candidate.components[part] = static_cast<std::uint16_t>(value);
        if (part != 3 && (cursor == text.size() || text[cursor++] != '.')) return false;
    }
    if (cursor != text.size()) return false;
    result = candidate;
    return true;
}
std::string format_release_version(const ReleaseVersion& version) {
    std::string text;
    for (const auto part : version.components) {
        if (!text.empty()) text += '.';
        text += std::to_string(part);
    }
    return text;
}
bool is_newer_version(const ReleaseVersion& candidate, const ReleaseVersion& installed) noexcept {
    return installed.components < candidate.components;
}
bool parse_release_metadata(std::string_view json, ReleaseMetadata& result) {
    rapidjson::Document document;
    if (!parse_response(json, document)) return false;
    const auto& data = document["data"];
    if (!data.HasMember("id") || !data["id"].IsUint() || !data["id"].GetUint() || data["id"].GetUint() > INT32_MAX
        || !data.HasMember("version") || !data.HasMember("fileSize") || !data["fileSize"].IsUint64()
        || !data["fileSize"].GetUint64() || data["fileSize"].GetUint64() > kMaximumPackageBytes
        || !data.HasMember("fileSha256")) return false;
    ReleaseMetadata candidate;
    std::string version, hash;
    if (!text_field(data["version"], 23, version) || !parse_release_version(version, candidate.version)
        || !text_field(data["fileSha256"], 64, hash) || hash.size() != 64) return false;
    for (unsigned index = 0; index != 32; ++index) {
        const auto high = hex_digit(hash[index * 2]), low = hex_digit(hash[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        candidate.package.sha256[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    if (data.HasMember("description") && !text_field(data["description"], 16384, candidate.description)) return false;
    candidate.release_id = data["id"].GetUint();
    candidate.package.byte_count = data["fileSize"].GetUint64();
    result = std::move(candidate);
    return true;
}
bool parse_download_location(std::string_view json, std::string& result) {
    rapidjson::Document document;
    if (!parse_response(json, document)) return false;
    const auto& data = document["data"];
    std::string candidate;
    if (!data.HasMember("url") || !text_field(data["url"], 8192, candidate)
        || candidate.rfind("https://", 0) != 0) return false;
    const auto authority_end = candidate.find_first_of("/?#", 8);
    const auto authority = std::string_view(candidate).substr(8, authority_end == std::string::npos ? candidate.size() - 8 : authority_end - 8);
    if (authority.empty() || authority.find('@') != std::string_view::npos) return false;
    for (const unsigned char character : candidate)
        if (character <= 32 || character == 127 || character == '\\') return false;
    result = std::move(candidate);
    return true;
}
}

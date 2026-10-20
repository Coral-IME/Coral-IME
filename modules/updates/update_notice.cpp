#include "coral/updates/update_notice.h"
#include <rapidjson/document.h>
#include <set>

namespace coral::updates {
namespace {
bool unique_object(const rapidjson::Value& value) {
    if (!value.IsObject()) return false;
    std::set<std::string> keys;
    for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
        std::string key(it->name.GetString(), it->name.GetStringLength());
        if (key.find('\0') != std::string::npos || !keys.insert(key).second) return false;
    }
    return true;
}
bool ascii_segment(std::string_view value) noexcept {
    if (value.empty() || value == "." || value == "..") return false;
    for (const char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    return true;
}
bool text(const rapidjson::Value& value, std::size_t limit, std::string& result) {
    if (!value.IsString() || value.GetStringLength() > limit) return false;
    std::string candidate(value.GetString(), value.GetStringLength());
    for (const unsigned char c : candidate)
        if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127) return false;
    result = std::move(candidate);
    return true;
}
}
bool is_release_page_url(std::string_view url) noexcept {
    constexpr std::string_view prefix = "https://github.com/";
    if (url.size() > 2048 || url.substr(0, prefix.size()) != prefix) return false;
    auto path = url.substr(prefix.size());
    const auto owner_end = path.find('/');
    if (owner_end == std::string_view::npos || !ascii_segment(path.substr(0, owner_end))) return false;
    path.remove_prefix(owner_end + 1);
    const auto repo_end = path.find('/');
    if (repo_end == std::string_view::npos || !ascii_segment(path.substr(0, repo_end))) return false;
    path.remove_prefix(repo_end);
    constexpr std::string_view tag_prefix = "/releases/tag/";
    return path.substr(0, tag_prefix.size()) == tag_prefix && ascii_segment(path.substr(tag_prefix.size()));
}
bool parse_update_notice(std::string_view json, UpdateNotice& result) {
    if (json.empty() || json.size() > kMaximumMetadataBytes || json.find('\0') != std::string_view::npos) return false;
    rapidjson::Document doc;
    doc.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if (doc.HasParseError() || !unique_object(doc) || !doc.HasMember("schema_version") ||
        !doc["schema_version"].IsUint() ||
        (doc["schema_version"].GetUint() != 1 && doc["schema_version"].GetUint() != 2) ||
        !doc.HasMember("data") || !unique_object(doc["data"])) return false;
    const auto& data = doc["data"];
    if (!data.HasMember("version") || !data.HasMember("description")) return false;
    UpdateNotice candidate;
    std::string version;
    if (!text(data["version"], 23, version) || !parse_release_version(version, candidate.version) ||
        !text(data["description"], 16384, candidate.description)) return false;
    if (doc["schema_version"].GetUint() == 2) {
        ReleaseMetadata package;
        if (!parse_release_metadata(json, package)) return false;
        candidate.package = std::move(package);
    } else if (!data.HasMember("release_url") || !text(data["release_url"], 2048, candidate.release_url)
        || !is_release_page_url(candidate.release_url)) return false;
    result = std::move(candidate);
    return true;
}
bool auto_check_due(std::int64_t now_utc, std::int64_t last_attempt_utc) noexcept {
    return now_utc > 0 && (last_attempt_utc <= 0 || now_utc < last_attempt_utc || now_utc - last_attempt_utc >= 86400);
}
}

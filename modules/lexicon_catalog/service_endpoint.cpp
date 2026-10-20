#include "coral/lexicon_catalog/service_endpoint.h"
#include "lexicon_service_config.h"
#include <stdexcept>
#include <string_view>
#include <utility>

namespace coral::lexicon_catalog {
namespace {
bool ascii_host(char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9') || value == '.' || value == '-';
}
bool valid_base(std::string_view text) {
    if (text.empty()) return true;
    if (text.size() > 8192 || text.substr(0, 8) != "https://") return false;
    const auto slash = text.find('/', 8);
    const auto authority = text.substr(8, slash == std::string_view::npos ? text.size() - 8 : slash - 8);
    const auto colon = authority.find(':');
    const auto host = authority.substr(0, colon);
    if (host.empty()) return false;
    for (const auto value : host) if (!ascii_host(value)) return false;
    if (colon != std::string_view::npos) {
        const auto port = authority.substr(colon + 1);
        if (port.empty()) return false;
        std::uint32_t number = 0;
        for (const auto value : port) {
            if (value < '0' || value > '9') return false;
            number = number * 10 + static_cast<unsigned>(value - '0');
            if (number > 65535) return false;
        }
        if (!number) return false;
    }
    if (slash != std::string_view::npos)
        for (const auto value : text.substr(slash))
            if (!ascii_host(value) && value != '/' && value != '_' && value != '~') return false;
    return true;
}
}
LexiconService::LexiconService(std::string base) {
    if (!valid_base(base)) throw std::invalid_argument("Invalid HTTPS lexicon service base");
    while (!base.empty() && base.back() == '/') base.pop_back();
    base_ = std::move(base);
}
std::string LexiconService::catalog_url() const {
    return available() ? base_ + "/api/v1/dict?autoDownload=1" : std::string{};
}
std::string LexiconService::location_url(std::uint32_t id) const {
    return available() && id && id <= INT32_MAX
        ? base_ + "/api/v1/dict/getDownloadUrl?Id=" + std::to_string(id) : std::string{};
}
const LexiconService& configured_lexicon_service() {
    static const LexiconService service(kLexiconServiceBase);
    return service;
}
}

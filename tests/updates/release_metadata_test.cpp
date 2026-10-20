#include "coral/updates/release_metadata.h"
#include "coral/updates/update_service.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value) { if (!value) throw std::runtime_error("Release contract fixture failed"); }
std::string response(std::string extra = {}) {
    return "{\"data\":{\"id\":7,\"version\":\"1.2.3.4\",\"fileSize\":3,\"fileSha256\":\""
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\",\"description\":\"人工版本\"" + extra + "}}";
}
}
int main() {
    using namespace coral::updates;
    try {
        ReleaseVersion version;
        require(parse_release_version("65535.0.1.65535", version));
        require(format_release_version(version) == "65535.0.1.65535");
        const auto saved = version;
        for (const auto text : {"", "1.2.3", "1.2.3.4.5", "01.2.3.4", "1..3.4", "+1.2.3.4", "1.2.3.-4",
            "65536.0.0.0", "999999999999999.0.0.0", "1.2.3.4x", "1.2.3.4/../../escape", " 1.2.3.4"}) {
            require(!parse_release_version(text, version)); require(version.components == saved.components);
        }
        ReleaseVersion older, newer;
        require(parse_release_version("1.9.0.0", older) && parse_release_version("1.10.0.0", newer));
        require(is_newer_version(newer, older) && !is_newer_version(older, newer) && !is_newer_version(newer, newer));
        ReleaseMetadata metadata;
        require(parse_release_metadata(response(), metadata));
        require(metadata.release_id == 7 && metadata.package.byte_count == 3 && metadata.package.sha256[0] == 0xba
            && metadata.package.sha256[31] == 0xad && metadata.description == u8"人工版本");
        const auto valid = response();
        const auto changed = [&](const std::string& from, const std::string& to) {
            auto text = valid; const auto at = text.find(from); require(at != std::string::npos); text.replace(at, from.size(), to); return text;
        };
        const std::string invalid[] = {"{}", "[]", "{\"data\":[]}", response(",\"id\":8"),
            "{\"data\":{},\"data\":{}}", valid + "garbage", valid + std::string(1, '\0'),
            changed("\"id\":7", "\"id\":0"), changed("\"id\":7", "\"id\":-1"), changed("\"id\":7", "\"id\":2147483648"),
            changed("\"fileSize\":3", "\"fileSize\":0"), changed("\"fileSize\":3", "\"fileSize\":-3"),
            changed("\"fileSize\":3", "\"fileSize\":3.0"), changed("\"fileSize\":3", "\"fileSize\":4294967297"),
            changed("\"fileSize\":3,", ""), changed("BA7816", "GA7816"), changed("BA7816", "BA781"),
            changed("1.2.3.4", "../file"), changed("人工版本", "\\u0000"),
            changed("人工版本", std::string(16385,'a')), changed("人工版本", std::string(1,static_cast<char>(0xff)))};
        for (const auto& text : invalid) {
            require(!parse_release_metadata(text, metadata));
            require(metadata.release_id == 7 && metadata.package.byte_count == 3 && metadata.description == u8"人工版本");
        }
        require(!parse_release_metadata(std::string(kMaximumMetadataBytes + 1, ' '), metadata));
        std::string url = "unchanged";
        for (const auto text : {"{\"data\":{\"url\":\"http://example.test/a\"}}", "{\"data\":{\"url\":\"https:///a\"}}",
            "{\"data\":{\"url\":\"https://user@example.test/a\"}}", "{\"data\":{\"url\":\"https://example.test/ a\"}}",
            "{\"data\":{\"url\":\"https://example.test/a\\u0000\"}}", "{\"data\":{\"url\":\"https://a\",\"url\":\"https://b\"}}"}) {
            require(!parse_download_location(text, url) && url == "unchanged");
        }
        require(parse_download_location("{\"data\":{\"url\":\"https://example.test/package.exe?token=abc\"}}", url));
        require(url == "https://example.test/package.exe?token=abc");
        require(download_location_url(0).empty() && download_location_url(UINT32_MAX).empty());
        require(update_service_available() == !release_check_url().empty());
        if (!update_service_available()) require(download_location_url(7).empty());
        std::cout << "Version, UTF-8 metadata, fingerprint, duplicate and transaction fixtures passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}

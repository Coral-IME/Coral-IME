#include "coral/updates/update_notice.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool value) { if (!value) throw std::runtime_error("Update notice fixture failed"); }
std::string notice(std::string version = "1.0.0.4", std::string url = "https://github.com/coral/project/releases/tag/v1.0.0.4") {
    return "{\"schema_version\":1,\"data\":{\"version\":\"" + version +
        "\",\"description\":\"更新说明\\n第二行\",\"release_url\":\"" + url + "\"}}";
}
}
int main(int argc, char** argv) {
    using namespace coral::updates;
    try {
        UpdateNotice result;
        require(parse_update_notice(notice(), result));
        require(result.description == u8"更新说明\n第二行" && format_release_version(result.version) == "1.0.0.4");
        const auto saved_url = result.release_url;
        for (const auto* url : {"http://github.com/coral/project/releases/tag/v1", "https://github.com.evil.test/a/b/releases/tag/v1",
            "https://user@github.com/a/b/releases/tag/v1", "https://github.com/a/b/releases/latest",
            "https://github.com/a/b/releases/download/v1/a.exe", "https://github.com/a/b/releases/tag/..",
            "https://github.com/../b/releases/tag/v1", "https://github.com/a/b/releases/tag/v1?x=1",
            "https://github.com/a/b/releases/tag/v1#frag", "https://github.com/a/b/releases/tag/v1/other",
            "https://github.com/a/b/releases/tag/v1%20x", "file:///C:/installer.exe", "javascript:alert(1)"}) {
            require(!is_release_page_url(url));
            require(!parse_update_notice(notice("1.0.0.4", url), result) && result.release_url == saved_url);
        }
        require(is_release_page_url("https://github.com/Some-Owner/repo_name/releases/tag/v1.2.3.4-beta"));
        for (const auto* version : {"1.0.0", "01.0.0.4", "65536.0.0.0", "1.0.0.4beta", "99999999999.0.0.0"})
            require(!parse_update_notice(notice(version), result));
        const auto valid = notice();
        const auto replace = [&](const std::string& from, const std::string& to) {
            auto copy = valid; const auto at = copy.find(from); require(at != std::string::npos);
            copy.replace(at, from.size(), to); return copy;
        };
        for (const auto& invalid : {std::string("{}"), valid + "extra", valid + std::string(1, '\0'),
            replace("\"schema_version\":1", "\"schema_version\":2"), replace("\"schema_version\":1", "\"schema_version\":3"), replace("\"schema_version\":1", "\"schema_version\":true"),
            replace("\"schema_version\":1", "\"schema_version\":1.0"), replace("\"schema_version\":1", "\"schema_version\":1,\"schema_version\":1"),
            replace("\"version\":", "\"version\":\"1.0.0.4\",\"version\":"),
            replace("更新说明", "\\u0000"), replace("更新说明", "\\u0001"),
            replace("更新说明", std::string(16385, 'a')), replace("更新说明", std::string(1, static_cast<char>(0xff))),
            replace("\"release_url\":", "\"other\":"), replace("\"data\":{", "\"data\":{\"description\":\"duplicate\","),
            std::string(kMaximumMetadataBytes + 1, ' ')}) {
            require(!parse_update_notice(invalid, result) && result.release_url == saved_url &&
                result.description == u8"更新说明\n第二行");
        }
        require(auto_check_due(100000, 0) && !auto_check_due(100000, 99999));
        require(!auto_check_due(186399, 100000) && auto_check_due(186400, 100000));
        require(auto_check_due(100000, 200000) && !auto_check_due(0, 0) && auto_check_due(100000, -1));
        require(argc >= 1 && argc <= 3);
        // 客户端校验可独立运行；传入服务端示例时额外检查两端的数据契约。
        if (argc >= 2) {
            std::ifstream source(argv[1], std::ios::binary);
            require(static_cast<bool>(source));
            const std::string example((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
            require(parse_update_notice(example, result) && format_release_version(result.version) == "1.0.0.4");
        }
        const std::string package = R"({"schema_version":2,"data":{"id":10001,"version":"1.0.0.1","description":"package","fileSize":3,"fileSha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}})";
        require(parse_update_notice(package, result) && result.package.has_value() && result.release_url.empty());
        require(result.package->release_id == 10001 && result.package->package.byte_count == 3);
        ReleaseVersion installed;
        require(parse_release_version("1.0.0.0", installed) && is_newer_version(result.version, installed));
        require(!is_newer_version(installed, result.version) && !is_newer_version(result.version, result.version));
        for (const auto& from : {std::string("\"id\":10001"), std::string("\"fileSize\":3"), std::string("\"fileSha256\":")}) {
            auto broken = package;
            const auto at = broken.find(from);
            broken.replace(at, from.size(), from == "\"id\":10001" ? "\"id\":0" : from == "\"fileSize\":3" ? "\"fileSize\":0" : "\"other\":");
            require(!parse_update_notice(broken, result) && result.package->release_id == 10001);
        }
        if (argc == 3) {
            std::ifstream source(argv[2], std::ios::binary);
            const std::string example((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
            require(parse_update_notice(example, result) && result.package && result.package->release_id == 10001);
        }
        std::cout << "Notice schema, release page validation, version comparison and daily scheduling passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

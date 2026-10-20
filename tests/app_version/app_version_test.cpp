#include "package_version.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
struct TempDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("coral_app_version_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory() { std::filesystem::create_directory(path); }
    ~TempDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
}

int main()
{
    try {
        TempDirectory temp;
        const auto installDir = temp.path / std::filesystem::u8path(u8"珊瑚输入法 安装目录");
        std::filesystem::create_directories(installDir / "config");
        auto expect = [&](const std::string& expected) {
            const auto actual = coral::config::readInstalledAppVersion(installDir);
            if (actual != expected)
                throw std::runtime_error("Expected '" + expected + "', got '" + actual + "'");
        };
        expect(""); // 缺失元数据时允许回退到可执行文件版本。
        auto check = [&](const std::string& data, const std::string& expected) {
            {
                std::ofstream file(installDir / "config" / "coralpinyinConfig.ini", std::ios::binary);
                file << data;
                if (!file) throw std::runtime_error("Cannot write fixture");
            }
            expect(expected);
        };
        check("[app]\r\nVersion=1.0.0.28\r\n", "1.0.0.28");
        check("\xEF\xBB\xBF; package metadata\r\n[APP]\r\n version = 2.3.4.5 \r\n", "2.3.4.5");
        check("[settingsconfig]\nVersion=1.0.0.3\n[app]\nVersion=1.0.0.29\n", "1.0.0.29");
        check("[other]\nVersion=1.0.0.3\n", "");
        for (const auto* invalid : {"", "1.0.0", "1.0.0.28.1", "1..0.28", "-1.0.0.28",
                                    "1.0.0.28-beta", "65536.0.0.0", "999999999999999999.0.0.0"})
            check(std::string("[app]\nVersion=") + invalid + "\n", "");
        check("[app]\nVersion=65535.65535.65535.65535\n", "65535.65535.65535.65535");
        check("[app]\nVersion=0.0.0.0\n", "0.0.0.0");
        check("", "");
        std::cout << "PASS: installed version metadata, Unicode paths, BOM, missing/invalid versions\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

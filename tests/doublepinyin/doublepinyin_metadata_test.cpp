#include "layout_metadata.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void expect(const std::filesystem::path& path, const std::string& expected)
{
    const auto actual = coral::config::readDoublepinyinSchemeTitle(path);
    if (actual != expected)
        throw std::runtime_error("Unexpected title in " + path.u8string() + ": " + actual);
}

struct TempDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("coral_doublepinyin_metadata_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory() { std::filesystem::create_directory(path); }
    ~TempDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 2) throw std::runtime_error("Expected builtin scheme directory");
        const auto builtins = std::filesystem::u8path(argv[1]);
        const std::vector<std::pair<std::string, std::string>> titles = {
            {"ABC", u8"智能ABC"}, {"guobiao", u8"国标双拼"},
            {"ms2003", u8"微软双拼"}, {"pinyinjiajia", u8"拼音加加"},
            {"sougou", u8"搜狗双拼"}, {"xiaohe", u8"小鹤双拼"},
            {"ziguang", u8"紫光双拼"}, {"ziranma", u8"自然码"}
        };
        for (const auto& [name, title] : titles) expect(builtins / (name + ".ini"), title);

        TempDirectory temp;
        const auto custom = temp.path / std::filesystem::u8path(u8"自定义方案.ini");
        auto check = [&](const std::string& data, const std::string& expected) {
            {
                std::ofstream file(custom, std::ios::binary | std::ios::trunc);
                file.write(data.data(), static_cast<std::streamsize>(data.size()));
                if (!file) throw std::runtime_error("Cannot write fixture");
            }
            expect(custom, expected);
        };
        check("\xEF\xBB\xBF[metadata]\r\ntitle=小鹤双拼\r\n", u8"小鹤双拼");
        check(u8"[metadata]\ntitle=自定义双拼😀\n", u8"自定义双拼😀");
        check(u8"; comment\n[other]\ntitle=wrong\n[Metadata]\n\n# comment\n Title = 自定义双拼 \n[shengmu]\na=b\n", u8"自定义双拼");
        check("[metadata]\nsubtitle=wrong\n[other]\ntitle=wrong\n", "");
        check("[metadata]\ntitle=\n", "");
        check("[metadata]\ntitle=   \t\n", "");
        check("", "");
        const std::string longTitle(512, 'a');
        check("[metadata]\ntitle=" + longTitle + "\n", longTitle);
        expect(temp.path / "missing.ini", "");
        std::cout << "PASS: 8 builtin titles and 9 UTF-8/INI edge cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

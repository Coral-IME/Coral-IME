#include <cstdint>
#include <type_traits>
#include "candidatefontlist.h"
#include <iostream>
#include <stdexcept>

using namespace coral::settings;
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main() {
    try {
        const auto installed = [](const auto& name) {
            return SameFont(name, L"SimSun") || SameFont(name, L"Custom Chinese Font");
        };
        const auto fonts = BuildCandidateFonts(
            { L"Removed Font", L"Custom Chinese Font", L"custom chinese font", L"宋体" },
            L"Removed Font", installed);
        Require(fonts.size() == 2, "Missing fonts leaked into list or duplicates were added");
        Require(fonts[0].name == L"SimSun" && fonts[0].label == L"宋体", "Default font mapping changed");
        Require(fonts[1].name == L"Custom Chinese Font", "Added font mapping changed");
        const auto current = BuildCandidateFonts({}, L"Custom Chinese Font", installed);
        Require(current.size() == 2, "Installed current font was not restored");
        Require(BuildCandidateFonts({}, L"Removed Font", [](const auto&) { return false; }).empty(),
            "Empty installed set must produce an empty dropdown");
        const std::vector<std::wstring> names{ L"自定义中文字体", L"Quote \" and \\" };
        Require(ParseAddedFonts(SerializeAddedFonts(names)) == names, "UTF-8/escaped font names did not round trip");
        Require(ParseAddedFonts("broken JSON").empty(), "Malformed configuration was accepted");
        Require(ParseAddedFonts(R"([42,null,"","Test","test","Bad\u0000Font"])") ==
            std::vector<std::wstring>{ L"Test" }, "Invalid or duplicate saved names were accepted");
        Require(!IsInstalledFont(L"CoralMissingFont_73D948") && !IsInstalledFont(L"") &&
            !IsInstalledFont(L"@SimSun"), "System enumeration substituted a missing font");
        Require(IsInstalledFont(L"Segoe UI"), "Known installed system font was not detected");
        Require(SameFont(L"Microsoft YaHei", L"微软雅黑"), "Localized font alias was not deduplicated");
        std::cout << "[PASS] installed-font filtering, aliases, current selection and saved-list round trip\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

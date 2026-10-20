#ifndef CORAL_INCLUDE_CORAL_TRANSLATION_SHORTCUT_H_INCLUDED
#define CORAL_INCLUDE_CORAL_TRANSLATION_SHORTCUT_H_INCLUDED
#include <array>
#include <cstdint>
#include <string_view>

namespace coral::translation {
struct ShortcutOption {
    const char* value;
    const wchar_t* label;
    std::uint32_t virtual_key;
};
// 虚拟键码固定为等号及 F1–F12，供设置与 TSF 共用。
inline constexpr std::array<ShortcutOption, 13> kShortcutOptions{{
    {"=", L"=", 0xBB}, {"F1", L"F1", 0x70}, {"F2", L"F2", 0x71},
    {"F3", L"F3", 0x72}, {"F4", L"F4", 0x73}, {"F5", L"F5", 0x74},
    {"F6", L"F6", 0x75}, {"F7", L"F7", 0x76}, {"F8", L"F8", 0x77},
    {"F9", L"F9", 0x78}, {"F10", L"F10", 0x79}, {"F11", L"F11", 0x7A},
    {"F12", L"F12", 0x7B}
}};
inline const ShortcutOption& shortcut_option(std::string_view value) {
    for (const auto& option : kShortcutOptions) if (value == option.value) return option;
    return kShortcutOptions.front();
}
inline bool shortcut_conflicts(std::uint32_t virtual_key, bool minus_equals_page) {
    return virtual_key == kShortcutOptions.front().virtual_key && minus_equals_page;
}
}
#endif

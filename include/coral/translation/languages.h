#ifndef CORAL_INCLUDE_CORAL_TRANSLATION_LANGUAGES_H_INCLUDED
#define CORAL_INCLUDE_CORAL_TRANSLATION_LANGUAGES_H_INCLUDED
#include <array>
#include <string_view>

namespace coral::translation {
struct Language { const char* code; const wchar_t* label; };
// 覆盖世界主要语言，归组阿拉伯语变体并排除固定的
// 中文源语言。这是产品选择，不是实时排名。
inline constexpr std::array<Language, 15> kTargetLanguages{{
    {"en", L"英语"}, {"hi", L"印地语"}, {"es", L"西班牙语"},
    {"fr", L"法语"}, {"ar", L"阿拉伯语"}, {"bn", L"孟加拉语"},
    {"pt", L"葡萄牙语"}, {"ru", L"俄语"}, {"ur", L"乌尔都语"},
    {"id", L"印度尼西亚语"}, {"de", L"德语"}, {"ja", L"日语"},
    {"mr", L"马拉地语"}, {"te", L"泰卢固语"}, {"ta", L"泰米尔语"}
}};
inline bool supported_target(std::string_view code) {
    for (const auto& language : kTargetLanguages) if (code == language.code) return true;
    return false;
}
}
#endif

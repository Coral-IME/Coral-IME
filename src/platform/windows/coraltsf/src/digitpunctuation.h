#pragma once

namespace coral::tsf {

inline bool IsCommittedDigit(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') || (ch >= L'０' && ch <= L'９');
}

// 在组装待上屏文本时调用，而不是在路由数字键时。
// 因此选词/翻页键绝不会被误当成数字输出。
inline wchar_t DigitFollowingPeriod(bool enabled, bool pendingInput,
                                    wchar_t previous, wchar_t punctuation)
{
    return enabled && !pendingInput && IsCommittedDigit(previous) && punctuation == L'。'
        ? L'.' : punctuation;
}

inline wchar_t english_separator(wchar_t punctuation)
{
    switch (punctuation) {
    case L'：': return L':';
    case L'，': return L',';
    case L'。': return L'.';
    default: return L'\0';
    }
}

// 由按键路由与执行共用，使两者识别同一套标点。
inline wchar_t DigitSeparatorReplacement(wchar_t previous, wchar_t punctuation)
{
    return IsCommittedDigit(previous) ? english_separator(punctuation) : L'\0';
}

inline bool is_committed_letter(wchar_t ch)
{
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
        (ch >= L'ａ' && ch <= L'ｚ') || (ch >= L'Ａ' && ch <= L'Ｚ');
}

// 字母后的分隔符在标点上屏时转换，后续字母继续走正常组串流程。
inline wchar_t following_punctuation(bool digit_enabled, bool letter_enabled,
                                    bool pending_input, wchar_t previous, wchar_t punctuation)
{
    if (pending_input) return punctuation;
    if (letter_enabled && is_committed_letter(previous)) {
        if (const auto replacement = english_separator(punctuation)) return replacement;
    }
    return DigitFollowingPeriod(digit_enabled, false, previous, punctuation);
}

// 仅数字之间保留延迟替换，字母键不能触发退格和字符注入。
inline wchar_t digit_separator_replacement(bool enabled, wchar_t previous,
                                          wchar_t punctuation, wchar_t next)
{
    return enabled && IsCommittedDigit(next)
        ? DigitSeparatorReplacement(previous, punctuation) : L'\0';
}

} // 命名空间 coral::tsf 结束

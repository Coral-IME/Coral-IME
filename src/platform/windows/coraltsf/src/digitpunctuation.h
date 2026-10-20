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

// 由按键路由与执行共用，使两者识别同一套标点。
inline wchar_t DigitSeparatorReplacement(wchar_t previous, wchar_t punctuation)
{
    if (!IsCommittedDigit(previous)) return L'\0';
    switch (punctuation) {
    case L'：': return L':';
    case L'，': return L',';
    case L'。': return L'.';
    default: return L'\0';
    }
}

inline bool is_committed_letter(wchar_t ch)
{
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
        (ch >= L'ａ' && ch <= L'ｚ') || (ch >= L'Ａ' && ch <= L'Ｚ');
}

inline wchar_t following_period(bool digit_enabled, bool letter_enabled,
                                bool pending_input, wchar_t previous, wchar_t punctuation)
{
    const bool enabled = (digit_enabled && IsCommittedDigit(previous)) ||
        (letter_enabled && is_committed_letter(previous));
    return enabled && !pending_input && punctuation == L'。' ? L'.' : punctuation;
}

// 前后两个字符必须都属于启用的类别。
inline wchar_t separator_replacement(bool digit_enabled, bool letter_enabled,
                                     wchar_t previous, wchar_t punctuation, wchar_t next)
{
    const bool enabled =
        (digit_enabled && IsCommittedDigit(previous) && IsCommittedDigit(next)) ||
        (letter_enabled && is_committed_letter(previous) && is_committed_letter(next));
    return enabled ? DigitSeparatorReplacement(L'0', punctuation) : L'\0';
}

} // 命名空间 coral::tsf 结束

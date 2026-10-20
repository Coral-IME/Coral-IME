#pragma once

namespace coral::tsf {

inline bool IsCommittedDigit(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') || (ch >= L'０' && ch <= L'９');
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

// 数字和字母后的分隔符在标点上屏时转换，不等待后续字符。
// 只检查已提交字符，选词和翻页键不能被当作数字输出。
inline wchar_t following_punctuation(bool digit_enabled, bool letter_enabled,
                                    bool pending_input, wchar_t previous, wchar_t punctuation)
{
    if (pending_input) return punctuation;
    if ((digit_enabled && IsCommittedDigit(previous)) ||
        (letter_enabled && is_committed_letter(previous))) {
        if (const auto replacement = english_separator(punctuation)) return replacement;
    }
    return punctuation;
}

// 仅为历史中残留的数字间中文分隔符保留替换，字母键不能触发注入。
inline wchar_t digit_separator_replacement(bool enabled, wchar_t previous,
                                          wchar_t punctuation, wchar_t next)
{
    return enabled && IsCommittedDigit(next)
        ? DigitSeparatorReplacement(previous, punctuation) : L'\0';
}

} // 命名空间 coral::tsf 结束

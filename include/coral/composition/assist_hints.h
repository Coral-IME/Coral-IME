#pragma once
#include "assist_mode.h"

namespace coral::composition {
inline std::wstring_view specialModeHint(SpecialMode mode, std::wstring_view input,
    bool matched, bool unavailable = false) {
    if (mode == SpecialMode::Normal) return {};
    if (unavailable) return L"暂时无法获取候选，请检查输入法服务或修复安装\n可继续输入；Esc 退出";
    const bool uppercase = !input.empty() && (input.front() == L'U' || input.front() == L'V');
    switch (mode) {
    case SpecialMode::Stroke:
        if (input.size() > 1 && !matched)
            return uppercase
                ? L"尚无匹配，请继续输入或检查笔画/拆字\nh横 s竖 p撇 n点/捺 z折；拆字如 Umumumu；Esc 退出"
                : L"尚无匹配，请继续输入或检查笔画/拆字\nh横 s竖 p撇 n点/捺 z折；拆字如 umumumu；Esc 退出";
        return uppercase
            ? L"h横 s竖 p撇 n点/捺 z折；拆字如 Umumumu\n输入 Uu 查看符号分类；数字键选词，Esc 退出"
            : L"h横 s竖 p撇 n点/捺 z折；拆字如 umumumu\n输入 uu 查看符号分类；数字键选词，Esc 退出";
    case SpecialMode::Symbols:
        if (input.size() > 2 && !matched)
            return uppercase
                ? L"尚无匹配，请检查分类缩写\nUudw 单位 · Uuxlzm 希腊字母 · Uuyf 音符；Esc 退出"
                : L"尚无匹配，请检查分类缩写\nuudw 单位 · uuxlzm 希腊字母 · uuyf 音符；Esc 退出";
        return uppercase
            ? L"Uudw 单位 · Uuxlzm 希腊字母 · Uulmsz 罗马数字\nUuyf 音符 · Uujq 节气；数字键选词，Esc 退出"
            : L"uudw 单位 · uuxlzm 希腊字母 · uulmsz 罗马数字\nuuyf 音符 · uujq 节气；数字键选词，Esc 退出";
    case SpecialMode::Convert:
        if (input.size() > 1 && !matched)
            return uppercase
                ? L"尚无结果，请补全或检查数字、算式、日期、时间\n如 V1+2、V2026.10.9、V12:30；Esc 退出"
                : L"尚无结果，请补全或检查数字、算式、日期、时间\n如 v1+2、v2026.10.9、v12:30；Esc 退出";
        return uppercase
            ? L"数字 V123.45 · 计算 V1+2 · 日期 V2026.10.9\n时间 V12:30；A-I 选词，数字键继续输入，Esc 退出"
            : L"数字 v123.45 · 计算 v1+2 · 日期 v2026.10.9\n时间 v12:30；A-I 选词，数字键继续输入，Esc 退出";
    default: return {};
    }
}

inline constexpr wchar_t kUModeHelp[] =
    L"全拼：在新输入开始时输入 u；双拼：按 Shift+U。\n\n"
    L"笔画输入：h 横、s 竖、p 撇、n 点/捺、z 折。\n"
    L"示例：ushhsh 可以查找 㐀。\n"
    L"拆字输入：依次输入部件的拼音，不加声调。\n"
    L"示例：umumumu 查找 瞐；ucaowubei 查找 蒇。\n\n"
    L"符号输入：uu 加分类缩写；双拼先按 Shift+U，再输入 u。\n"
    L"uudw 单位与货币；uuxlzm 希腊字母；uulmsz 罗马数字；\n"
    L"uuyf 音符；uuyd 声调；uujq 节气；uubh 笔画。\n\n"
    L"数字键或空格选词；翻页键查看更多；Esc 退出。";
inline constexpr wchar_t kVModeHelp[] =
    L"全拼：在新输入开始时输入 v；双拼：按 Shift+V。\n\n"
    L"数字与金额：v123.45，提供中文数字和金额大小写。\n"
    L"计算：v1+2、v1/2、v(1+2)*3，支持 + - * / ^ 和括号。\n"
    L"除法与乘方结果保留现有约六位小数的精度。\n"
    L"日期：v2026.10.9，也支持 -、/ 分隔或 v2026n10y9r。\n"
    L"时间：v12:30 或 v12:30:45；两段格式也可表示分、秒。\n\n"
    L"A-I 或空格选词；数字键继续输入；翻页键查看更多；Esc 退出。\n"
    L"部分输入可以同时表示日期和算式，会保留两类候选。";
}

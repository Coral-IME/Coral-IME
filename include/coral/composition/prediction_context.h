#pragma once

#include <string>
#include <string_view>

namespace coral::composition {

// 只保留连续中文的末尾，分次选出的单字不会覆盖整段上下文。
inline std::wstring prediction_context(std::wstring_view previous, std::wstring_view selected)
{
    std::wstring result(previous);
    for (std::size_t i = 0; i < selected.size(); ++i) {
        const auto first = static_cast<unsigned>(selected[i]);
        unsigned scalar = first;
        std::size_t units = 1;
        if (first >= 0xD800 && first <= 0xDBFF && i + 1 < selected.size()) {
            const auto second = static_cast<unsigned>(selected[i + 1]);
            if (second >= 0xDC00 && second <= 0xDFFF) {
                scalar = 0x10000 + ((first - 0xD800) << 10) + second - 0xDC00;
                units = 2;
            }
        }
        const bool han = (scalar >= 0x3400 && scalar <= 0x4DBF) ||
            (scalar >= 0x4E00 && scalar <= 0x9FFF) ||
            (scalar >= 0xF900 && scalar <= 0xFAFF) ||
            (scalar >= 0x20000 && scalar <= 0x323AF);
        if (han) result.append(selected.substr(i, units));
        else result.clear();
        i += units - 1;
    }
    // 以 UTF-16 单元计的上限；裁剪时保留完整代理对。
    if (result.size() > 16) {
        auto start = result.size() - 16;
        if (result[start] >= 0xDC00 && result[start] <= 0xDFFF) ++start;
        result.erase(0, start);
    }
    return result;
}
}

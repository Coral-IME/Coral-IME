#pragma once

#include <string_view>

namespace coral::composition {
enum class SpecialMode { Normal, Stroke, Symbols, Convert };

// 由输入控制器与引擎共用。双拼下只有大写触发
// 才进入特殊模式；其余按键保持普通含义。
template<class Char>
SpecialMode specialMode(std::basic_string_view<Char> input, bool doublepinyin,
    bool uEnabled, bool vEnabled)
{
    if (input.empty()) return SpecialMode::Normal;
    const Char first = input[0];
    if (uEnabled && first == (doublepinyin ? 'U' : 'u')) {
        return input.size() > 1 && (input[1] == 'u' || (doublepinyin && input[1] == 'U'))
            ? SpecialMode::Symbols : SpecialMode::Stroke;
    }
    if (vEnabled && first == (doublepinyin ? 'V' : 'v')) {
        // n/y/r 是年/月/日分隔符；其他字母是普通
        // 字母输入。V 模式下 A-I 仍是选词键。
        for (std::size_t i = 1; i < input.size(); ++i)
            if (input[i] >= 'a' && input[i] <= 'z' &&
                input[i] != 'n' && input[i] != 'y' && input[i] != 'r')
                return SpecialMode::Normal;
        return SpecialMode::Convert;
    }
    return SpecialMode::Normal;
}
}

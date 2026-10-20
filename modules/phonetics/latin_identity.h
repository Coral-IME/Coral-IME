#pragma once

#include <string>
#include <string_view>

namespace coral {
// C++、CI/CD、OpenAI 等 ASCII 技术词同样是模型词元。
// 显示拼写独立于小写查找标识。
inline std::string english_identity_key(std::string_view word) {
    if (word.empty()) return {};
    bool hasLetter = false;
    for (unsigned char ch : word) {
        if (ch < 0x21 || ch > 0x7e) return {};
        hasLetter |= (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
    }
    if (!hasLetter) return {};
    std::string key(word);
    for (auto& ch : key) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return key;
}
}

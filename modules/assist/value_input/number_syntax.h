#pragma once
#include <string_view>

namespace coral::uv {
struct DecimalParts {
    std::string_view integer;
    std::string_view fraction;
};
inline bool decimal_parts(std::string_view input, DecimalParts& parts) {
    const auto dot = input.find('.');
    parts.integer = input.substr(0, dot);
    parts.fraction = dot == input.npos ? std::string_view{} : input.substr(dot + 1);
    if (parts.integer.empty() || parts.integer.size() > 64 ||
        (dot != input.npos && parts.fraction.empty()) || input.size() > 128) return false;
    for (char c : parts.integer) if (c < '0' || c > '9') return false;
    for (char c : parts.fraction) if (c < '0' || c > '9') return false;
    return true;
}
}

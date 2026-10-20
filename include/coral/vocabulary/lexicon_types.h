#pragma once

#include <cstdint>
#include <string_view>

namespace coral {

// 字母系统独立于词条或候选规则的来源。
enum class LanguageVariant : std::uint8_t {
    Simplified = 0,
    Traditional = 1,
};

enum class CandidateAction : std::uint8_t {
    PinFirst = 0,
    UnpinFirst = 1,
    Disable = 2,
};

constexpr bool IsValidLanguageVariant(LanguageVariant variant) {
    return variant == LanguageVariant::Simplified || variant == LanguageVariant::Traditional;
}

constexpr bool IsValidCandidateAction(CandidateAction action) {
    return action == CandidateAction::PinFirst || action == CandidateAction::UnpinFirst ||
           action == CandidateAction::Disable;
}

// 统计实际 UTF-8 字符数，包括增补平面的汉字。
// 变体选择符是对字符的细化，不会把它变成一个词。
inline bool CanDisableCandidate(std::string_view phrase) {
    std::size_t characters = 0;
    for (std::size_t offset = 0; offset < phrase.size();) {
        const auto first = static_cast<unsigned char>(phrase[offset++]);
        std::uint32_t codepoint = first;
        int continuation = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            codepoint = first & 0x1f; continuation = 1; minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            codepoint = first & 0x0f; continuation = 2; minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            codepoint = first & 0x07; continuation = 3; minimum = 0x10000;
        } else if (first >= 0x80) {
            return false;
        }
        for (int i = 0; i < continuation; ++i) {
            if (offset == phrase.size()) return false;
            const auto next = static_cast<unsigned char>(phrase[offset++]);
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        if ((codepoint >= 0xfe00 && codepoint <= 0xfe0f) ||
            (codepoint >= 0xe0100 && codepoint <= 0xe01ef)) continue;
        ++characters;
    }
    return characters > 1;
}

}  // 命名空间 coral 结束

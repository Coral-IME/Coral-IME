#pragma once

#include <charconv>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace coral::preferences {
namespace detail {

constexpr bool is_ascii_space(char value) noexcept
{
    return value == ' ' || (value >= '\t' && value <= '\r');
}

constexpr std::string_view trim_ascii(std::string_view text) noexcept
{
    while (!text.empty() && is_ascii_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_ascii_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

constexpr bool equals_ascii_folded(std::string_view text, std::string_view token) noexcept
{
    if (text.size() != token.size()) {
        return false;
    }
    for (std::size_t position = 0; position < text.size(); ++position) {
        const char value = text[position];
        const char folded = value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
        if (folded != token[position]) {
            return false;
        }
    }
    return true;
}

} // 命名空间 detail 结束

template <typename T>
struct ScalarTextCodec {
    static T parse(std::string_view) = delete;
    static std::string format(const T&) = delete;
};

template <>
struct ScalarTextCodec<int> {
    static std::optional<int> try_parse(std::string_view text) noexcept
    {
        text = detail::trim_ascii(text);
        if (text.empty()) {
            return std::nullopt;
        }
        int result = 0;
        const auto conversion = std::from_chars(text.data(), text.data() + text.size(), result);
        if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size()) {
            return std::nullopt;
        }
        return result;
    }

    static int parse(std::string_view text) noexcept { return try_parse(text).value_or(0); }
    static std::string format(int value) { return std::to_string(value); }
};

template <>
struct ScalarTextCodec<bool> {
    static std::optional<bool> try_parse(std::string_view text) noexcept
    {
        text = detail::trim_ascii(text);
        for (const auto token : {"true", "yes", "on", "1"}) {
            if (detail::equals_ascii_folded(text, token)) {
                return true;
            }
        }
        for (const auto token : {"false", "no", "off", "0"}) {
            if (detail::equals_ascii_folded(text, token)) {
                return false;
            }
        }
        return std::nullopt;
    }

    static bool parse(std::string_view text) noexcept { return try_parse(text).value_or(false); }
    static std::string format(bool value) { return value ? "true" : "false"; }
};

template <>
struct ScalarTextCodec<std::string> {
    static std::string parse(std::string_view text) { return std::string(text); }
    static std::string format(const std::string& value) { return value; }
};

template <>
struct ScalarTextCodec<const char*> {
    static std::string format(const char* value) { return value ? value : ""; }
};

} // 命名空间 coral::preferences 结束

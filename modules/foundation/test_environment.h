#pragma once
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace coral::testing {
// 选择性启用的进程环境；普通产品对象名保持不变。
inline std::string objectName(const std::string& base) {
    const char* value = std::getenv("CORAL_TEST_NAMESPACE");
    if (!value || !*value) return base;
    const std::string suffix(value);
    if (suffix.size() > 64 || suffix.find_first_not_of(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
        throw std::invalid_argument("CORAL_TEST_NAMESPACE must contain at most 64 ASCII letters, digits, '_' or '-'");
    return base + "_test_" + suffix;
}
}

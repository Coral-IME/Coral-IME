#pragma once
#include "decimal_value.h"
namespace coral::assist {
class ExpressionEvaluator {
public:
    bool compute(const std::string& expr, BigFloat& ans);
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


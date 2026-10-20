#pragma once
#include "value_processor.h"
namespace coral::assist {
class ValueHandler {
public:
    static bool parse(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents);
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


#pragma once

#include "candidate_rules_types.h"

#include <string>
#include <vector>

namespace coral::candidate_policy {

struct DecodedPolicy {
    std::uint64_t generation = 0;
    std::vector<Rule> rules;
};

class Codec final {
public:
    static bool Encode(std::uint64_t generation, const std::vector<Rule>& rules,
                       std::string& output, std::string& error);
    static bool Decode(const std::string& input, DecodedPolicy& output, std::string& error);
};

}  // 命名空间 coral::candidate_policy 结束

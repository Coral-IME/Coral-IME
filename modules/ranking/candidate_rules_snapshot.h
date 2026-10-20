#pragma once

#include "candidate_rules_types.h"

#include <memory>
#include <string_view>
#include <vector>

namespace coral::candidate_policy {

class Snapshot final {
public:
    static std::shared_ptr<const Snapshot> Build(std::uint64_t generation,
                                                  std::vector<Rule> rules);

    std::uint64_t generation() const { return generation_; }
    const std::vector<Rule>& rules() const { return rules_; }
    std::vector<Rule> Lookup(LanguageVariant variant, std::string_view lookupKey) const;
    const Rule* Find(const RuleIdentity& identity) const;

private:
    Snapshot(std::uint64_t generation, std::vector<Rule> rules);

    std::uint64_t generation_ = 0;
    std::vector<Rule> rules_;
};

}  // 命名空间 coral::candidate_policy 结束

#include "candidate_rules_snapshot.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace coral::candidate_policy {
namespace {

bool RuleLess(const Rule& lhs, const Rule& rhs) {
    return std::tie(lhs.variant, lhs.lookup_key, lhs.phrase) <
           std::tie(rhs.variant, rhs.lookup_key, rhs.phrase);
}

}  // namespace

Snapshot::Snapshot(std::uint64_t generation, std::vector<Rule> rules)
    : generation_(generation), rules_(std::move(rules)) {}

std::shared_ptr<const Snapshot> Snapshot::Build(std::uint64_t generation,
                                                 std::vector<Rule> rules) {
    std::vector<Rule> normalized;
    normalized.reserve(rules.size());
    std::map<std::pair<LanguageVariant, std::string>, std::size_t> pinnedByKey;
    for (Rule& rule : rules) {
        if (rule.kind == RuleKind::Disable) {
            // 旧的单字符禁用规则不得隐藏基础字符。
            if (!coral::CanDisableCandidate(rule.phrase)) continue;
            rule.pinned = false;
        }
        rule.rule_id = RuleId(IdentityOf(rule));
        if (rule.kind == RuleKind::Promote && rule.pinned) {
            const auto key = std::make_pair(rule.variant, rule.lookup_key);
            const auto [it, inserted] = pinnedByKey.emplace(key, normalized.size());
            if (inserted) {
                normalized.push_back(std::move(rule));
            } else if (rule.updated_sequence >= normalized[it->second].updated_sequence) {
                normalized[it->second] = std::move(rule);
            }
        } else {
            normalized.push_back(std::move(rule));
        }
    }
    rules = std::move(normalized);
    std::sort(rules.begin(), rules.end(), RuleLess);
    rules.erase(std::unique(rules.begin(), rules.end(), [](const Rule& lhs, const Rule& rhs) {
        return !RuleLess(lhs, rhs) && !RuleLess(rhs, lhs);
    }), rules.end());
    return std::shared_ptr<const Snapshot>(new Snapshot(generation, std::move(rules)));
}

std::vector<Rule> Snapshot::Lookup(LanguageVariant variant, std::string_view lookupKey) const {
    const auto first = std::lower_bound(rules_.begin(), rules_.end(), std::make_pair(variant, lookupKey),
        [](const Rule& rule, const auto& key) {
            if (rule.variant != key.first) return rule.variant < key.first;
            return rule.lookup_key < key.second;
        });
    std::vector<Rule> result;
    for (auto it = first; it != rules_.end() && it->variant == variant && it->lookup_key == lookupKey; ++it) {
        result.push_back(*it);
    }
    return result;
}

const Rule* Snapshot::Find(const RuleIdentity& identity) const {
    Rule probe;
    probe.variant = identity.variant;
    probe.lookup_key = identity.lookup_key;
    probe.phrase = identity.phrase;
    const auto it = std::lower_bound(rules_.begin(), rules_.end(), probe, RuleLess);
    return it != rules_.end() && SameIdentity(*it, identity) ? &*it : nullptr;
}

}  // 命名空间 coral::candidate_policy 结束

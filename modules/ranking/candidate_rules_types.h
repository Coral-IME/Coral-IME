#pragma once

#include "../../include/coral/vocabulary/lexicon_types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace coral::candidate_policy {

// 手动规则绝不参与自动历史评分或淘汰。
using LanguageVariant = coral::LanguageVariant;

enum class RuleKind : std::uint8_t {
    Promote = 0,
    Disable = 1,
};

struct RuleIdentity {
    LanguageVariant variant = LanguageVariant::Simplified;
    std::string lookup_key;
    std::string phrase;

    bool operator==(const RuleIdentity& rhs) const {
        return variant == rhs.variant && lookup_key == rhs.lookup_key && phrase == rhs.phrase;
    }
};

struct Rule {
    std::uint64_t rule_id = 0;
    LanguageVariant variant = LanguageVariant::Simplified;
    RuleKind kind = RuleKind::Promote;
    bool pinned = false;
    std::uint64_t updated_sequence = 0;
    std::string lookup_key;
    std::string full_pinyin;
    std::string phrase;
};

enum class MutationKind : std::uint8_t {
    Set,
    Remove,
    ClearAll,
};

struct Mutation {
    MutationKind kind = MutationKind::Set;
    RuleIdentity identity;
    std::optional<Rule> rule;
    std::uint64_t local_sequence = 0;
};

struct Options {
    // 这是独立的安全上限。它不占用
    // 10 万条自动学习历史条目的预算。
    std::size_t max_rules = 100000;
    std::size_t mutation_checkpoint_threshold = 32;
    std::uint32_t checkpoint_delay_ms = 3000;
    std::uint32_t external_change_poll_ms = 2000;
};

std::uint64_t RuleId(const RuleIdentity& identity);
RuleIdentity IdentityOf(const Rule& rule);
bool SameIdentity(const Rule& rule, const RuleIdentity& identity);

}  // 命名空间 coral::candidate_policy 结束

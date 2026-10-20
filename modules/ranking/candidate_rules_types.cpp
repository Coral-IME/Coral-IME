#include "candidate_rules_types.h"

#include <cstddef>

namespace coral::candidate_policy {
namespace {

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

}  // namespace

std::uint64_t RuleId(const RuleIdentity& identity) {
    std::uint64_t hash = 1469598103934665603ull;
    const auto variant = static_cast<std::uint8_t>(identity.variant);
    HashBytes(hash, &variant, sizeof(variant));
    HashBytes(hash, identity.lookup_key.data(), identity.lookup_key.size());
    constexpr unsigned char separator = 0xff;
    HashBytes(hash, &separator, sizeof(separator));
    HashBytes(hash, identity.phrase.data(), identity.phrase.size());
    return hash;
}

RuleIdentity IdentityOf(const Rule& rule) {
    return {rule.variant, rule.lookup_key, rule.phrase};
}

bool SameIdentity(const Rule& rule, const RuleIdentity& identity) {
    return rule.variant == identity.variant && rule.lookup_key == identity.lookup_key &&
           rule.phrase == identity.phrase;
}

}  // 命名空间 coral::candidate_policy 结束

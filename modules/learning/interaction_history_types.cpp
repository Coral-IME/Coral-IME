#include "interaction_history_types.h"

namespace coral::history {
namespace {

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

}  // namespace

std::uint64_t EntryId(const EntryIdentity& identity) {
    std::uint64_t hash = 1469598103934665603ull;
    const auto partition = static_cast<std::uint8_t>(identity.partition);
    HashBytes(hash, &partition, sizeof(partition));
    HashBytes(hash, identity.lookup_key.data(), identity.lookup_key.size());
    constexpr unsigned char separator = 0xff;
    HashBytes(hash, &separator, sizeof(separator));
    HashBytes(hash, identity.phrase.data(), identity.phrase.size());
    return hash;
}

EntryIdentity IdentityOf(const Entry& entry) {
    return EntryIdentity{entry.partition, entry.lookup_key, entry.phrase};
}

bool SameIdentity(const Entry& entry, const EntryIdentity& identity) {
    return entry.partition == identity.partition &&
           entry.lookup_key == identity.lookup_key &&
           entry.phrase == identity.phrase;
}

}  // 命名空间 coral::history 结束

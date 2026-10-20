#include "interaction_history_snapshot.h"

#include <algorithm>
#include <tuple>

namespace coral::history {
namespace {

bool EntryLess(const Entry& lhs, const Entry& rhs) {
    return std::tie(lhs.partition, lhs.lookup_key, lhs.phrase) <
           std::tie(rhs.partition, rhs.lookup_key, rhs.phrase);
}

}  // namespace

Snapshot::Snapshot(std::uint64_t generation, std::vector<Entry> entries)
    : generation_(generation), entries_(std::move(entries)) {}

std::shared_ptr<const Snapshot> Snapshot::Build(std::uint64_t generation,
                                                 std::vector<Entry> entries) {
    for (Entry& entry : entries) {
        entry.entry_id = EntryId(IdentityOf(entry));
    }
    std::sort(entries.begin(), entries.end(), EntryLess);
    entries.erase(std::unique(entries.begin(), entries.end(), [](const Entry& lhs, const Entry& rhs) {
        return !EntryLess(lhs, rhs) && !EntryLess(rhs, lhs);
    }), entries.end());
    return std::shared_ptr<const Snapshot>(new Snapshot(generation, std::move(entries)));
}

std::vector<Entry> Snapshot::Lookup(Partition partition, std::string_view lookup_key) const {
    const auto first = std::lower_bound(entries_.begin(), entries_.end(), std::make_pair(partition, lookup_key),
        [](const Entry& entry, const auto& key) {
            if (entry.partition != key.first) {
                return entry.partition < key.first;
            }
            return entry.lookup_key < key.second;
        });
    std::vector<Entry> result;
    for (auto it = first; it != entries_.end() && it->partition == partition && it->lookup_key == lookup_key; ++it) {
        result.push_back(*it);
    }
    return result;
}

const Entry* Snapshot::Find(const EntryIdentity& identity) const {
    const Entry probe{EntryId(identity), identity.partition, kNone, identity.lookup_key, {}, identity.phrase};
    const auto it = std::lower_bound(entries_.begin(), entries_.end(), probe, EntryLess);
    if (it == entries_.end() || !SameIdentity(*it, identity)) {
        return nullptr;
    }
    return &*it;
}

}  // 命名空间 coral::history 结束

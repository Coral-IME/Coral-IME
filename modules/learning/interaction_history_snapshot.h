#pragma once

#include "interaction_history_types.h"

#include <memory>
#include <string_view>
#include <vector>

namespace coral::history {

class Snapshot final {
public:
    static std::shared_ptr<const Snapshot> Build(std::uint64_t generation,
                                                  std::vector<Entry> entries);

    std::uint64_t generation() const { return generation_; }
    const std::vector<Entry>& entries() const { return entries_; }

    std::vector<Entry> Lookup(Partition partition, std::string_view lookup_key) const;
    const Entry* Find(const EntryIdentity& identity) const;

private:
    Snapshot(std::uint64_t generation, std::vector<Entry> entries);

    std::uint64_t generation_ = 0;
    std::vector<Entry> entries_;
};

}  // 命名空间 coral::history 结束

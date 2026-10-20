#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace coral::history {

enum class Partition : std::uint8_t {
    Simplified = 0,
    Traditional = 1,
    // 仅保留供 v2.0/旧编解码器迁移旧的嵌入策略记录。
    // 运行时历史存储会过滤这些值。
    SimplifiedPolicy = 2,
    TraditionalPolicy = 3,
};

enum EntryFlag : std::uint16_t {
    kNone = 0,
    kShortPinyin = 1u << 0,
    kFixedCandidate = 1u << 1,
    kDeleted = 1u << 2,
};

struct EntryIdentity {
    Partition partition = Partition::Simplified;
    std::string lookup_key;
    std::string phrase;

    bool operator==(const EntryIdentity& rhs) const {
        return partition == rhs.partition && lookup_key == rhs.lookup_key && phrase == rhs.phrase;
    }
};

struct Entry {
    std::uint64_t entry_id = 0;
    Partition partition = Partition::Simplified;
    std::uint16_t flags = kNone;
    std::string lookup_key;
    std::string full_pinyin;
    std::string phrase;
    std::uint32_t commit_count = 0;
    std::uint64_t last_used_sequence = 0;
    float score_state = 0.0f;
    // 旧迁移字段。新的自动历史记录始终为 -1；
    // 手动候选置顶由 CandidatePolicyManager 存储。
    std::int32_t candidate_position = -1;
};

enum class MutationKind : std::uint8_t {
    Learn,
    Upsert,
    Remove,
    ClearAll,
};

struct Mutation {
    MutationKind kind = MutationKind::Learn;
    EntryIdentity identity;
    std::uint32_t commit_delta = 0;
    std::uint64_t last_used_sequence = 0;
    float score_state = 0.0f;
    std::optional<Entry> entry;
    std::uint64_t local_sequence = 0;
};

struct Options {
    std::size_t max_entries = 100000;
    std::size_t delta_compact_threshold = 512;
    std::size_t mutation_checkpoint_threshold = 128;
    std::uint32_t checkpoint_delay_ms = 15000;
    std::uint32_t external_change_poll_ms = 2000;
};

std::uint64_t EntryId(const EntryIdentity& identity);
EntryIdentity IdentityOf(const Entry& entry);
bool SameIdentity(const Entry& entry, const EntryIdentity& identity);

}  // 命名空间 coral::history 结束

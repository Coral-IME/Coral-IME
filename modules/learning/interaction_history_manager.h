#pragma once

#include "interaction_history_storage.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace coral::history {

struct LearnInput {
    Partition partition = Partition::Simplified;
    std::string lookup_key;
    std::string full_pinyin;
    std::string phrase;
    std::uint16_t flags = kNone;
};

class Manager final {
public:
    explicit Manager(Options options = {});
    ~Manager();

    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    bool Start(const std::filesystem::path& legacyBase, std::string& error);
    void Shutdown();

    std::vector<Entry> Lookup(Partition partition, std::string_view lookupKey) const;
    std::vector<Entry> AllEntries() const;

    bool LearnBatch(const std::vector<LearnInput>& inputs);
    bool Upsert(Entry entry, bool recordRevert = false);
    // 词库导入的批量变体：整批只做一次增量排序。
    // 条目保留各自的 last_used_sequence，使导入不会
    // 使既有历史变旧。
    bool UpsertBatch(std::vector<Entry> entries);
    bool RemoveBatch(const std::vector<EntryIdentity>& identities);
    bool Remove(const EntryIdentity& identity, bool recordRevert = false);
    bool RevertLast(const std::string& phrase);
    bool ClearAll();

    bool Flush(std::string& error);
    void RequestCheckpoint();
    void RequestCompact();

    std::uint64_t currentSequence() const;
    std::uint64_t generation() const;
    std::filesystem::path path() const;

private:
    struct StatDelta {
        std::uint32_t commit_delta = 0;
        std::uint64_t last_used_sequence = 0;
        float score_state = 0.0f;
    };
    struct RevertItem {
        EntryIdentity identity;
        std::optional<Entry> before;
    };
    struct RevertRecord {
        std::string phrase;
        std::vector<RevertItem> items;
    };

    std::shared_ptr<const Snapshot> GetSnapshot() const;
    void PublishSnapshot(std::shared_ptr<const Snapshot> snapshot);
    std::optional<Entry> CurrentEntryLocked(const EntryIdentity& identity,
                                            const std::shared_ptr<const Snapshot>& base) const;
    void SetDeltaLocked(Entry entry);
    void SortDeltaLocked();
    void RemoveDeltaLocked(const EntryIdentity& identity);
    void QueueMutationLocked(Mutation mutation);
    void ApplyOverlayMutationLocked(const Mutation& mutation,
                                    const std::shared_ptr<const Snapshot>& base);
    void RebuildOverlaysLocked(const std::shared_ptr<const Snapshot>& base);
    void WorkerLoop();
    void RefreshFromDiskIfChanged();
    static float UpdatedScore(float oldScore, std::uint64_t oldSequence,
                              std::uint64_t newSequence);

    Options options_;
    std::unique_ptr<Storage> storage_;

    mutable std::mutex snapshot_mutex_;
    std::shared_ptr<const Snapshot> snapshot_;

    mutable std::mutex history_mutex_;
    std::vector<Entry> delta_;
    std::unordered_map<std::uint64_t, std::size_t> delta_index_;
    std::unordered_map<std::uint64_t, StatDelta> stats_;
    std::vector<EntryIdentity> removed_;
    std::vector<Mutation> mutations_;
    std::deque<RevertRecord> revert_queue_;
    std::uint64_t sequence_ = 0;
    std::uint64_t next_local_sequence_ = 0;
    bool dirty_ = false;
    bool force_checkpoint_ = false;
    bool clear_all_overlay_ = false;
    std::chrono::steady_clock::time_point dirty_since_{};

    mutable std::mutex flush_mutex_;
    std::mutex worker_mutex_;
    std::condition_variable worker_cv_;
    std::thread worker_;
    bool stop_ = false;
    bool started_ = false;
    std::filesystem::file_time_type observed_write_time_{};
};

}  // 命名空间 coral::history 结束

#pragma once

#include "candidate_rules_storage.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace coral::candidate_policy {

class Manager final {
public:
    explicit Manager(Options options = {});
    ~Manager();

    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    bool Start(const std::filesystem::path& legacyBase, std::string& error);
    void Shutdown();

    std::vector<Rule> Lookup(LanguageVariant variant, std::string_view lookupKey) const;
    std::vector<Rule> AllRules() const;
    bool SetRule(Rule rule);
    bool Remove(const RuleIdentity& identity);
    bool ClearAll();

    bool Flush(std::string& error);
    void RequestCheckpoint();
    std::filesystem::path path() const;

private:
    std::shared_ptr<const Snapshot> GetSnapshot() const;
    void PublishSnapshot(std::shared_ptr<const Snapshot> snapshot);
    void QueueMutationLocked(Mutation mutation);
    void WorkerLoop();
    void RefreshFromDiskIfChanged();

    Options options_;
    std::unique_ptr<Storage> storage_;

    mutable std::mutex snapshot_mutex_;
    std::shared_ptr<const Snapshot> snapshot_;

    mutable std::mutex state_mutex_;
    std::vector<Mutation> mutations_;
    std::uint64_t next_local_sequence_ = 0;
    std::uint64_t next_updated_sequence_ = 0;
    bool dirty_ = false;
    bool force_checkpoint_ = false;
    std::chrono::steady_clock::time_point dirty_since_{};

    mutable std::mutex flush_mutex_;
    std::mutex worker_mutex_;
    std::condition_variable worker_cv_;
    std::thread worker_;
    bool stop_ = false;
    bool started_ = false;
    std::filesystem::file_time_type observed_write_time_{};
};

}  // 命名空间 coral::candidate_policy 结束

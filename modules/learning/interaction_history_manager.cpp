#include "interaction_history_manager.h"

#include <algorithm>
#include <cmath>

namespace coral::history {
namespace {

bool IdentityEquals(const EntryIdentity& lhs, const EntryIdentity& rhs) {
    return lhs == rhs;
}

}  // namespace

Manager::Manager(Options options) : options_(options), snapshot_(Snapshot::Build(0, {})) {}

Manager::~Manager() {
    Shutdown();
}

bool Manager::Start(const std::filesystem::path& legacyBase, std::string& error) {
    std::lock_guard<std::mutex> workerLock(worker_mutex_);
    if (started_) return true;
    storage_ = std::make_unique<Storage>(legacyBase, options_);
    std::shared_ptr<const Snapshot> loaded;
    bool migrated = false;
    if (!storage_->Load(loaded, migrated, error)) return false;
    PublishSnapshot(loaded);
    sequence_ = 0;
    for (const Entry& entry : loaded->entries()) sequence_ = std::max(sequence_, entry.last_used_sequence);
    std::error_code ec;
    if (std::filesystem::exists(storage_->path(), ec)) observed_write_time_ = std::filesystem::last_write_time(storage_->path(), ec);
    stop_ = false;
    started_ = true;
    if (migrated) {
        dirty_ = true;
        force_checkpoint_ = true;
        dirty_since_ = std::chrono::steady_clock::now();
    }
    worker_ = std::thread([this] { WorkerLoop(); });
    return true;
}

void Manager::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (!started_) return;
    }
    std::string ignored;
    Flush(ignored);
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        stop_ = true;
    }
    worker_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(worker_mutex_);
    started_ = false;
}

std::shared_ptr<const Snapshot> Manager::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_;
}

void Manager::PublishSnapshot(std::shared_ptr<const Snapshot> snapshot) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    snapshot_ = std::move(snapshot);
}

std::optional<Entry> Manager::CurrentEntryLocked(const EntryIdentity& identity,
                                                  const std::shared_ptr<const Snapshot>& base) const {
    const auto removed = std::find_if(removed_.begin(), removed_.end(), [&](const EntryIdentity& value) {
        return IdentityEquals(value, identity);
    });
    if (removed != removed_.end()) return std::nullopt;
    const auto deltaIt = delta_index_.find(EntryId(identity));
    if (deltaIt != delta_index_.end() && deltaIt->second < delta_.size() &&
        SameIdentity(delta_[deltaIt->second], identity)) {
        return delta_[deltaIt->second];
    }
    if (clear_all_overlay_) return std::nullopt;
    const Entry* found = base->Find(identity);
    if (!found) return std::nullopt;
    Entry result = *found;
    const auto stat = stats_.find(result.entry_id);
    if (stat != stats_.end()) {
        result.commit_count += stat->second.commit_delta;
        result.last_used_sequence = std::max(result.last_used_sequence, stat->second.last_used_sequence);
        result.score_state = stat->second.score_state;
    }
    return result;
}

void Manager::SetDeltaLocked(Entry entry) {
    entry.entry_id = EntryId(IdentityOf(entry));
    removed_.erase(std::remove_if(removed_.begin(), removed_.end(), [&](const EntryIdentity& value) {
        return IdentityEquals(value, IdentityOf(entry));
    }), removed_.end());
    const auto it = delta_index_.find(entry.entry_id);
    if (it != delta_index_.end() && it->second < delta_.size() && SameIdentity(delta_[it->second], IdentityOf(entry))) {
        delta_[it->second] = std::move(entry);
        return;
    }
    delta_.push_back(std::move(entry));
    SortDeltaLocked();
}

void Manager::SortDeltaLocked() {
    std::sort(delta_.begin(), delta_.end(), [](const Entry& lhs, const Entry& rhs) {
        if (lhs.partition != rhs.partition) return lhs.partition < rhs.partition;
        if (lhs.lookup_key != rhs.lookup_key) return lhs.lookup_key < rhs.lookup_key;
        return lhs.phrase < rhs.phrase;
    });
    delta_index_.clear();
    for (std::size_t i = 0; i < delta_.size(); ++i) delta_index_[delta_[i].entry_id] = i;
}

void Manager::RemoveDeltaLocked(const EntryIdentity& identity) {
    delta_.erase(std::remove_if(delta_.begin(), delta_.end(), [&](const Entry& entry) {
        return SameIdentity(entry, identity);
    }), delta_.end());
    delta_index_.clear();
    for (std::size_t i = 0; i < delta_.size(); ++i) delta_index_[delta_[i].entry_id] = i;
    if (std::none_of(removed_.begin(), removed_.end(), [&](const EntryIdentity& value) { return value == identity; })) {
        removed_.push_back(identity);
    }
    stats_.erase(EntryId(identity));
}

void Manager::QueueMutationLocked(Mutation mutation) {
    mutation.local_sequence = ++next_local_sequence_;
    mutations_.push_back(std::move(mutation));
    if (!dirty_) dirty_since_ = std::chrono::steady_clock::now();
    dirty_ = true;
    worker_cv_.notify_one();
}

float Manager::UpdatedScore(float oldScore, std::uint64_t oldSequence, std::uint64_t newSequence) {
    float adjusted = oldScore;
    if (adjusted > 50.0f) adjusted -= 1.0f;
    float score = 1.0f + adjusted * std::exp((static_cast<float>(oldSequence) - static_cast<float>(newSequence)) / 200.0f);
    if (newSequence >= oldSequence && newSequence - oldSequence < 4000) score += 4.0f;
    return std::min(score, 55.0f);
}

std::vector<Entry> Manager::Lookup(Partition partition, std::string_view lookupKey) const {
    const auto base = GetSnapshot();
    std::lock_guard<std::mutex> lock(history_mutex_);
    std::vector<Entry> result = clear_all_overlay_ ? std::vector<Entry>{} : base->Lookup(partition, lookupKey);
    result.erase(std::remove_if(result.begin(), result.end(), [&](const Entry& entry) {
        return std::any_of(removed_.begin(), removed_.end(), [&](const EntryIdentity& value) {
            return SameIdentity(entry, value);
        });
    }), result.end());
    for (Entry& entry : result) {
        const auto stat = stats_.find(entry.entry_id);
        if (stat != stats_.end()) {
            entry.commit_count += stat->second.commit_delta;
            entry.last_used_sequence = std::max(entry.last_used_sequence, stat->second.last_used_sequence);
            entry.score_state = stat->second.score_state;
        }
    }
    for (const Entry& entry : delta_) {
        if (entry.partition != partition || entry.lookup_key != lookupKey) continue;
        auto existing = std::find_if(result.begin(), result.end(), [&](const Entry& value) {
            return value.entry_id == entry.entry_id && SameIdentity(value, IdentityOf(entry));
        });
        if (existing == result.end()) result.push_back(entry);
        else *existing = entry;
    }
    return result;
}

std::vector<Entry> Manager::AllEntries() const {
    const auto base = GetSnapshot();
    std::lock_guard<std::mutex> lock(history_mutex_);
    std::vector<Entry> result = clear_all_overlay_ ? std::vector<Entry>{} : base->entries();
    result.erase(std::remove_if(result.begin(), result.end(), [&](const Entry& entry) {
        return std::any_of(removed_.begin(), removed_.end(), [&](const EntryIdentity& value) {
            return SameIdentity(entry, value);
        });
    }), result.end());
    for (Entry& entry : result) {
        const auto stat = stats_.find(entry.entry_id);
        if (stat != stats_.end()) {
            entry.commit_count += stat->second.commit_delta;
            entry.last_used_sequence = std::max(entry.last_used_sequence, stat->second.last_used_sequence);
            entry.score_state = stat->second.score_state;
        }
    }
    for (const Entry& entry : delta_) {
        auto it = std::find_if(result.begin(), result.end(), [&](const Entry& value) {
            return value.entry_id == entry.entry_id && SameIdentity(value, IdentityOf(entry));
        });
        if (it == result.end()) result.push_back(entry); else *it = entry;
    }
    return result;
}

bool Manager::LearnBatch(const std::vector<LearnInput>& inputs) {
    if (inputs.empty()) return false;
    const auto base = GetSnapshot();
    std::lock_guard<std::mutex> lock(history_mutex_);
    RevertRecord revert;
    revert.phrase = inputs.front().phrase;
    for (const LearnInput& input : inputs) {
        if (input.lookup_key.empty() || input.phrase.empty()) continue;
        EntryIdentity identity{input.partition, input.lookup_key, input.phrase};
        auto before = CurrentEntryLocked(identity, base);
        revert.items.push_back(RevertItem{identity, before});
        const std::uint64_t sequence = ++sequence_;
        Entry seed;
        if (before) seed = *before;
        seed.partition = input.partition;
        seed.lookup_key = input.lookup_key;
        seed.full_pinyin = input.full_pinyin;
        seed.phrase = input.phrase;
        seed.flags |= input.flags;
        seed.entry_id = EntryId(identity);
        const float score = UpdatedScore(before ? before->score_state : 0.0f,
                                         before ? before->last_used_sequence : sequence, sequence);
        if (before && !delta_index_.count(seed.entry_id)) {
            StatDelta& stat = stats_[seed.entry_id];
            ++stat.commit_delta;
            stat.last_used_sequence = sequence;
            stat.score_state = score;
        } else {
            seed.commit_count = (before ? before->commit_count : 0) + 1;
            seed.last_used_sequence = sequence;
            seed.score_state = score;
            SetDeltaLocked(seed);
        }
        Mutation mutation;
        mutation.kind = MutationKind::Learn;
        mutation.identity = identity;
        mutation.commit_delta = 1;
        mutation.last_used_sequence = sequence;
        mutation.score_state = score;
        seed.commit_count = 0;  // 存储层应用增量，种子只携带结构化字段
        mutation.entry = std::move(seed);
        QueueMutationLocked(std::move(mutation));
    }
    if (!revert.items.empty()) {
        revert_queue_.push_back(std::move(revert));
        while (revert_queue_.size() > 3) revert_queue_.pop_front();
    }
    return true;
}

bool Manager::Upsert(Entry entry, bool recordRevert) {
    if (entry.lookup_key.empty() || entry.phrase.empty()) return false;
    const auto base = GetSnapshot();
    std::lock_guard<std::mutex> lock(history_mutex_);
    EntryIdentity identity = IdentityOf(entry);
    if (recordRevert) {
        RevertRecord record;
        record.phrase = entry.phrase;
        record.items.push_back({identity, CurrentEntryLocked(identity, base)});
        revert_queue_.push_back(std::move(record));
        while (revert_queue_.size() > 3) revert_queue_.pop_front();
    }
    entry.entry_id = EntryId(identity);
    entry.last_used_sequence = entry.last_used_sequence ? entry.last_used_sequence : ++sequence_;
    sequence_ = std::max(sequence_, entry.last_used_sequence);
    SetDeltaLocked(entry);
    Mutation mutation;
    mutation.kind = MutationKind::Upsert;
    mutation.identity = identity;
    mutation.entry = std::move(entry);
    QueueMutationLocked(std::move(mutation));
    return true;
}

bool Manager::UpsertBatch(std::vector<Entry> entries) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    std::unordered_set<std::uint64_t> touched;
    for (Entry& entry : entries) {
        if (entry.lookup_key.empty() || entry.phrase.empty()) continue;
        const EntryIdentity identity = IdentityOf(entry);
        entry.entry_id = EntryId(identity);
        if (!entry.last_used_sequence) entry.last_used_sequence = sequence_ ? sequence_ : 1;
        sequence_ = std::max(sequence_, entry.last_used_sequence);
        touched.insert(entry.entry_id);
        stats_.erase(entry.entry_id);
        const auto it = delta_index_.find(entry.entry_id);
        if (it != delta_index_.end() && it->second < delta_.size() && SameIdentity(delta_[it->second], identity)) {
            delta_[it->second] = entry;
        } else {
            delta_index_[entry.entry_id] = delta_.size();
            delta_.push_back(entry);
        }
        Mutation mutation;
        mutation.kind = MutationKind::Upsert;
        mutation.identity = identity;
        mutation.entry = std::move(entry);
        QueueMutationLocked(std::move(mutation));
    }
    if (touched.empty()) return false;
    removed_.erase(std::remove_if(removed_.begin(), removed_.end(), [&](const EntryIdentity& value) {
        return touched.count(EntryId(value)) != 0;
    }), removed_.end());
    SortDeltaLocked();
    return true;
}

bool Manager::RemoveBatch(const std::vector<EntryIdentity>& identities) {
    if (identities.empty()) return false;
    std::lock_guard<std::mutex> lock(history_mutex_);
    std::unordered_set<std::uint64_t> ids;
    for (const EntryIdentity& identity : identities) ids.insert(EntryId(identity));
    delta_.erase(std::remove_if(delta_.begin(), delta_.end(), [&](const Entry& entry) {
        return ids.count(entry.entry_id) != 0;
    }), delta_.end());
    delta_index_.clear();
    for (std::size_t i = 0; i < delta_.size(); ++i) delta_index_[delta_[i].entry_id] = i;
    std::unordered_set<std::uint64_t> alreadyRemoved;
    for (const EntryIdentity& value : removed_) alreadyRemoved.insert(EntryId(value));
    for (const EntryIdentity& identity : identities) {
        const std::uint64_t id = EntryId(identity);
        stats_.erase(id);
        if (alreadyRemoved.insert(id).second) removed_.push_back(identity);
        Mutation mutation;
        mutation.kind = MutationKind::Remove;
        mutation.identity = identity;
        QueueMutationLocked(std::move(mutation));
    }
    return true;
}

bool Manager::Remove(const EntryIdentity& identity, bool recordRevert) {
    const auto base = GetSnapshot();
    std::lock_guard<std::mutex> lock(history_mutex_);
    const auto before = CurrentEntryLocked(identity, base);
    if (!before) return false;
    if (recordRevert) {
        RevertRecord record;
        record.phrase = identity.phrase;
        record.items.push_back({identity, before});
        revert_queue_.push_back(std::move(record));
        while (revert_queue_.size() > 3) revert_queue_.pop_front();
    }
    RemoveDeltaLocked(identity);
    Mutation mutation;
    mutation.kind = MutationKind::Remove;
    mutation.identity = identity;
    QueueMutationLocked(std::move(mutation));
    return true;
}

bool Manager::RevertLast(const std::string& phrase) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    if (revert_queue_.empty()) return false;
    RevertRecord record = revert_queue_.back();
    if (phrase != "ANYWORD" && record.phrase != phrase) return false;
    revert_queue_.pop_back();
    for (const RevertItem& item : record.items) {
        Mutation mutation;
        mutation.identity = item.identity;
        if (item.before) {
            SetDeltaLocked(*item.before);
            mutation.kind = MutationKind::Upsert;
            mutation.entry = item.before;
        } else {
            RemoveDeltaLocked(item.identity);
            mutation.kind = MutationKind::Remove;
        }
        QueueMutationLocked(std::move(mutation));
    }
    return true;
}

bool Manager::ClearAll() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    delta_.clear();
    delta_index_.clear();
    stats_.clear();
    removed_.clear();
    revert_queue_.clear();
    clear_all_overlay_ = true;
    Mutation mutation;
    mutation.kind = MutationKind::ClearAll;
    QueueMutationLocked(std::move(mutation));
    return true;
}

void Manager::ApplyOverlayMutationLocked(const Mutation& mutation,
                                         const std::shared_ptr<const Snapshot>& base) {
    if (mutation.kind == MutationKind::ClearAll) {
        delta_.clear(); delta_index_.clear(); stats_.clear(); removed_.clear(); clear_all_overlay_ = true;
    } else if (mutation.kind == MutationKind::Remove) {
        RemoveDeltaLocked(mutation.identity);
    } else if (mutation.kind == MutationKind::Upsert && mutation.entry) {
        SetDeltaLocked(*mutation.entry);
    } else if (mutation.kind == MutationKind::Learn) {
        auto current = CurrentEntryLocked(mutation.identity, base);
        Entry entry = mutation.entry.value_or(Entry{});
        if (current) entry = *current;
        entry.partition = mutation.identity.partition;
        entry.lookup_key = mutation.identity.lookup_key;
        entry.phrase = mutation.identity.phrase;
        if (mutation.entry) {
            if (!mutation.entry->full_pinyin.empty()) entry.full_pinyin = mutation.entry->full_pinyin;
            entry.flags |= mutation.entry->flags;
        }
        entry.commit_count += mutation.commit_delta;
        entry.last_used_sequence = std::max(entry.last_used_sequence, mutation.last_used_sequence);
        entry.score_state = mutation.score_state;
        SetDeltaLocked(std::move(entry));
    }
}

void Manager::RebuildOverlaysLocked(const std::shared_ptr<const Snapshot>& base) {
    delta_.clear(); delta_index_.clear(); stats_.clear(); removed_.clear(); clear_all_overlay_ = false;
    for (const Mutation& mutation : mutations_) ApplyOverlayMutationLocked(mutation, base);
}

bool Manager::Flush(std::string& error) {
    std::lock_guard<std::mutex> flushLock(flush_mutex_);
    if (!storage_) return true;
    std::vector<Mutation> captured;
    std::uint64_t watermark = 0;
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        if (mutations_.empty() && !force_checkpoint_) return true;
        captured = mutations_;
        if (!captured.empty()) watermark = captured.back().local_sequence;
    }
    std::shared_ptr<const Snapshot> persisted;
    if (!storage_->Checkpoint(captured, persisted, error)) return false;
    PublishSnapshot(persisted);
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        mutations_.erase(std::remove_if(mutations_.begin(), mutations_.end(), [&](const Mutation& mutation) {
            return mutation.local_sequence <= watermark;
        }), mutations_.end());
        force_checkpoint_ = false;
        dirty_ = !mutations_.empty();
        if (dirty_) dirty_since_ = std::chrono::steady_clock::now();
        RebuildOverlaysLocked(persisted);
    }
    std::error_code ec;
    observed_write_time_ = std::filesystem::last_write_time(storage_->path(), ec);
    return true;
}

void Manager::RequestCheckpoint() {
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        force_checkpoint_ = true;
        if (!dirty_) dirty_since_ = std::chrono::steady_clock::now();
        dirty_ = true;
    }
    worker_cv_.notify_one();
}

void Manager::RequestCompact() {
    RequestCheckpoint();
}

void Manager::RefreshFromDiskIfChanged() {
    if (!storage_) return;
    std::error_code ec;
    if (!std::filesystem::exists(storage_->path(), ec)) return;
    const auto writeTime = std::filesystem::last_write_time(storage_->path(), ec);
    if (ec || writeTime == observed_write_time_) return;
    bool hasPending = false;
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        hasPending = !mutations_.empty();
    }
    if (hasPending) {
        std::string ignored;
        Flush(ignored);
        return;
    }
    std::shared_ptr<const Snapshot> loaded;
    bool migrated = false;
    std::string ignored;
    if (storage_->Load(loaded, migrated, ignored) && loaded->generation() > generation()) {
        PublishSnapshot(loaded);
        std::lock_guard<std::mutex> lock(history_mutex_);
        sequence_ = 0;
        for (const Entry& entry : loaded->entries()) sequence_ = std::max(sequence_, entry.last_used_sequence);
    }
    observed_write_time_ = writeTime;
}

void Manager::WorkerLoop() {
    auto nextPoll = std::chrono::steady_clock::now();
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(worker_mutex_);
            worker_cv_.wait_for(lock, std::chrono::milliseconds(500), [&] { return stop_; });
            if (stop_) break;
        }
        bool shouldFlush = false;
        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            const auto elapsed = dirty_ ? std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - dirty_since_).count() : 0;
            shouldFlush = dirty_ && (force_checkpoint_ || mutations_.size() >= options_.mutation_checkpoint_threshold ||
                                     delta_.size() >= options_.delta_compact_threshold ||
                                     elapsed >= options_.checkpoint_delay_ms);
        }
        if (shouldFlush) {
            std::string ignored;
            Flush(ignored);
        }
        if (std::chrono::steady_clock::now() >= nextPoll) {
            RefreshFromDiskIfChanged();
            nextPoll = std::chrono::steady_clock::now() + std::chrono::milliseconds(options_.external_change_poll_ms);
        }
    }
}

std::uint64_t Manager::currentSequence() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return sequence_;
}

std::uint64_t Manager::generation() const {
    return GetSnapshot()->generation();
}

std::filesystem::path Manager::path() const {
    return storage_ ? storage_->path() : std::filesystem::path{};
}

}  // 命名空间 coral::history 结束

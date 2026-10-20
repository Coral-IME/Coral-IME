#include "candidate_rules_manager.h"

#include <algorithm>

namespace coral::candidate_policy {

Manager::Manager(Options options)
    : options_(options), snapshot_(Snapshot::Build(0, {})) {}

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
    next_updated_sequence_ = 0;
    for (const Rule& rule : loaded->rules()) {
        next_updated_sequence_ = std::max(next_updated_sequence_, rule.updated_sequence);
    }
    std::error_code ec;
    if (std::filesystem::exists(storage_->path(), ec)) {
        observed_write_time_ = std::filesystem::last_write_time(storage_->path(), ec);
    }
    stop_ = false;
    started_ = true;
    if (migrated) {
        // 把导入的规则原样持久化为变更。历史管理器
        // 可能在本工作线程运行前移除旧的嵌入策略分区。
        for (const Rule& rule : loaded->rules()) {
            Mutation mutation;
            mutation.kind = MutationKind::Set;
            mutation.identity = IdentityOf(rule);
            mutation.rule = rule;
            mutation.local_sequence = ++next_local_sequence_;
            mutations_.push_back(std::move(mutation));
        }
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

std::vector<Rule> Manager::Lookup(LanguageVariant variant, std::string_view lookupKey) const {
    return GetSnapshot()->Lookup(variant, lookupKey);
}

std::vector<Rule> Manager::AllRules() const {
    return GetSnapshot()->rules();
}

void Manager::QueueMutationLocked(Mutation mutation) {
    mutation.local_sequence = ++next_local_sequence_;
    mutations_.push_back(std::move(mutation));
    if (!dirty_) dirty_since_ = std::chrono::steady_clock::now();
    dirty_ = true;
    worker_cv_.notify_one();
}

bool Manager::SetRule(Rule rule) {
    if (!coral::IsValidLanguageVariant(rule.variant) ||
        (rule.kind != RuleKind::Promote && rule.kind != RuleKind::Disable) ||
        rule.lookup_key.empty() || rule.phrase.empty()) return false;
    if (rule.kind == RuleKind::Disable) {
        if (!coral::CanDisableCandidate(rule.phrase)) return false;
        rule.pinned = false;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    const auto base = GetSnapshot();
    std::vector<Mutation> immediate;
    if (rule.kind == RuleKind::Promote && rule.pinned) {
        for (const Rule& existing : base->Lookup(rule.variant, rule.lookup_key)) {
            if (existing.pinned && existing.phrase != rule.phrase) {
                Mutation remove;
                remove.kind = MutationKind::Remove;
                remove.identity = IdentityOf(existing);
                immediate.push_back(remove);
            }
        }
    }
    rule.updated_sequence = ++next_updated_sequence_;
    rule.rule_id = RuleId(IdentityOf(rule));
    Mutation set;
    set.kind = MutationKind::Set;
    set.identity = IdentityOf(rule);
    set.rule = rule;
    immediate.push_back(set);
    auto rules = Storage::ApplyMutations(base->rules(), immediate);
    if (rules.size() > options_.max_rules) return false;
    for (Mutation& mutation : immediate) QueueMutationLocked(std::move(mutation));
    PublishSnapshot(Snapshot::Build(base->generation(), std::move(rules)));
    return true;
}

bool Manager::Remove(const RuleIdentity& identity) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const auto base = GetSnapshot();
    if (!base->Find(identity)) return false;
    Mutation mutation;
    mutation.kind = MutationKind::Remove;
    mutation.identity = identity;
    auto rules = Storage::ApplyMutations(base->rules(), {mutation});
    QueueMutationLocked(std::move(mutation));
    PublishSnapshot(Snapshot::Build(base->generation(), std::move(rules)));
    return true;
}

bool Manager::ClearAll() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    Mutation mutation;
    mutation.kind = MutationKind::ClearAll;
    QueueMutationLocked(std::move(mutation));
    PublishSnapshot(Snapshot::Build(GetSnapshot()->generation(), {}));
    return true;
}

bool Manager::Flush(std::string& error) {
    std::lock_guard<std::mutex> flushLock(flush_mutex_);
    if (!storage_) return true;
    std::vector<Mutation> captured;
    std::uint64_t watermark = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (mutations_.empty() && !force_checkpoint_) return true;
        captured = mutations_;
        if (!captured.empty()) watermark = captured.back().local_sequence;
    }
    std::shared_ptr<const Snapshot> persisted;
    if (!storage_->Checkpoint(captured, persisted, error)) return false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        mutations_.erase(std::remove_if(mutations_.begin(), mutations_.end(),
            [&](const Mutation& mutation) { return mutation.local_sequence <= watermark; }), mutations_.end());
        force_checkpoint_ = false;
        dirty_ = !mutations_.empty();
        if (dirty_) dirty_since_ = std::chrono::steady_clock::now();
        auto visible = Storage::ApplyMutations(persisted->rules(), mutations_);
        PublishSnapshot(Snapshot::Build(persisted->generation(), std::move(visible)));
    }
    std::error_code ec;
    observed_write_time_ = std::filesystem::last_write_time(storage_->path(), ec);
    return true;
}

void Manager::RequestCheckpoint() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        force_checkpoint_ = true;
        if (!dirty_) dirty_since_ = std::chrono::steady_clock::now();
        dirty_ = true;
    }
    worker_cv_.notify_one();
}

void Manager::RefreshFromDiskIfChanged() {
    if (!storage_) return;
    std::error_code ec;
    if (!std::filesystem::exists(storage_->path(), ec)) return;
    const auto writeTime = std::filesystem::last_write_time(storage_->path(), ec);
    if (ec || writeTime == observed_write_time_) return;
    bool hasPending = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
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
    if (storage_->Load(loaded, migrated, ignored) && loaded->generation() > GetSnapshot()->generation()) {
        PublishSnapshot(loaded);
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (const Rule& rule : loaded->rules()) {
            next_updated_sequence_ = std::max(next_updated_sequence_, rule.updated_sequence);
        }
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
            std::lock_guard<std::mutex> lock(state_mutex_);
            const auto elapsed = dirty_ ? std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - dirty_since_).count() : 0;
            shouldFlush = dirty_ && (force_checkpoint_ ||
                mutations_.size() >= options_.mutation_checkpoint_threshold ||
                elapsed >= options_.checkpoint_delay_ms);
        }
        if (shouldFlush) {
            std::string ignored;
            Flush(ignored);
        }
        if (std::chrono::steady_clock::now() >= nextPoll) {
            RefreshFromDiskIfChanged();
            nextPoll = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(options_.external_change_poll_ms);
        }
    }
}

std::filesystem::path Manager::path() const {
    return storage_ ? storage_->path() : std::filesystem::path{};
}

}  // 命名空间 coral::candidate_policy 结束

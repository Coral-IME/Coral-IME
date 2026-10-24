#include "coral/language_model/character_rerank.h"
#include <algorithm>
#include <chrono>
#include <utility>

namespace coral::language_model {
namespace {
thread_local CharacterRerankScope* current_scope = nullptr;
bool matches(const CharacterRerankInput& input, const std::shared_ptr<const CharacterModel>& model,
    std::string_view context, const std::vector<std::string_view>& texts) {
    return input.model == model && input.context == context && input.texts.size() == texts.size() &&
        std::equal(input.texts.begin(), input.texts.end(), texts.begin());
}
}

CharacterRerankScope::CharacterRerankScope(CharacterRerankBatch& capture)
    : previous_(current_scope), capture_(&capture) { current_scope = this; }
CharacterRerankScope::CharacterRerankScope(const CharacterRerankBatch& completed)
    : previous_(current_scope), completed_(&completed), matched_(completed.size(), false) { current_scope = this; }
CharacterRerankScope::~CharacterRerankScope() { current_scope = previous_; }
bool CharacterRerankScope::replayed_all() const {
    return completed_ && !mismatch_ && std::all_of(matched_.begin(), matched_.end(), [](bool value) { return value; });
}
bool CharacterRerankScope::score(std::shared_ptr<const CharacterModel> model, std::string_view context,
    const std::vector<std::string_view>& texts, std::vector<double>& scores) {
    auto* scope = current_scope;
    if (!scope) return model && model->score_candidates(context, texts, scores);
    if (scope->capture_) {
        auto& batch = *scope->capture_;
        if (!model || batch.size() >= 8) return false;
        if (std::none_of(batch.begin(), batch.end(), [&](const auto& item) { return matches(item, model, context, texts); })) {
            CharacterRerankInput input;
            input.model = std::move(model); input.context.assign(context);
            for (auto text : texts) input.texts.emplace_back(text);
            batch.push_back(std::move(input));
        }
        return false;
    }
    for (size_t i = 0; i < scope->completed_->size(); ++i) {
        const auto& input = (*scope->completed_)[i];
        if (matches(input, model, context, texts) && input.scores.size() == texts.size()) {
            scope->matched_[i] = true; scores = input.scores; return true;
        }
    }
    scope->mismatch_ = true;
    return false;
}

AsyncCharacterReranker::~AsyncCharacterReranker() { stop(); }
std::shared_ptr<CharacterRerankTask> AsyncCharacterReranker::submit(CharacterRerankBatch batch) {
    auto task = std::make_shared<CharacterRerankTask>();
    task->batch = std::move(batch);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) { task->cancelled = true; task->finished = true; return task; }
        if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
        if (pending_) pending_->cancelled = true;
        if (active_) active_->cancelled = true;
        pending_ = task;
    }
    wake_.notify_one();
    return task;
}
void AsyncCharacterReranker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        if (pending_) pending_->cancelled = true;
        if (active_) active_->cancelled = true;
        pending_.reset();
    }
    wake_.notify_one();
    if (thread_.joinable()) thread_.join();
}
void AsyncCharacterReranker::run() {
    if (prepare_worker_) { try { prepare_worker_(); } catch (...) {} }
    for (;;) {
        std::shared_ptr<CharacterRerankTask> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || pending_; });
            if (stopping_) return;
            task = std::move(pending_); active_ = task;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
        const auto cancelled = [&] { return task->cancelled.load(std::memory_order_relaxed) ||
            std::chrono::steady_clock::now() >= deadline; };
        bool succeeded = !cancelled();
        try {
            for (auto& input : task->batch) {
                std::vector<std::string_view> texts(input.texts.begin(), input.texts.end());
                if (cancelled() || !input.model || !input.model->score_candidates(input.context, texts, input.scores, cancelled)) {
                    succeeded = false; break;
                }
            }
        } catch (...) { succeeded = false; }
        {
            std::lock_guard<std::mutex> lock(task->mutex);
            task->succeeded = succeeded && !cancelled(); task->finished = true;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_ == task) active_.reset();
        }
    }
}
} // 命名空间 coral::language_model 结束

#ifndef CORAL_INCLUDE_CORAL_LANGUAGE_MODEL_CHARACTER_RERANK_H_INCLUDED
#define CORAL_INCLUDE_CORAL_LANGUAGE_MODEL_CHARACTER_RERANK_H_INCLUDED

#include "character_model.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace coral::language_model {

struct CharacterRerankInput {
    std::shared_ptr<const CharacterModel> model;
    std::string context;
    std::vector<std::string> texts;
    std::vector<double> scores;
};
using CharacterRerankBatch = std::vector<CharacterRerankInput>;

// 解析线程只收集拥有文本与模型的快照，或使用已完成的评分；不借用解析器内存。
class CharacterRerankScope {
public:
    explicit CharacterRerankScope(CharacterRerankBatch& capture);
    explicit CharacterRerankScope(const CharacterRerankBatch& completed);
    ~CharacterRerankScope();
    CharacterRerankScope(const CharacterRerankScope&) = delete;
    CharacterRerankScope& operator=(const CharacterRerankScope&) = delete;
    bool replayed_all() const;
    static bool score(std::shared_ptr<const CharacterModel> model, std::string_view context,
        const std::vector<std::string_view>& texts, std::vector<double>& scores);
private:
    CharacterRerankScope* previous_;
    CharacterRerankBatch* capture_ = nullptr;
    const CharacterRerankBatch* completed_ = nullptr;
    std::vector<bool> matched_;
    bool mismatch_ = false;
};

struct CharacterRerankTask {
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    bool finished = false;
    bool succeeded = false;
    CharacterRerankBatch batch;
};

// 一个宿主最多一个执行中任务与一个最新待执行任务；计算不获取引擎锁。
class AsyncCharacterReranker {
public:
    explicit AsyncCharacterReranker(std::function<void()> prepare_worker = {}) : prepare_worker_(std::move(prepare_worker)) {}
    ~AsyncCharacterReranker();
    std::shared_ptr<CharacterRerankTask> submit(CharacterRerankBatch batch);
    void stop();
private:
    void run();
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    bool stopping_ = false;
    std::shared_ptr<CharacterRerankTask> pending_, active_;
    std::function<void()> prepare_worker_;
};

} // 命名空间 coral::language_model 结束
#endif

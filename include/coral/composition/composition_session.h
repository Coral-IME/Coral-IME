// 既有的会话工作源自 Coral 项目；注入的状态机在此维护，
// 独立于平台与词典实现细节。
#ifndef CORAL_INCLUDE_CORAL_COMPOSITION_COMPOSITION_SESSION_H_INCLUDED
#define CORAL_INCLUDE_CORAL_COMPOSITION_COMPOSITION_SESSION_H_INCLUDED
#include "composition_types.h"
#include <memory>
#include <string>

namespace coral::composition
{
constexpr int kMaxPageSize = 512;
constexpr std::size_t kMaxInputBytes = 256 * 1024;

class CandidateSource
{
public:
    virtual ~CandidateSource() = default;
    virtual bool query_candidates(const std::string& condition, const std::string& pinyin,
        int offset, int page_size, int mode, CandidatePage& page) = 0;
    virtual void remember_selection(const Candidate& candidate) = 0;
};

class CompositionSession
{
public:
    // 默认适配器由 runtime 提供，位于可移植目标之外。
    CompositionSession();
    explicit CompositionSession(std::shared_ptr<CandidateSource> source);
    Output send_key(const KeyEvent& event, const InputContext& context);
    Output select_index(int index_in_page);
    Output page_candidates(int delta);
    Output reset();
    Output query_page(const std::string& condition, const std::string& pinyin, int offset,
        int mode = kModeQuanpin);
    bool has_input() const { return !pinyin_.empty(); }
    const std::string& pinyin() const { return pinyin_; }
    const std::string& condition() const { return condition_; }
    int page_size() const { return page_size_; }
    bool set_page_size(int size);

private:
    Output snapshot(bool consumed = false) const;
    void requery(int offset);
    CandidatePage fetch_page(const std::string& condition, const std::string& pinyin, int offset) const;
    std::shared_ptr<CandidateSource> source_;
    std::string pinyin_;
    std::string condition_;
    int mode_ = kModeQuanpin;
    int page_size_ = 5;
    CandidatePage page_;
};
}
#endif // CORAL_INCLUDE_CORAL_COMPOSITION_COMPOSITION_SESSION_H_INCLUDED

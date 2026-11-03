#include "coral/composition/composition_session.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace coral::composition
{
namespace
{
bool pinyin_character(std::uint32_t value)
{
    return (value >= 'a' && value <= 'z') || value == '\'';
}

std::size_t converted_prefix(const std::string& input, const std::string& selected)
{
    std::string letters;
    for (char letter : selected) if (letter != '\'') letters.push_back(letter);
    if (letters.empty()) return input.size();
    std::size_t index = 0, matched = 0;
    while (index < input.size() && matched < letters.size())
    {
        const char letter = input[index++];
        if (letter == '\'') continue;
        if (letter != letters[matched++]) return input.size();
    }
    if (matched != letters.size()) return input.size();
    while (index < input.size() && input[index] == '\'') ++index;
    return index;
}
}

CompositionSession::CompositionSession(std::shared_ptr<CandidateSource> source) : source_(std::move(source))
{
    if (!source_) throw std::invalid_argument("composition needs a candidate source");
}

bool CompositionSession::set_page_size(int size)
{
    if (size <= 0 || size > kMaxPageSize) return false;
    page_size_ = size;
    return true;
}

Output CompositionSession::snapshot(bool consumed) const
{
    Output output;
    output.consumed = consumed;
    output.composition = pinyin_;
    output.candidates = page_;
    output.candidates_visible = !page_.empty();
    return output;
}

CandidatePage CompositionSession::fetch_page(const std::string& condition, const std::string& pinyin, int offset) const
{
    CandidatePage next;
    next.offset = offset;
    if (!pinyin.empty())
    {
        if (!source_->query_candidates(condition, pinyin, offset, page_size_, mode_, next))
            next = CandidatePage{};
        // 失败的查询不能让旧输入的可选中候选残留。
        next.offset = offset;
    }
    return next;
}

void CompositionSession::requery(int offset)
{
    page_ = fetch_page(condition_, pinyin_, offset);
}

Output CompositionSession::query_page(const std::string& condition, const std::string& pinyin, int offset, int mode)
{
    if (condition.size() > kMaxInputBytes || pinyin.size() > kMaxInputBytes || offset < 0 ||
        offset > std::numeric_limits<int>::max() - page_size_ ||
        (mode != kModeQuanpin && mode != kModeDoublepinyin)) return snapshot();
    const int previous_mode = mode_;
    mode_ = mode;
    CandidatePage next;
    try { next = fetch_page(condition, pinyin, offset); }
    catch (...) { mode_ = previous_mode; throw; }
    condition_ = condition;
    pinyin_ = pinyin;
    page_ = std::move(next);
    return snapshot();
}

Output CompositionSession::reset()
{
    const bool had_input = has_input();
    pinyin_.clear(); condition_.clear(); page_ = {}; mode_ = kModeQuanpin;
    return snapshot(had_input);
}

Output CompositionSession::select_index(int index_in_page)
{
    if (index_in_page < 0 || static_cast<std::size_t>(index_in_page) >= page_.candidates.size()) return snapshot();
    const Candidate selected = page_.candidates[static_cast<std::size_t>(index_in_page)];
    const auto prefix = converted_prefix(pinyin_, selected.pinyin);
    // 学习调用先于状态变更，异常时保持会话不变。
    source_->remember_selection(selected);
    const auto remaining = pinyin_.substr(prefix);
    if (remaining.empty())
    {
        pinyin_.clear(); condition_.clear(); page_ = {}; mode_ = kModeQuanpin;
    }
    else
    {
        const auto condition = condition_ + selected.word;
        auto next = fetch_page(condition, remaining, 0);
        pinyin_ = remaining; condition_ = condition; page_ = std::move(next);
    }
    auto output = snapshot(true);
    output.commit_text = selected.word;
    return output;
}

Output CompositionSession::page_candidates(int delta)
{
    if (!has_input() || delta == 0) return snapshot();
    if (delta > 0 && !page_.has_next_page) return snapshot(true);
    const std::int64_t next = static_cast<std::int64_t>(page_.offset) + static_cast<std::int64_t>(delta) * page_size_;
    if (next > std::numeric_limits<int>::max() - page_size_) return snapshot(true);
    const int offset = next < 0 ? 0 : static_cast<int>(next);
    if (offset != page_.offset) requery(offset);
    return snapshot(true);
}

Output CompositionSession::send_key(const KeyEvent& event, const InputContext&)
{
    if (event.ctrl || event.alt) return snapshot();
    if (pinyin_character(event.codepoint))
    {
        if (pinyin_.size() >= kMaxInputBytes) return snapshot();
        auto output = query_page(condition_, pinyin_ + static_cast<char>(event.codepoint), 0, mode_);
        output.consumed = true;
        return output;
    }
    if (event.key == Key::kEscape) return reset();
    if (event.key == Key::kBackspace && has_input())
    {
        auto output = query_page(condition_, pinyin_.substr(0, pinyin_.size() - 1), 0, mode_);
        output.consumed = true;
        return output;
    }
    if (event.key == Key::kSpace && !page_.empty()) return select_index(0);
    if (event.codepoint >= '1' && event.codepoint <= '9' && has_input())
    {
        const int index = static_cast<int>(event.codepoint - '1');
        if (static_cast<std::size_t>(index) < page_.candidates.size()) return select_index(index);
        return snapshot(true);
    }
    if (event.key == Key::kPageDown) return page_candidates(1);
    if (event.key == Key::kPageUp) return page_candidates(-1);
    return snapshot();
}
}

#include "coral/composition/session_port.h"
#include "coral/composition/composition_session.h"
#include "text_file_helpers.hpp"
#include "core_operations.h"
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace coral::composition
{
namespace
{
static_assert(FUZZY_CHOICES == kSessionFuzzyChoiceCount, "session fuzzy settings must match the legacy model");
// 所有权迁移期间，解码器与设置仍是进程级全局。本二进制中的
// 每个本地端口共享同一道门；各会话映射则不是。
std::mutex runtime_gate;
SessionToken next_session_token = 1;

bool valid_text(const std::string& text)
{
    return text.size() <= kMaxInputBytes && text.find('\0') == std::string::npos;
}

class LocalSessionPort final : public SessionPort
{
public:
    explicit LocalSessionPort(bool owns_runtime = false)
        : owns_runtime_(owns_runtime), runtime_generation_(coral::runtime::runtime_ready() ? coral::runtime::runtime_generation() : 0) {}
    ~LocalSessionPort() override
    {
        std::lock_guard<std::mutex> lock(runtime_gate);
        sessions_.clear();
        if (owns_runtime_ && current_runtime()) coral::runtime::stop_runtime();
    }
    SessionToken open_session() override
    {
        std::lock_guard<std::mutex> lock(runtime_gate);
        if (!current_runtime() || next_session_token == 0) return 0;
        const auto token = next_session_token;
        sessions_.emplace(token, SessionState{});
        next_session_token = token == std::numeric_limits<SessionToken>::max() ? 0 : token + 1;
        return token;
    }
    void close_session(SessionToken token) override
    {
        std::lock_guard<std::mutex> lock(runtime_gate);
        sessions_.erase(token);
    }
    bool available() const override
    {
        std::lock_guard<std::mutex> lock(runtime_gate);
        return current_runtime();
    }
    bool flush_history() override
    {
        std::lock_guard<std::mutex> lock(runtime_gate);
        return current_runtime() && coral::runtime::flush_user_lexicon();
    }
    SessionResult send_key(SessionToken token, std::uint64_t request_id, const KeyEvent& event, const InputContext& context) override
    {
        const bool valid = static_cast<std::uint32_t>(event.key) <= static_cast<std::uint32_t>(Key::kDelete) &&
            event.codepoint <= 0x10ffff && (event.codepoint < 0xd800 || event.codepoint > 0xdfff);
        return invoke(token, request_id, valid, [&](SessionState& state, SessionResult& result) {
            result.output = state.session.send_key(event, context);
        });
    }
    SessionResult query_page(SessionToken token, std::uint64_t request_id, const std::string& condition,
        const std::string& pinyin, int offset, int page_size, int mode) override
    {
        const bool valid = valid_text(condition) && valid_text(pinyin) && page_size > 0 && page_size <= kMaxPageSize &&
            offset >= 0 && offset <= std::numeric_limits<int>::max() - page_size &&
            (mode == kModeQuanpin || mode == kModeDoublepinyin);
        return invoke(token, request_id, valid, [&](SessionState& state, SessionResult& result) {
            state.session.set_page_size(page_size);
            result.output = state.session.query_page(condition, pinyin, offset, mode);
        });
    }
    SessionResult select_candidate(SessionToken token, std::uint64_t request_id, int index) override
    {
        return invoke(token, request_id, index >= 0 && index < kMaxPageSize, [&](SessionState& state, SessionResult& result) {
            result.output = state.session.select_index(index);
        });
    }
    SessionResult reset_session(SessionToken token, std::uint64_t request_id) override
    {
        return invoke(token, request_id, true, [](SessionState& state, SessionResult& result) { result.output = state.session.reset(); });
    }
    SessionResult record_selection(SessionToken token, std::uint64_t request_id, const std::string& pinyin,
        const std::string& word, std::uint32_t condition_id) override
    {
        return invoke(token, request_id, valid_text(pinyin) && valid_text(word), [&](SessionState&, SessionResult& result) {
            coral::runtime::remember_candidate(pinyin, word, condition_id); result.value = condition_id;
        });
    }
    SessionResult revert_selection(SessionToken token, std::uint64_t request_id, const std::string& word) override
    {
        return invoke(token, request_id, valid_text(word), [&](SessionState&, SessionResult& result) {
            result.result = coral::runtime::revert_phrase_learning(word);
        });
    }
    SessionResult apply_candidate_action(SessionToken token, std::uint64_t request_id, const std::string& pinyin,
        const std::string& word, CandidateAction action, LanguageVariant variant) override
    {
        return invoke(token, request_id, valid_text(pinyin) && valid_text(word) && IsValidCandidateAction(action) && IsValidLanguageVariant(variant),
            [&](SessionState&, SessionResult& result) { result.result = coral::runtime::apply_candidate_rule(pinyin, word, action, variant); });
    }
    SessionResult clear_cache(SessionToken token, std::uint64_t request_id) override
    {
        return invoke(token, request_id, true, [](SessionState&, SessionResult&) { coral::runtime::clear_parse_cache(); });
    }
    SessionResult refresh_lexicon(SessionToken token, std::uint64_t request_id) override
    {
        return invoke(token, request_id, true, [](SessionState&, SessionResult&) { coral::runtime::refresh_lexicon_state(); });
    }
    SessionResult apply_settings(SessionToken token, std::uint64_t request_id, const SessionSettings& settings) override
    {
        const bool valid = settings.limit_count > 0 && settings.limit_count <= kMaxPageSize &&
            (settings.fuzzy_choices.empty() || settings.fuzzy_choices.size() == FUZZY_CHOICES);
        return invoke(token, request_id, valid, [&](SessionState& state, SessionResult& result) {
            if (state.settings && settings.revision < state.settings->revision)
            {
                result.status = SessionStatus::kProtocolError; result.result = false;
                result.message = "stale session settings revision"; return;
            }
            state.settings = settings;
            if (state.settings->fuzzy_choices.empty()) state.settings->fuzzy_choices.assign(FUZZY_CHOICES, 0);
        });
    }
private:
    struct SessionState
    {
        CompositionSession session;
        std::optional<SessionSettings> settings;
    };
    static SessionResult reply(SessionToken token, std::uint64_t request_id, SessionStatus status = SessionStatus::kOk)
    {
        SessionResult result;
        result.request_id = request_id; result.session_id = token;
        result.session_generation = 1; result.status = status;
        result.result = status == SessionStatus::kOk;
        return result;
    }
    static void activate_settings(const SessionState& state)
    {
        coral::runtime::refresh_runtime_settings();
        if (!state.settings) return;
        const auto& settings = *state.settings;
        coral::runtime::apply_runtime_settings(settings.u_mode, settings.v_mode, settings.pinyin_correction,
            settings.full_width, settings.traditional_chinese, settings.show_infos, settings.limit_count,
            settings.combined_lexicon_version, settings.fuzzy_choices.data(), static_cast<int>(settings.fuzzy_choices.size()), settings.smart_prediction);
    }
    static void identify_candidates(SessionResult& result)
    {
        for (std::size_t index = 0; index < result.output.candidates.candidates.size(); ++index)
        {
            auto& candidate = result.output.candidates.candidates[index];
            std::uint64_t hash = 14695981039346656037ULL;
            const auto number = [&](std::uint64_t value) {
                for (int byte = 0; byte < 8; ++byte) { hash ^= (value >> (8 * byte)) & 0xff; hash *= 1099511628211ULL; }
            };
            const auto text = [&](const std::string& value) {
                number(value.size());
                for (unsigned char byte : value) { hash ^= byte; hash *= 1099511628211ULL; }
            };
            number(result.session_id); number(static_cast<std::uint64_t>(result.output.candidates.offset) + index);
            text(candidate.word); text(candidate.pinyin);
            candidate.id = hash == 0 ? 1 : hash;
        }
    }
    template<class Function>
    SessionResult invoke(SessionToken token, std::uint64_t request_id, bool valid, Function function)
    {
        std::lock_guard<std::mutex> lock(runtime_gate);
        if (!current_runtime()) return reply(token, request_id, SessionStatus::kUnavailable);
        const auto found = sessions_.find(token);
        if (found == sessions_.end()) return reply(token, request_id, SessionStatus::kInvalidSession);
        if (!valid) return reply(token, request_id, SessionStatus::kProtocolError);
        auto result = reply(token, request_id);
        try
        {
            activate_settings(found->second); function(found->second, result); identify_candidates(result);
        }
        catch (...)
        {
            result = reply(token, request_id, SessionStatus::kInternalError);
            result.message = "session operation failed";
        }
        return result;
    }
    bool current_runtime() const
    {
        return runtime_generation_ != 0 && coral::runtime::runtime_ready() &&
            runtime_generation_ == coral::runtime::runtime_generation();
    }
    const std::uint64_t runtime_generation_;
    bool owns_runtime_ = false;
    std::unordered_map<SessionToken, SessionState> sessions_;
};
}

std::unique_ptr<SessionPort> make_local_session_port()
{
    std::lock_guard<std::mutex> lock(runtime_gate);
    return std::make_unique<LocalSessionPort>();
}

std::unique_ptr<SessionPort> make_initialized_session_port(const std::string& user_data_path, bool synchronous_load)
{
    std::lock_guard<std::mutex> lock(runtime_gate);
    if (coral::runtime::runtime_ready()) return nullptr;
    if (!coral::runtime::start_runtime(user_data_path.c_str(), synchronous_load)) return nullptr;
    try
    {
        return std::make_unique<LocalSessionPort>(true);
    }
    catch (...) { coral::runtime::stop_runtime(); throw; }
}
}

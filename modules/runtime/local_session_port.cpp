#include "coral/composition/session_port.h"
#if CORAL_ENABLE_CHARACTER_MODEL
#include "coral/language_model/character_rerank.h"
#endif
#include "coral/composition/composition_session.h"
#include "text_file_helpers.hpp"
#include "core_operations.h"
#include "runtime_context.h"
#include "mode_selection.h"
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace coral::composition
{
namespace
{
static_assert(FUZZY_CHOICES == kSessionFuzzyChoiceCount, "session fuzzy settings must match the legacy model");
// 解码器、模型缓存和解析 arena 仍在本二进制内共享，所有本地端口统一串行访问。
// 会话配置只覆盖当前请求线程；各端口的会话映射独立。
std::mutex runtime_gate;
SessionToken next_session_token = 1;

bool valid_text(const std::string& text)
{
    return text.size() <= kMaxInputBytes && text.find('\0') == std::string::npos;
}

class SessionSettingsScope {
public:
    explicit SessionSettingsScope(const std::optional<SessionSettings>& settings) {
        if (!settings) return;
        auto config = std::make_shared<coral::EngineConfig>();
        config->smart_prediction = settings->smart_prediction;
        config->isUMode = settings->u_mode;
        config->isVMode = settings->v_mode;
        config->pinyin_correction = settings->pinyin_correction;
        config->fuzzy_choices = settings->fuzzy_choices;
        config->limit_count = settings->limit_count;
        config->isShowInfos = settings->show_infos;
        config->combined_lexicon_version = settings->combined_lexicon_version;
        config_.emplace(coral::Engine::compatInstance().config(), std::move(config));
        mode_.emplace(settings->full_width, settings->traditional_chinese);
    }
private:
    std::optional<coral::ScopedConfigOverride> config_;
    std::optional<coral::ScopedInputMode> mode_;
};

class LocalSessionPort final : public SessionPort
{
public:
    explicit LocalSessionPort(bool owns_runtime = false)
        : owns_runtime_(owns_runtime), runtime_generation_(coral::runtime::runtime_ready() ? coral::runtime::runtime_generation() : 0) {}
    ~LocalSessionPort() override
    {
#if CORAL_ENABLE_CHARACTER_MODEL
        reranker_.stop();
#endif
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
            state.refinement.reset();
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
            state.refinement.reset();
#if CORAL_ENABLE_CHARACTER_MODEL
            language_model::CharacterRerankBatch batch;
            language_model::CharacterRerankScope capture(batch);
#endif
            state.session.set_page_size(page_size);
            result.output = state.session.query_page(condition, pinyin, offset, mode);
#if CORAL_ENABLE_CHARACTER_MODEL
            if (offset == 0 && !batch.empty() && !result.output.candidates.empty()) {
                auto next = std::make_unique<Refinement>();
                next->query_request_id = request_id; next->condition = condition; next->pinyin = pinyin;
                next->page_size = page_size; next->mode = mode; next->batch = std::move(batch);
                state.refinement = std::move(next);
                result.value = kRefinementPending;
            }
#endif
        });
    }
    SessionResult refine_candidates(SessionToken token, std::uint64_t request_id,
        std::uint64_t query_request_id, bool cancel) override
    {
        return invoke(token, request_id, query_request_id != 0, [&](SessionState& state, SessionResult& result) {
#if CORAL_ENABLE_CHARACTER_MODEL
            if (!state.refinement || state.refinement->query_request_id != query_request_id) return;
            if (cancel) { state.refinement.reset(); return; }
            auto& refinement = *state.refinement;
            if (!refinement.task) {
                refinement.task = reranker_.submit(std::move(refinement.batch));
                result.value = kRefinementPending;
                return;
            }
            auto task = refinement.task;
            language_model::CharacterRerankBatch completed;
            {
                std::lock_guard<std::mutex> lock(task->mutex);
                if (task->cancelled) { state.refinement.reset(); return; }
                if (!task->finished) { result.value = kRefinementPending; return; }
                if (!task->succeeded) { state.refinement.reset(); return; }
                completed = std::move(task->batch);
            }
            // 已完成的分数只用于完全一致的候选输入；重建阶段绝不调用神经计算。
            language_model::CharacterRerankScope replay(static_cast<const language_model::CharacterRerankBatch&>(completed));
            coral::runtime::clear_parse_cache();
            state.session.set_page_size(refinement.page_size);
            auto output = state.session.query_page(refinement.condition, refinement.pinyin, 0, refinement.mode);
            if (replay.replayed_all() && !output.candidates.empty()) {
                result.output = std::move(output); result.value = kRefinementReady;
            } else {
                language_model::CharacterRerankBatch ignored;
                language_model::CharacterRerankScope baseline(ignored);
                coral::runtime::clear_parse_cache();
                state.session.query_page(refinement.condition, refinement.pinyin, 0, refinement.mode);
            }
            state.refinement.reset();
#endif
        }, !cancel);
    }
    SessionResult select_candidate(SessionToken token, std::uint64_t request_id, int index) override
    {
        return invoke(token, request_id, index >= 0 && index < kMaxPageSize, [&](SessionState& state, SessionResult& result) {
            invalidate_refinements();
            result.output = state.session.select_index(index);
        });
    }
    SessionResult reset_session(SessionToken token, std::uint64_t request_id) override
    {
        return invoke(token, request_id, true, [](SessionState& state, SessionResult& result) {
            state.refinement.reset(); result.output = state.session.reset();
        });
    }
    SessionResult record_selection(SessionToken token, std::uint64_t request_id, const std::string& pinyin,
        const std::string& word, std::uint32_t condition_id) override
    {
        return invoke(token, request_id, valid_text(pinyin) && valid_text(word), [&](SessionState&, SessionResult& result) {
            invalidate_refinements();
            coral::runtime::remember_candidate(pinyin, word, condition_id); result.value = condition_id;
        });
    }
    SessionResult revert_selection(SessionToken token, std::uint64_t request_id, const std::string& word) override
    {
        return invoke(token, request_id, valid_text(word), [&](SessionState&, SessionResult& result) {
            invalidate_refinements();
            result.result = coral::runtime::revert_phrase_learning(word);
        });
    }
    SessionResult apply_candidate_action(SessionToken token, std::uint64_t request_id, const std::string& pinyin,
        const std::string& word, CandidateAction action, LanguageVariant variant) override
    {
        return invoke(token, request_id, valid_text(pinyin) && valid_text(word) && IsValidCandidateAction(action) && IsValidLanguageVariant(variant),
            [&](SessionState&, SessionResult& result) {
                invalidate_refinements(); result.result = coral::runtime::apply_candidate_rule(pinyin, word, action, variant);
            });
    }
    SessionResult clear_cache(SessionToken token, std::uint64_t request_id) override
    {
        return invoke(token, request_id, true, [&](SessionState&, SessionResult&) {
            invalidate_refinements(); coral::runtime::clear_parse_cache();
        });
    }
    SessionResult refresh_lexicon(SessionToken token, std::uint64_t request_id) override
    {
        return invoke(token, request_id, true, [&](SessionState&, SessionResult&) {
            invalidate_refinements(); coral::runtime::refresh_lexicon_state();
        });
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
            state.refinement.reset();
            if (state.settings->fuzzy_choices.empty()) state.settings->fuzzy_choices.assign(FUZZY_CHOICES, 0);
        }, false);
    }
private:
    struct Refinement {
#if CORAL_ENABLE_CHARACTER_MODEL
        std::uint64_t query_request_id = 0;
        std::string condition, pinyin;
        int page_size = 0, mode = 0;
        language_model::CharacterRerankBatch batch;
        std::shared_ptr<language_model::CharacterRerankTask> task;
        ~Refinement() { if (task) task->cancelled = true; }
#endif
    };
    struct SessionState
    {
        CompositionSession session;
        std::optional<SessionSettings> settings;
        std::unique_ptr<Refinement> refinement;
    };
    void invalidate_refinements() {
        for (auto& entry : sessions_) entry.second.refinement.reset();
    }
    static SessionResult reply(SessionToken token, std::uint64_t request_id, SessionStatus status = SessionStatus::kOk)
    {
        SessionResult result;
        result.request_id = request_id; result.session_id = token;
        result.session_generation = 1; result.status = status;
        result.result = status == SessionStatus::kOk;
        return result;
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
    SessionResult invoke(SessionToken token, std::uint64_t request_id, bool valid, Function function, bool activate = true)
    {
        std::lock_guard<std::mutex> lock(runtime_gate);
        if (!current_runtime()) return reply(token, request_id, SessionStatus::kUnavailable);
        const auto found = sessions_.find(token);
        if (found == sessions_.end()) return reply(token, request_id, SessionStatus::kInvalidSession);
        if (!valid) return reply(token, request_id, SessionStatus::kProtocolError);
        auto result = reply(token, request_id);
        try
        {
            std::optional<SessionSettingsScope> settings_scope;
            // 所有会话操作（包括翻页和选中后的再解析）均禁止内联神经推理。
#if CORAL_ENABLE_CHARACTER_MODEL
            language_model::CharacterRerankBatch ignored;
            language_model::CharacterRerankScope baseline(ignored);
#endif
            if (activate) {
                coral::runtime::refresh_runtime_settings();
                settings_scope.emplace(found->second.settings);
            }
            function(found->second, result);
            identify_candidates(result);
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
#if CORAL_ENABLE_CHARACTER_MODEL
    language_model::AsyncCharacterReranker reranker_{[] {
#if defined(_WIN32)
        // 模型让出 CPU 调度优先级，低配置机器也优先处理前台按键。
        ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    }};
#endif
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

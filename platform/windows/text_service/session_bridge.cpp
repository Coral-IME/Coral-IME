#include "session_bridge.h"
#include "coral/composition/composition_session.h"
#include <limits>
#include <utility>

namespace coral::text_service {
using composition::SessionStatus;
using composition::SessionToken;
using composition::SessionResult;
namespace {
constexpr std::uint32_t kInitialWaitMs = 2000;
constexpr std::uint32_t kRecoveryWaitMs = 50;

bool can_recover(SessionStatus status) {
    return status == SessionStatus::kUnavailable || status == SessionStatus::kInvalidSession;
}

template<class Number>
Number take_id(Number& next) {
    const auto value = next;
    if (value) next = value == std::numeric_limits<Number>::max() ? 0 : value + 1;
    return value;
}
}

SessionBridge::SessionBridge(PortFactory port_factory) : port_factory_(std::move(port_factory)) {}

HRESULT SessionBridge::to_hresult(SessionStatus status) {
    switch (status) {
    case SessionStatus::kOk: return S_OK;
    case SessionStatus::kInvalidSession: return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
    case SessionStatus::kUnavailable: return HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);
    case SessionStatus::kTimeout: return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    case SessionStatus::kProtocolError: return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    default: return E_FAIL;
    }
}

bool SessionBridge::connect(std::uint32_t wait_ms) {
    if (!port_factory_) return false;
    auto replacement = port_factory_(wait_ms);
    if (!replacement || !replacement->available()) return false;
    port_ = std::move(replacement);
    for (auto& entry : sessions_) {
        entry.second.remote_id = 0;
        entry.second.settings_synced = false;
    }
    return true;
}

HRESULT SessionBridge::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port_) return S_FALSE;
    return connect(kInitialWaitMs) ? S_OK : to_hresult(SessionStatus::kUnavailable);
}

std::uint64_t SessionBridge::take_request_id() { return take_id(next_request_id_); }

SessionStatus SessionBridge::prepare_session(SessionState& state) {
    if (!port_) return SessionStatus::kUnavailable;
    if (!state.remote_id) {
        state.remote_id = port_->open_session();
        state.settings_synced = false;
        if (!state.remote_id) return SessionStatus::kUnavailable;
    }
    if (state.settings && !state.settings_synced) {
        const auto request_id = take_request_id();
        if (!request_id) return SessionStatus::kInternalError;
        const auto result = port_->apply_settings(state.remote_id, request_id, *state.settings);
        if (!result) return result.status;
        state.settings_synced = true;
    }
    return SessionStatus::kOk;
}

SessionToken SessionBridge::create_session() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!next_session_id_ || (!port_ && !connect(kInitialWaitMs))) return 0;
    auto remote_id = port_->open_session();
    if (!remote_id && connect(kRecoveryWaitMs)) remote_id = port_->open_session();
    if (!remote_id) return 0;
    const auto logical_id = take_id(next_session_id_);
    try { sessions_.emplace(logical_id, SessionState{ remote_id }); }
    catch (...) { port_->close_session(remote_id); throw; }
    return logical_id;
}

void SessionBridge::destroy_session(SessionToken session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(session_id);
    if (found == sessions_.end()) return;
    if (port_ && found->second.remote_id) port_->close_session(found->second.remote_id);
    sessions_.erase(found);
}

SessionResult SessionBridge::execute(SessionToken session_id, ReplayPolicy policy, const Operation& operation) {
    SessionResult result;
    const auto found = sessions_.find(session_id);
    if (found == sessions_.end()) { result.status = SessionStatus::kInvalidSession; return result; }
    auto& state = found->second;
    bool recovered = false;
    auto status = prepare_session(state);
    if (can_recover(status) && connect(kRecoveryWaitMs)) {
        recovered = true;
        status = prepare_session(state);
    }
    if (status != SessionStatus::kOk) { result.status = status; return result; }
    const auto invoke = [&] {
        const auto request_id = take_request_id();
        if (!request_id) { SessionResult failure; failure.status = SessionStatus::kInternalError; return failure; }
        return operation(*port_, state.remote_id, request_id);
    };
    result = invoke();
    const bool replay = result.status == SessionStatus::kInvalidSession ||
        (policy == ReplayPolicy::kRead && result.status == SessionStatus::kUnavailable);
    if (!recovered && replay && connect(kRecoveryWaitMs)) {
        status = prepare_session(state);
        if (status != SessionStatus::kOk) { result.status = status; return result; }
        result = invoke();
    }
    if (result.status == SessionStatus::kUnavailable && policy == ReplayPolicy::kMutation) {
        port_.reset();
        for (auto& entry : sessions_) {
            entry.second.remote_id = 0;
            entry.second.settings_synced = false;
        }
    }
    // 失败的变更可能已到达服务端。重连被推迟到下一次调用，
    // 且绝不重放本次调用。
    return result;
}

HRESULT SessionBridge::query(const CandidateLookup& lookup, CandidateSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto result = execute(lookup.session_id, ReplayPolicy::kRead,
        [&](composition::SessionPort& port, SessionToken remote_id, std::uint64_t request_id) {
            return port.query_page(remote_id, request_id, lookup.condition, lookup.pinyin,
                lookup.offset, lookup.page_size, lookup.mode);
        });
    if (!result) return to_hresult(result.status);
    CandidateSnapshot replacement;
    replacement.request_id = lookup.request_id; // UI 版本，与传输层标识不同。
    replacement.offset = result.output.candidates.offset;
    replacement.has_more = result.output.candidates.has_next_page;
    for (const auto& candidate : result.output.candidates.candidates) {
        replacement.words.push_back(candidate.word);
        replacement.pinyins.push_back(candidate.pinyin);
        replacement.display_texts.push_back(candidate.display_text);
    }
    snapshot = std::move(replacement);
    return S_OK;
}

HRESULT SessionBridge::reset(SessionToken session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return to_hresult(execute(session_id, ReplayPolicy::kMutation,
        [](composition::SessionPort& port, SessionToken remote_id, std::uint64_t request_id) {
            return port.reset_session(remote_id, request_id);
        }).status);
}

HRESULT SessionBridge::remember_candidate(SessionToken session_id, const std::string& pinyin,
    const std::string& word, std::uint32_t& condition_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto result = execute(session_id, ReplayPolicy::kMutation,
        [&](composition::SessionPort& port, SessionToken remote_id, std::uint64_t request_id) {
            return port.record_selection(remote_id, request_id, pinyin, word, condition_id);
        });
    if (result) condition_id = result.value;
    return to_hresult(result.status);
}

bool SessionBridge::revert_last_action(SessionToken session_id, const std::string& word) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto result = execute(session_id, ReplayPolicy::kMutation,
        [&](composition::SessionPort& port, SessionToken remote_id, std::uint64_t request_id) {
            return port.revert_selection(remote_id, request_id, word);
        });
    return result && result.result;
}

bool SessionBridge::apply_candidate_rule(SessionToken session_id, const std::string& pinyin,
    const std::string& word, CandidateAction action, LanguageVariant variant) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto result = execute(session_id, ReplayPolicy::kMutation,
        [&](composition::SessionPort& port, SessionToken remote_id, std::uint64_t request_id) {
            return port.apply_candidate_action(remote_id, request_id, pinyin, word, action, variant);
        });
    return result && result.result;
}

HRESULT SessionBridge::clear_cache(SessionToken session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return to_hresult(execute(session_id, ReplayPolicy::kMutation,
        [](composition::SessionPort& port, SessionToken remote_id, std::uint64_t request_id) {
            return port.clear_cache(remote_id, request_id);
        }).status);
}

HRESULT SessionBridge::refresh_lexicon(SessionToken session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return to_hresult(execute(session_id, ReplayPolicy::kMutation,
        [](composition::SessionPort& port, SessionToken remote_id, std::uint64_t request_id) {
            return port.refresh_lexicon(remote_id, request_id);
        }).status);
}

HRESULT SessionBridge::apply_settings(SessionToken session_id, const composition::SessionSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(session_id);
    if (found == sessions_.end()) return to_hresult(SessionStatus::kInvalidSession);
    auto& state = found->second;
    if (settings.limit_count <= 0 || settings.limit_count > composition::kMaxPageSize ||
        (!settings.fuzzy_choices.empty() && settings.fuzzy_choices.size() != composition::kSessionFuzzyChoiceCount) ||
        (state.settings && settings.revision < state.settings->revision))
        return to_hresult(SessionStatus::kProtocolError);
    if (state.settings && state.settings_synced && state.settings->revision == settings.revision &&
        composition::same_session_settings(*state.settings, settings)) return S_OK;
    // 发送前先分配新缓存；失败不得回滚已接受的修订。
    auto accepted = std::make_optional(settings);
    const auto result = execute(session_id, ReplayPolicy::kMutation,
        [&](composition::SessionPort& port, SessionToken remote_id, std::uint64_t request_id) {
            return port.apply_settings(remote_id, request_id, settings);
        });
    if (result) {
        state.settings = std::move(accepted);
        state.settings_synced = true;
    } else if (result.status == SessionStatus::kTimeout || result.status == SessionStatus::kProtocolError) {
        // 缺失/无效的回复无法告知远端接受了哪些设置。
        // 退役该远端会话；在新会话上恢复最后确认的设置。
        if (port_ && state.remote_id) port_->close_session(state.remote_id);
        state.remote_id = 0;
        state.settings_synced = false;
    }
    return to_hresult(result.status);
}
} // 命名空间 coral::text_service 结束

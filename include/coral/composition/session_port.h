#ifndef CORAL_INCLUDE_CORAL_COMPOSITION_SESSION_PORT_H_INCLUDED
#define CORAL_INCLUDE_CORAL_COMPOSITION_SESSION_PORT_H_INCLUDED

#include "composition_types.h"
#include "coral/vocabulary/lexicon_types.h"

#include <cstdint>
#include <memory>
#include <string>

namespace coral::composition {

using SessionToken = std::uint64_t;
constexpr std::size_t kSessionFuzzyChoiceCount = 19;
constexpr std::uint32_t kRefinementPending = 1;
constexpr std::uint32_t kRefinementReady = 2;

enum class SessionStatus : std::uint32_t {
    kOk = 0,
    kInvalidSession = 1,
    kUnavailable = 2,
    kTimeout = 3,
    kProtocolError = 4,
    kInternalError = 5,
};

struct SessionResult {
    SessionStatus status = SessionStatus::kOk;
    std::uint64_t request_id = 0;
    SessionToken session_id = 0;
    std::uint64_t session_generation = 0;
    std::uint64_t server_generation = 0;
    Output output;
    bool result = false;
    std::uint32_t value = 0;
    std::string message;

    explicit operator bool() const { return status == SessionStatus::kOk; }
};

struct SessionSettings {
    std::uint64_t revision = 0;
    bool smart_prediction = true;
    bool u_mode = true;
    bool v_mode = true;
    bool pinyin_correction = true;
    bool full_width = false;
    bool traditional_chinese = false;
    bool show_infos = false;
    int limit_count = 10;
    int combined_lexicon_version = -1;
    std::vector<int> fuzzy_choices;
};

inline bool same_session_settings(const SessionSettings& a, const SessionSettings& b) {
    return a.smart_prediction == b.smart_prediction && a.u_mode == b.u_mode && a.v_mode == b.v_mode &&
           a.pinyin_correction == b.pinyin_correction &&
           a.full_width == b.full_width &&
           a.traditional_chinese == b.traditional_chinese &&
           a.show_infos == b.show_infos && a.limit_count == b.limit_count &&
           a.combined_lexicon_version == b.combined_lexicon_version &&
           a.fuzzy_choices == b.fuzzy_choices;
}

// 稳定的本地/远端边界。在远端引擎成为默认之前，
// 平台代码应迁移到此契约。
class SessionPort {
public:
    virtual ~SessionPort() = default;
    virtual SessionToken open_session() = 0;
    virtual void close_session(SessionToken session_id) = 0;
    virtual SessionResult send_key(SessionToken session_id, std::uint64_t request_id,
                                   const KeyEvent& event, const InputContext& context) = 0;
    virtual SessionResult query_page(SessionToken session_id, std::uint64_t request_id,
                                     const std::string& condition, const std::string& pinyin,
                                     int offset, int page_size, int mode) = 0;
    virtual SessionResult select_candidate(SessionToken session_id, std::uint64_t request_id,
                                           int index_in_page) = 0;
    // 只轮询指定基础查询的优化；默认端口不提供此能力，绝不退回同步推理。
    virtual SessionResult refine_candidates(SessionToken session_id, std::uint64_t request_id,
        std::uint64_t query_request_id, bool cancel = false) {
        SessionResult result; result.session_id = session_id; result.request_id = request_id;
        result.result = true; return result;
    }
    virtual SessionResult reset_session(SessionToken session_id, std::uint64_t request_id) = 0;
    virtual SessionResult record_selection(SessionToken session_id, std::uint64_t request_id,
                                          const std::string& pinyin, const std::string& word,
                                          std::uint32_t condition_id) = 0;
    virtual SessionResult revert_selection(SessionToken session_id, std::uint64_t request_id,
                                            const std::string& word) = 0;
    virtual SessionResult apply_candidate_action(SessionToken session_id, std::uint64_t request_id,
                                             const std::string& pinyin, const std::string& word,
                                             CandidateAction action, LanguageVariant variant) = 0;
    virtual SessionResult clear_cache(SessionToken session_id, std::uint64_t request_id) = 0;
    virtual SessionResult refresh_lexicon(SessionToken session_id, std::uint64_t request_id) = 0;
    virtual SessionResult apply_settings(SessionToken session_id, std::uint64_t request_id,
                                        const SessionSettings& config) = 0;
    virtual bool flush_history() = 0;
    virtual bool available() const = 0;
};

std::unique_ptr<SessionPort> make_local_session_port();
std::unique_ptr<SessionPort> make_initialized_session_port(
    const std::string& user_data_path, bool synchronous_load);

}  // 命名空间 coral::composition 结束

#endif // CORAL_INCLUDE_CORAL_COMPOSITION_SESSION_PORT_H_INCLUDED

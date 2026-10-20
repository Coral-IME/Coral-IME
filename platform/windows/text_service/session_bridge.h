#pragma once

#include "coral/composition/session_port.h"
#include <windows.h>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace coral::text_service {

struct CandidateLookup {
    composition::SessionToken session_id = 0;
    std::uint64_t request_id = 0;
    std::string condition;
    std::string pinyin;
    int offset = 0;
    int page_size = 5;
    int mode = composition::kModeQuanpin;
};

struct CandidateSnapshot {
    std::uint64_t request_id = 0;
    int offset = 0;
    bool has_more = false;
    std::vector<std::string> words;
    std::vector<std::string> pinyins;
    std::vector<std::string> display_texts;
};

class CandidateProvider {
public:
    virtual ~CandidateProvider() = default;
    virtual HRESULT query(const CandidateLookup& lookup, CandidateSnapshot& snapshot) = 0;
    virtual HRESULT remember_candidate(composition::SessionToken session_id, const std::string& pinyin,
        const std::string& word, std::uint32_t& condition_id) = 0;
    virtual bool apply_candidate_rule(composition::SessionToken session_id, const std::string& pinyin,
        const std::string& word, CandidateAction action, LanguageVariant variant) = 0;
    virtual HRESULT clear_cache(composition::SessionToken session_id) = 0;
};

// 工厂执行平台启动/连接工作。它不得回调
// 本桥接。逻辑令牌属于本对象；远端令牌不属于。
class SessionBridge final : public CandidateProvider {
public:
    using PortFactory = std::function<std::unique_ptr<composition::SessionPort>(std::uint32_t wait_ms)>;
    explicit SessionBridge(PortFactory port_factory);
    HRESULT start();
    composition::SessionToken create_session();
    void destroy_session(composition::SessionToken session_id);
    HRESULT query(const CandidateLookup& lookup, CandidateSnapshot& snapshot) override;
    HRESULT reset(composition::SessionToken session_id);
    HRESULT remember_candidate(composition::SessionToken session_id, const std::string& pinyin,
        const std::string& word, std::uint32_t& condition_id) override;
    bool revert_last_action(composition::SessionToken session_id, const std::string& word);
    bool apply_candidate_rule(composition::SessionToken session_id, const std::string& pinyin,
        const std::string& word, CandidateAction action, LanguageVariant variant) override;
    HRESULT clear_cache(composition::SessionToken session_id) override;
    HRESULT refresh_lexicon(composition::SessionToken session_id);
    HRESULT apply_settings(composition::SessionToken session_id, const composition::SessionSettings& settings);

private:
    struct SessionState {
        composition::SessionToken remote_id = 0;
        bool settings_synced = false;
        std::optional<composition::SessionSettings> settings;
    };
    enum class ReplayPolicy { kRead, kMutation };
    using Operation = std::function<composition::SessionResult(composition::SessionPort&,
        composition::SessionToken, std::uint64_t)>;
    static HRESULT to_hresult(composition::SessionStatus status);
    bool connect(std::uint32_t wait_ms);
    composition::SessionStatus prepare_session(SessionState& state);
    composition::SessionResult execute(composition::SessionToken session_id,
        ReplayPolicy policy, const Operation& operation);
    std::uint64_t take_request_id();

    std::mutex mutex_;
    PortFactory port_factory_;
    std::unique_ptr<composition::SessionPort> port_;
    std::unordered_map<composition::SessionToken, SessionState> sessions_;
    composition::SessionToken next_session_id_ = 1;
    std::uint64_t next_request_id_ = 1;
};

// 由 TSF 集成目标实现，与桥接库分离。
std::shared_ptr<SessionBridge> shared_session_bridge();

} // 命名空间 coral::text_service 结束

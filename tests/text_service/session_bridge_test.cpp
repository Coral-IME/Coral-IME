#include "session_bridge.h"
#include <deque>
#include <iostream>
#include <stdexcept>

namespace {
using namespace coral::composition;
using namespace coral::text_service;
using coral::CandidateAction;
using coral::LanguageVariant;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Script {
    std::deque<SessionStatus> replies;
    std::vector<SessionSettings> settings;
    std::vector<std::string> calls;
    std::vector<std::uint64_t> request_ids;
    int opens = 0;
    int closes = 0;
    int destroyed = 0;
    bool healthy = true;
    bool throw_query = false;
};

class ScriptPort final : public SessionPort {
public:
    explicit ScriptPort(std::shared_ptr<Script> script) : script_(std::move(script)) {}
    ~ScriptPort() override { ++script_->destroyed; }
    SessionToken open_session() override { return static_cast<SessionToken>(++script_->opens); }
    void close_session(SessionToken) override { ++script_->closes; }
    bool available() const override { return script_->healthy; }
    bool flush_history() override { return true; }
    SessionResult send_key(SessionToken, std::uint64_t id, const KeyEvent&, const InputContext&) override { return reply("key", id); }
    SessionResult query_page(SessionToken, std::uint64_t id, const std::string&, const std::string&, int offset, int, int) override {
        if (script_->throw_query) throw std::runtime_error("artificial query failure");
        auto result = reply("query", id);
        result.output.candidates.offset = offset;
        result.output.candidates.has_next_page = true;
        Candidate candidate;
        candidate.word = "sample"; candidate.pinyin = "pin"; candidate.display_text = "display";
        result.output.candidates.candidates.push_back(candidate);
        return result;
    }
    SessionResult select_candidate(SessionToken, std::uint64_t id, int) override { return reply("select", id); }
    SessionResult reset_session(SessionToken, std::uint64_t id) override { return reply("reset", id); }
    SessionResult record_selection(SessionToken, std::uint64_t id, const std::string&, const std::string&, std::uint32_t) override { return reply("remember", id); }
    SessionResult revert_selection(SessionToken, std::uint64_t id, const std::string&) override { return reply("revert", id); }
    SessionResult apply_candidate_action(SessionToken, std::uint64_t id, const std::string&, const std::string&, CandidateAction, LanguageVariant) override { return reply("rule", id); }
    SessionResult clear_cache(SessionToken, std::uint64_t id) override { return reply("clear", id); }
    SessionResult refresh_lexicon(SessionToken, std::uint64_t id) override { return reply("refresh", id); }
    SessionResult apply_settings(SessionToken, std::uint64_t id, const SessionSettings& settings) override {
        script_->settings.push_back(settings); return reply("settings", id);
    }
private:
    SessionResult reply(const std::string& name, std::uint64_t id) {
        script_->calls.push_back(name); script_->request_ids.push_back(id);
        SessionResult result; result.request_id = id; result.result = true; result.value = 123;
        if (!script_->replies.empty()) { result.status = script_->replies.front(); script_->replies.pop_front(); }
        return result;
    }
    std::shared_ptr<Script> script_;
};

struct Fixture {
    std::vector<std::shared_ptr<Script>> scripts{ std::make_shared<Script>(), std::make_shared<Script>() };
    std::vector<std::uint32_t> waits;
    SessionBridge bridge{ [&](std::uint32_t wait_ms) -> std::unique_ptr<SessionPort> {
        waits.push_back(wait_ms);
        if (waits.size() > scripts.size()) return {};
        return std::make_unique<ScriptPort>(scripts[waits.size() - 1]);
    } };
    CandidateLookup lookup;
    Fixture() { lookup.session_id = bridge.create_session(); lookup.request_id = 987; require(lookup.session_id != 0, "logical session opened"); }
};

void revisions() {
    Fixture f;
    SessionSettings settings; settings.revision = 10;
    require(f.bridge.apply_settings(f.lookup.session_id, settings) == S_OK, "first settings");
    settings.revision = 11;
    require(f.bridge.apply_settings(f.lookup.session_id, settings) == S_OK, "new revision with equal values");
    require(f.scripts[0]->settings.size() == 2 && f.scripts[0]->settings.back().revision == 11, "equal values still advance remote revision");
    settings.revision = 9;
    require(f.bridge.apply_settings(f.lookup.session_id, settings) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "stale revision refused");
    settings.revision = 12; settings.fuzzy_choices = { 1 };
    require(FAILED(f.bridge.apply_settings(f.lookup.session_id, settings)), "wrong fuzzy count refused");
    settings.fuzzy_choices.clear(); settings.limit_count = 0;
    require(FAILED(f.bridge.apply_settings(f.lookup.session_id, settings)), "bad candidate limit refused");
    require(f.waits.size() == 1 && f.scripts[0]->settings.size() == 2, "invalid settings never reach port or reconnect");
    settings.limit_count = 10;
    f.scripts[0]->replies = { SessionStatus::kProtocolError };
    require(FAILED(f.bridge.apply_settings(f.lookup.session_id, settings)), "remote refusal propagated");
    CandidateSnapshot snapshot;
    require(f.bridge.query(f.lookup, snapshot) == S_OK, "query after uncertain settings reply");
    require(f.waits.size() == 1 && f.scripts[0]->settings.back().revision == 11, "new remote restores confirmed revision without reconnecting on protocol error");
}

void recovery_and_snapshot() {
    Fixture f;
    const auto second = f.bridge.create_session();
    SessionSettings first; first.revision = 3; first.traditional_chinese = true;
    SessionSettings other; other.revision = 7;
    require(SUCCEEDED(f.bridge.apply_settings(f.lookup.session_id, first)) && SUCCEEDED(f.bridge.apply_settings(second, other)), "two configurations");
    f.scripts[0]->replies = { SessionStatus::kInvalidSession };
    CandidateSnapshot snapshot;
    require(f.bridge.query(f.lookup, snapshot) == S_OK, "invalid token recovered");
    require(snapshot.request_id == 987 && snapshot.words == std::vector<std::string>{"sample"} && snapshot.has_more, "atomic snapshot and UI identity");
    require(f.waits == std::vector<std::uint32_t>{2000, 50} && f.scripts[0]->destroyed == 1, "bounded recovery and old port released");
    require(f.scripts[1]->settings.size() == 1 && f.scripts[1]->settings[0].traditional_chinese, "requested logical session restored");
    f.lookup.session_id = second;
    require(f.bridge.query(f.lookup, snapshot) == S_OK && f.scripts[1]->settings.back().revision == 7, "other session restored lazily");
    f.scripts[1]->replies = { SessionStatus::kTimeout };
    require(f.bridge.query(f.lookup, snapshot) == HRESULT_FROM_WIN32(ERROR_TIMEOUT) && snapshot.words[0] == "sample", "failure preserves snapshot");
    f.scripts[1]->throw_query = true;
    bool threw = false;
    try { f.bridge.query(f.lookup, snapshot); }
    catch (const std::runtime_error&) { threw = true; }
    require(threw, "port exception reaches caller");
    require(snapshot.words[0] == "sample", "port exception preserves snapshot");
    f.scripts[1]->throw_query = false;
    f.bridge.destroy_session(second);
    require(f.bridge.query(f.lookup, snapshot) == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE), "closed logical token rejected");
    require(f.waits.size() == 2, "closed session never causes reconnect");
    for (const auto& script : f.scripts) for (auto id : script->request_ids) require(id != 0, "wire request IDs nonzero");
    require(f.scripts[1]->request_ids.front() > f.scripts[0]->request_ids.back(), "request identity advances across recovery");
}

void retry_policy() {
    for (const auto status : {SessionStatus::kUnavailable, SessionStatus::kInvalidSession, SessionStatus::kTimeout, SessionStatus::kProtocolError}) {
        Fixture f; f.scripts[0]->replies = { status }; CandidateSnapshot snapshot;
        const auto hr = f.bridge.query(f.lookup, snapshot);
        const bool recover = status == SessionStatus::kUnavailable || status == SessionStatus::kInvalidSession;
        require((hr == S_OK) == recover && f.waits.size() == (recover ? 2 : 1), "read retry classification");
        Fixture twice; twice.scripts[0]->replies = { status }; twice.scripts[1]->replies = { status };
        require(FAILED(twice.bridge.query(twice.lookup, snapshot)) && twice.waits.size() <= 2, "read only retries once");
        for (int operation = 0; operation < 7; ++operation) {
            Fixture m; m.scripts[0]->replies = { status };
            std::uint32_t condition = 17; HRESULT result = E_FAIL;
            switch (operation) {
            case 0: result = m.bridge.reset(m.lookup.session_id); break;
            case 1: result = m.bridge.clear_cache(m.lookup.session_id); break;
            case 2: result = m.bridge.refresh_lexicon(m.lookup.session_id); break;
            case 3: result = m.bridge.remember_candidate(m.lookup.session_id,"pin","word",condition); break;
            case 4: result = m.bridge.revert_last_action(m.lookup.session_id,"word") ? S_OK : E_FAIL; break;
            case 5: result = m.bridge.apply_candidate_rule(m.lookup.session_id,"pin","word",CandidateAction::Disable,LanguageVariant::Simplified) ? S_OK : E_FAIL; break;
            case 6: result = m.bridge.apply_settings(m.lookup.session_id, SessionSettings{}); break;
            }
            const bool replay = status == SessionStatus::kInvalidSession;
            require((result == S_OK) == replay && m.waits.size() == (replay ? 2 : 1), "mutations replay only explicit invalid-session refusal");
            if (!replay && operation == 3) require(condition == 17, "failed learning preserves condition ID");
            if (status == SessionStatus::kUnavailable) {
                require(m.scripts[0]->destroyed == 1, "failed transport retired without replay");
                require(m.bridge.query(m.lookup, snapshot) == S_OK && m.waits.size() == 2, "next call can reconnect");
                require(m.scripts[1]->calls == std::vector<std::string>{"query"}, "no deferred mutation replay");
            }
        }
    }
}

void failed_recovery() {
    Fixture f;
    SessionSettings settings; settings.revision = 4;
    require(f.bridge.apply_settings(f.lookup.session_id, settings) == S_OK, "initial settings");
    f.scripts[0]->replies = {SessionStatus::kInvalidSession};
    f.scripts[1]->replies = {SessionStatus::kProtocolError};
    CandidateSnapshot snapshot; snapshot.words = {"kept"};
    require(f.bridge.query(f.lookup, snapshot) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "settings restoration error propagated precisely");
    require(f.scripts[1]->calls == std::vector<std::string>{"settings"} && snapshot.words[0] == "kept", "failed restore stops before query");
    f.scripts[1]->healthy = false;
    Fixture unavailable; unavailable.scripts[1]->healthy = false;
    unavailable.scripts[0]->replies = {SessionStatus::kUnavailable};
    require(FAILED(unavailable.bridge.query(unavailable.lookup, snapshot)), "unhealthy replacement refused");
    require(unavailable.scripts[0]->destroyed == 0 && unavailable.scripts[1]->destroyed == 1, "failed replacement keeps original owner");
}

void uncertain_settings_and_budget() {
    for (const auto status : {SessionStatus::kUnavailable, SessionStatus::kTimeout}) {
        Fixture f;
        SessionSettings accepted; accepted.revision = 10;
        require(f.bridge.apply_settings(f.lookup.session_id, accepted) == S_OK, "confirmed configuration");
        auto newer = accepted; newer.revision = 11; newer.traditional_chinese = true;
        f.scripts[0]->replies = {status};
        require(FAILED(f.bridge.apply_settings(f.lookup.session_id, newer)), "uncertain update fails");
        require(f.waits.size() == 1, "uncertain settings never immediately reconnect");
        CandidateSnapshot snapshot;
        require(f.bridge.query(f.lookup, snapshot) == S_OK, "query after uncertain configuration");
        const auto active = f.scripts[status == SessionStatus::kUnavailable ? 1 : 0];
        require(active->settings.back().revision == 10 && !active->settings.back().traditional_chinese,
            "restore only the last confirmed settings");
    }
    Fixture f;
    SessionSettings settings; settings.revision = 1;
    require(f.bridge.apply_settings(f.lookup.session_id, settings) == S_OK, "budget confirmed settings");
    settings.revision = 2; f.scripts[0]->replies = {SessionStatus::kTimeout};
    require(FAILED(f.bridge.apply_settings(f.lookup.session_id, settings)), "retire uncertain remote");
    f.scripts[0]->replies = {SessionStatus::kInvalidSession};
    f.scripts[1]->replies = {SessionStatus::kOk, SessionStatus::kInvalidSession};
    CandidateSnapshot snapshot; snapshot.words = {"kept"};
    require(f.bridge.query(f.lookup, snapshot) == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE), "business failure after preparatory recovery");
    require(f.waits.size() == 2 && f.scripts[1]->calls == std::vector<std::string>{"settings", "query"},
        "preparation and business share one recovery budget");
    require(snapshot.words[0] == "kept", "budget failure preserves snapshot");
}
}

int main() {
    try { revisions(); recovery_and_snapshot(); retry_policy(); failed_recovery(); uncertain_settings_and_budget(); std::cout << "session bridge boundaries passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

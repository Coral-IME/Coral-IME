#include "pch.h"
#include "core_operations.h"
#include "coral/composition/session_port.h"
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace coral::composition;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    if (argc != 3 || !std::getenv("CORAL_TEST_NAMESPACE") || !std::getenv("CORAL_USER_DATA_DIR")) return 2;
    try
    {
        const auto runtime = std::filesystem::absolute(argv[1]);
        const auto user = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(user / "config");
        std::filesystem::copy_file(runtime / "config/coralpinyinConfig.ini", user / "config/coralpinyinConfig.ini",
            std::filesystem::copy_options::overwrite_existing);
        auto owner = make_initialized_session_port(user.string(), true);
        require(owner && owner->available(), "owned runtime initialized");
        auto second = make_local_session_port();
        const auto first_id = owner->open_session(), second_id = second->open_session();
        require(first_id != 0 && second_id != 0 && first_id != second_id, "tokens are unique across ports");
        require(!make_initialized_session_port(user.string(), true) && owner->available(), "duplicate init preserves active runtime");
        SessionSettings simplified; simplified.revision = 2; simplified.pinyin_correction = false;
        SessionSettings traditional = simplified; traditional.traditional_chinese = true;
        require(static_cast<bool>(owner->apply_settings(first_id, 1, simplified)), "first settings applied");
        require(static_cast<bool>(second->apply_settings(second_id, 1, traditional)), "second settings applied");
        auto check = [](SessionPort& port, SessionToken token, const std::string& expected) {
            for (std::uint64_t request = 2; request < 22; ++request)
            {
                const auto result = port.query_page(token, request, "", "zhongguo", 0, 7, 0);
                require(static_cast<bool>(result) && !result.output.candidates.empty(), "concurrent query succeeds");
                require(result.output.candidates.candidates[0].word == expected, "per-session configuration is isolated");
                require(result.request_id == request && result.session_id == token, "query correlation retained");
            }
        };
        auto first = std::async(std::launch::async, check, std::ref(*owner), first_id, u8"中国");
        auto other = std::async(std::launch::async, check, std::ref(*second), second_id, u8"中國");
        first.get(); other.get();
        const auto stable = owner->query_page(first_id, 30, "", "nihao", 0, 7, 0);
        const auto repeated = owner->query_page(first_id, 31, "", "nihao", 0, 7, 0);
        require(stable.output.candidates.candidates[0].id == repeated.output.candidates.candidates[0].id, "candidate identity is stable");
        require(owner->query_page(first_id, 32, "", "nihao", -1, 7, 0).status == SessionStatus::kProtocolError, "negative offset rejected");
        require(owner->query_page(first_id, 32, "", "nihao", std::numeric_limits<int>::max(), 7, 0).status == SessionStatus::kProtocolError, "offset overflow rejected");
        require(owner->query_page(first_id, 32, "", std::string("a\0b",3), 0, 7, 0).status == SessionStatus::kProtocolError, "C-string truncation rejected");
        simplified.revision = 1;
        require(owner->apply_settings(first_id, 33, simplified).status == SessionStatus::kProtocolError, "stale revision rejected");
        simplified.revision = 3; simplified.limit_count = 0;
        require(owner->apply_settings(first_id, 34, simplified).status == SessionStatus::kProtocolError, "invalid settings rejected");
        simplified.limit_count = 10; simplified.fuzzy_choices.resize(3);
        require(owner->apply_settings(first_id, 34, simplified).status == SessionStatus::kProtocolError, "unsupported fuzzy vector rejected before parser");
        owner->close_session(first_id); owner->close_session(first_id);
        require(owner->reset_session(first_id, 35).status == SessionStatus::kInvalidSession, "closed session is invalid");
        owner.reset();
        require(!second->available() && !coral::runtime::runtime_ready(), "owned destruction drains runtime");
        require(second->query_page(second_id, 36, "", "nihao", 0, 7, 0).status == SessionStatus::kUnavailable, "borrowed port detects stop");
        auto reopened = make_initialized_session_port(user.string(), true);
        require(reopened && reopened->available(), "owned runtime can reopen");
        require(!second->available() && second->open_session() == 0, "old borrowed port remains unavailable");
        require(second->query_page(second_id, 37, "", "nihao", 0, 7, 0).status == SessionStatus::kUnavailable, "old session cannot revive");
        const auto reopened_id = reopened->open_session();
        require(reopened_id != 0 && reopened_id != second_id, "new runtime has fresh token");
        require(static_cast<bool>(reopened->query_page(reopened_id, 38, "", "nihao", 0, 7, 0)), "reopened real query succeeds");
        reopened.reset();
        second.reset();
        std::cout << "Local sessions, concurrency and owned shutdown passed\n";
        return 0;
    }
    catch (const std::exception& error) { coral::runtime::stop_runtime(); std::cerr << error.what() << '\n'; return 1; }
}

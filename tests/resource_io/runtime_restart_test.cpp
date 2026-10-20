#include "pch.h"
#include "core_operations.h"
#include "runtime_context.h"
#include "coral/composition/session_port.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace coral::composition;

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

std::unique_ptr<SessionPort> open_runtime(const std::filesystem::path& runtime, const std::filesystem::path& user, bool sync = true)
{
    require(_putenv_s("CORAL_LANGENGINE_DATA_DIR", runtime.string().c_str()) == 0, "set isolated runtime");
    return make_initialized_session_port(user.string(), sync);
}

void check_candidates(SessionPort& port, const std::string& greeting, const std::string& country, bool u_mode)
{
    const auto token = port.open_session();
    require(token != 0, "new session opened");
    require(coral::Engine::compatInstance().config().isUMode() == u_mode, "runtime configuration rebound");
    SessionSettings settings;
    settings.pinyin_correction = false;
    settings.u_mode = u_mode;
    require(static_cast<bool>(port.apply_settings(token, 1, settings)), "simplified settings");
    auto result = port.query_page(token, 2, "", "nihao", 0, 7, 0);
    require(static_cast<bool>(result) && !result.output.candidates.empty(), "real greeting query");
    if (result.output.candidates.candidates[0].word != greeting)
    {
        std::cerr << "Greeting expected=" << greeting << " returned=";
        for (const auto& candidate : result.output.candidates.candidates) std::cerr << candidate.word << '|';
        std::cerr << '\n';
        throw std::runtime_error("new primary data actually used");
    }
    settings.traditional_chinese = true;
    require(static_cast<bool>(port.apply_settings(token, 3, settings)), "traditional settings");
    result = port.query_page(token, 4, "", "zhongguo", 0, 7, 0);
    require(static_cast<bool>(result) && !result.output.candidates.empty(), "real conversion query");
    require(result.output.candidates.candidates[0].word == country, "new OpenCC directory actually used");
    port.close_session(token);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 5 || !std::getenv("CORAL_TEST_NAMESPACE") || !std::getenv("CORAL_USER_DATA_DIR")) return 2;
    try
    {
        const auto first = std::filesystem::absolute(argv[1]);
        const auto second = std::filesystem::absolute(argv[2]);
        const auto invalid = std::filesystem::absolute(argv[3]);
        const auto user = std::filesystem::absolute(argv[4]);
        auto cold_borrower = make_local_session_port();
        auto owner = open_runtime(first, user / "first");
        require(owner && owner->available(), "first runtime initialized");
        const auto first_generation = coral::runtime::runtime_generation();
        check_candidates(*owner, u8"你好", u8"中國", true);
        require(!cold_borrower->available() && cold_borrower->open_session() == 0, "cold borrower cannot attach later");
        auto old_borrower = make_local_session_port();
        const auto old_token = old_borrower->open_session();
        require(old_token != 0, "old borrower opened");
        require(!coral::runtime::start_runtime((user / "second").string().c_str(), true), "duplicate direct startup rejected");
        require(owner->available(), "duplicate startup preserves owner");

        // 直接序列化的停止可能比持有它的端口存活更久。其析构函数
        // 不得停止之后的运行时，即使旧令牌仍然存在。
        coral::runtime::stop_runtime();
        auto replacement = open_runtime(second, user / "second");
        require(replacement && replacement->available(), "second runtime initialized");
        require(coral::runtime::runtime_generation() > first_generation, "new generation allocated");
        require(!owner->available() && !old_borrower->available(), "old ports remain unavailable after restart");
        require(!old_borrower->flush_history() && old_borrower->open_session() == 0, "old port cannot mutate new runtime");
        require(old_borrower->query_page(old_token, 5, "", "nihao", 0, 7, 0).status == SessionStatus::kUnavailable,
            "old session cannot revive");
        owner.reset();
        require(replacement->available(), "stale owner destruction preserves replacement");
        check_candidates(*replacement, u8"您好", u8"中囯", false);
        replacement.reset();
        require(!coral::runtime::runtime_ready(), "replacement stopped");
        coral::runtime::stop_runtime();

        const auto before_failure = coral::runtime::runtime_generation();
        require(!open_runtime(invalid, user / "failure"), "invalid checksum rejected after successful use");
        require(!coral::runtime::runtime_ready(), "failed runtime fully stopped");
        require(coral::runtime::runtime_generation() == before_failure, "failure does not publish generation");
        // 修复同一用户路径，然后在同一进程内重试。
        std::filesystem::copy_file(first / "config/coralpinyinConfig.ini", user / "failure/config/coralpinyinConfig.ini",
            std::filesystem::copy_options::overwrite_existing);
        auto recovered = open_runtime(first, user / "failure");
        require(recovered && recovered->available(), "failure followed by retry");
        check_candidates(*recovered, u8"你好", u8"中國", true);
        recovered.reset();

        DWORD handles_before = 0;
        require(GetProcessHandleCount(GetCurrentProcess(), &handles_before) != FALSE, "read baseline handles");
        for (int round = 0; round < 3; ++round)
        {
            auto asynchronous = open_runtime(first, user / "async", false);
            require(asynchronous && asynchronous->available(), "asynchronous runtime started");
            asynchronous.reset(); // 销毁模型之前必须排空加载。
            auto next = open_runtime(second, user / "second");
            require(next && next->available(), "restart after asynchronous stop");
            check_candidates(*next, u8"您好", u8"中囯", false);
            next.reset();
        }
        DWORD handles_after = 0;
        require(GetProcessHandleCount(GetCurrentProcess(), &handles_after) != FALSE, "read final handles");
        require(handles_after <= handles_before + 2, "repeated restart does not accumulate model handles");
        old_borrower.reset();
        cold_borrower.reset();
        std::cout << "Runtime paths, model reconstruction, stale ports, failed retry and async restart passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        coral::runtime::stop_runtime();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

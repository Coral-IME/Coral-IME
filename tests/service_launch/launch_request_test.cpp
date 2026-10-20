#include "launch_request.h"
#include "coral/product/product_identity.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace coral::service;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Actions final : LaunchActions {
    bool blocked = false, regular = true, running = false, throws = false;
    std::filesystem::path settings = L"C:/installed 中文/coralpinyin.exe";
    ProcessAttempt attempt{true,0};
    std::vector<std::string> calls;
    std::filesystem::path selected;
    std::uint32_t requested_session = 0;
    bool installation_blocked() override { calls.push_back("installation"); return blocked; }
    std::filesystem::path registered_settings_path() override {
        calls.push_back("registry"); if (throws) throw std::runtime_error("artificial registry failure"); return settings;
    }
    bool is_regular_file(const std::filesystem::path& path) override { calls.push_back("file"); selected = path; return regular; }
    bool already_running(const std::filesystem::path& path, std::uint32_t session) override {
        calls.push_back("running"); selected = path; requested_session = session; return running;
    }
    ProcessAttempt launch_process(const std::filesystem::path& path, std::uint32_t session, LaunchTarget) override {
        calls.push_back("launch"); selected = path; requested_session = session; return attempt;
    }
};

void parsing() {
    for (auto text : {L"",L"0",L"01",L"-1",L"+1",L" 1",L"1 ",L"1x",L"1.0",L"4294967295",L"4294967296",L"9999999999999999",L"１２"}) {
        std::uint32_t value = 17;
        require(!parse_session_id(text,value) && value == 17, "malformed session rejected without mutation");
    }
    std::uint32_t value = 0;
    require(parse_session_id(L"1",value) && value == 1, "session one");
    require(parse_session_id(L"4294967294",value) && value == 4294967294U, "largest non-sentinel session");
    const wchar_t embedded[] = {L'1',L'\0',L'2'};
    require(!parse_session_id(std::wstring_view(embedded,3),value) && value == 4294967294U, "embedded NUL refused");
}

void authorization() {
    const LaunchRequest request{LaunchTarget::kSettings,4};
    for (const auto caller : {std::optional<CallerSession>{},std::optional<CallerSession>{{0,4}},std::optional<CallerSession>{{123,5}}}) {
        Actions actions;
        require(handle_launch(request,caller,actions).status == LaunchStatus::kAccessDenied && actions.calls.empty(), "missing or different kernel peer denied before side effects");
    }
    Actions actions;
    require(handle_launch({LaunchTarget::kSettings,0},CallerSession{123,0},actions).status == LaunchStatus::kInvalidArgument && actions.calls.empty(), "system session never launched");
    require(handle_launch({static_cast<LaunchTarget>(99),4},CallerSession{123,4},actions).status == LaunchStatus::kInvalidArgument && actions.calls.empty(), "unknown target refused");
}

void selection_and_failure() {
    const CallerSession caller{123,4};
    for (auto target : {LaunchTarget::kSettings,LaunchTarget::kSessionHost,LaunchTarget::kRenderer}) {
        Actions actions;
        require(handle_launch({target,4},caller,actions).status == LaunchStatus::kStarted, "selected role launch");
        const auto expected = target == LaunchTarget::kSettings ? coral::product::kSettingsExeWide :
            target == LaunchTarget::kSessionHost ? coral::product::kSessionHostExeWide : coral::product::kRendererExeWide;
        require(actions.selected.filename() == expected && actions.selected.parent_path() == actions.settings.parent_path() && actions.requested_session == 4,
            "registered sibling path and explicit caller session");
        actions.calls.clear(); actions.running = true;
        require(handle_launch({target,4},caller,actions).status == LaunchStatus::kAlreadyRunning && actions.calls.back() == "running", "same exact path/session avoids launch");
    }
    for (const auto path : {L"",L"relative/coralpinyin.exe",L"C:/installed/other.exe",L"C:/\"quoted\"/coralpinyin.exe"}) {
        Actions actions; actions.settings = path;
        require(handle_launch({LaunchTarget::kSessionHost,4},caller,actions).status == LaunchStatus::kPathNotFound && actions.calls.size() == 2, "invalid registration never probes current directory");
    }
    std::wstring nul_path = L"C:/installed";
    nul_path.push_back(L'\0'); nul_path += L"bad/coralpinyin.exe";
    Actions nul; nul.settings = nul_path;
    require(handle_launch({LaunchTarget::kSettings,4},caller,nul).status == LaunchStatus::kPathNotFound, "embedded NUL in registration refused");
    Actions blocked; blocked.blocked = true;
    require(handle_launch({LaunchTarget::kSessionHost,4},caller,blocked).status == LaunchStatus::kInstalling && blocked.calls.size() == 1, "all roles honor installation gate");
    Actions directory; directory.regular = false;
    require(handle_launch({LaunchTarget::kRenderer,4},caller,directory).status == LaunchStatus::kPathNotFound && directory.calls.back() == "file", "non-file rejected");
    Actions failed; failed.attempt = {false,5};
    const auto reply = handle_launch({LaunchTarget::kSettings,4},caller,failed);
    require(reply.status == LaunchStatus::kLaunchFailed && reply.error == 5 && failed.calls.back() == "launch", "native error preserved without retry");
    Actions exception; exception.throws = true;
    require(handle_launch({LaunchTarget::kSettings,4},caller,exception).status == LaunchStatus::kInternalError, "action exception becomes error");
}
}
int main() {
    try { parsing(); authorization(); selection_and_failure(); std::cout << "service launch policy passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

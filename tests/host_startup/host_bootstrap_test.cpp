#include "host_bootstrap.h"
#include "coral/product/product_identity.h"
#include <deque>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace coral::bootstrap;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

class FakeStartup final : public StartupActions {
public:
    std::uint64_t tick = 0;
    std::uint32_t path_cost = 0, launch_cost = 0;
    bool blocked = false, restricted = false, direct_ok = true, service_ok = true;
    bool throw_path = false;
    std::filesystem::path path = L"C:/artificial directory/coral_engine_host.exe";
    std::deque<ProbeStatus> probes{ProbeStatus::kRetry,ProbeStatus::kReady};
    std::vector<std::uint32_t> waits;
    std::vector<std::string> calls;
    std::uint64_t now_ms() override { return tick; }
    void pause(std::uint32_t wait_ms) override { calls.push_back("pause"); tick += wait_ms; }
    ProbeStatus probe_pipe(std::uint32_t wait_ms) override {
        waits.push_back(wait_ms);
        const auto result = probes.empty() ? ProbeStatus::kRetry : probes.front();
        if (!probes.empty()) probes.pop_front();
        if (result == ProbeStatus::kRetry) tick += wait_ms;
        return result;
    }
    bool installation_blocked() override { calls.push_back("installation"); return blocked; }
    bool restricted_host() override { calls.push_back("restriction"); return restricted; }
    std::filesystem::path session_host_path() override {
        calls.push_back("path"); tick += path_cost;
        if (throw_path) throw std::runtime_error("artificial path error");
        return path;
    }
    bool launch_direct(const std::filesystem::path& executable) override {
        require(executable == path && executable.is_absolute(), "explicit absolute path passed");
        calls.push_back("direct"); tick += launch_cost; return direct_ok;
    }
    bool launch_service() override { calls.push_back("service"); tick += launch_cost; return service_ok; }
};

void policy() {
    FakeStartup already; already.probes = {ProbeStatus::kReady}; already.blocked = true;
    auto result = ensure_host_ready(already,0);
    require(static_cast<bool>(result) && already.calls.empty() && already.waits == std::vector<std::uint32_t>{0}, "ready first probe causes no startup side effects");
    FakeStartup denied; denied.probes = {ProbeStatus::kFailed};
    require(ensure_host_ready(denied,100).status == StartupStatus::kProbeFailed && denied.calls.empty(), "fatal initial probe never launches");
    FakeStartup blocked; blocked.blocked = true;
    require(ensure_host_ready(blocked,100).status == StartupStatus::kInstallationBlocked && blocked.calls == std::vector<std::string>{"installation"}, "installation gate");
    FakeStartup zero;
    require(ensure_host_ready(zero,0).status == StartupStatus::kTimedOut && zero.calls == std::vector<std::string>{"installation"}, "zero budget never launches");
    FakeStartup normal;
    result = ensure_host_ready(normal,100);
    require(static_cast<bool>(result) && result.direct_attempted && !result.service_attempted, "ordinary direct startup");
    FakeStartup restricted; restricted.restricted = true;
    result = ensure_host_ready(restricted,100);
    require(static_cast<bool>(result) && !result.direct_attempted && result.service_attempted &&
        restricted.calls == std::vector<std::string>{"installation","restriction","service"}, "restricted hosts never resolve or spawn a child");
    FakeStartup fallback; fallback.direct_ok = false;
    result = ensure_host_ready(fallback,100);
    require(static_cast<bool>(result) && result.direct_attempted && result.service_attempted, "direct failure falls back once");
    FakeStartup missing; missing.path.clear(); missing.service_ok = false;
    result = ensure_host_ready(missing,100);
    require(result.status == StartupStatus::kLaunchFailed && !result.direct_attempted && result.service_attempted, "missing host uses service; failure propagated");
    FakeStartup relative; relative.path = "coral_engine_host.exe";
    result = ensure_host_ready(relative,100);
    require(static_cast<bool>(result) && !result.direct_attempted && result.service_attempted, "relative path never spawned");
    FakeStartup fatal; fatal.probes = {ProbeStatus::kRetry,ProbeStatus::kFailed};
    require(ensure_host_ready(fatal,100).status == StartupStatus::kProbeFailed, "fatal readiness failure does not relaunch");
    FakeStartup exception; exception.throw_path = true;
    require(ensure_host_ready(exception,100).status == StartupStatus::kInternalError, "hook error translated");
}

void budget() {
    FakeStartup exhausted; exhausted.path_cost = 51;
    auto result = ensure_host_ready(exhausted,50);
    require(result.status == StartupStatus::kTimedOut && !result.direct_attempted && !result.service_attempted, "path resolution shares total budget");
    FakeStartup slow; slow.launch_cost = 60; slow.direct_ok = false;
    result = ensure_host_ready(slow,50);
    require(result.status == StartupStatus::kTimedOut && result.direct_attempted && !result.service_attempted, "no service fallback after slow direct attempt");
    FakeStartup absent; absent.path_cost = 12; absent.launch_cost = 7; absent.probes = {ProbeStatus::kRetry};
    require(ensure_host_ready(absent,50).status == StartupStatus::kTimedOut && absent.tick == 50, "wait and pause consume remaining budget exactly");
    require(absent.waits == std::vector<std::uint32_t>{0,25}, "no fixed delay extends the remaining window");
    FakeStartup wrap; wrap.tick = std::numeric_limits<std::uint64_t>::max() - 10; wrap.probes = {ProbeStatus::kRetry};
    require(ensure_host_ready(wrap,50).status == StartupStatus::kTimedOut && wrap.tick == 39, "elapsed subtraction survives tick wrap");
}

void paths() {
    const std::filesystem::path adjacent = L"C:/module space/中文";
    const std::filesystem::path installed = L"C:/installed";
    std::vector<std::filesystem::path> visited;
    auto selected = select_session_host_path(adjacent,installed,[&](const auto& path) { visited.push_back(path); return true; });
    require(selected == adjacent / coral::product::kSessionHostExeWide && visited.size() == 1, "adjacent unicode/space path preferred");
    selected = select_session_host_path(adjacent,installed,[&](const auto& path) { return path.parent_path() == installed; });
    require(selected == installed / coral::product::kSessionHostExeWide, "installed regular file fallback");
    visited.clear();
    selected = select_session_host_path({},"relative",[&](const auto& path) { visited.push_back(path); return true; });
    require(selected.empty() && visited.empty(), "empty and relative directories never searched");
    require(select_session_host_path(adjacent,installed,[](const auto&) { return false; }).empty(), "directories or missing files rejected by file predicate");
}
}
int main() {
    try { policy(); budget(); paths(); std::cout << "host startup boundaries passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#include "pch.h"
#include "configauthority.h"
#include "preference_registry.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using coral::tsf::ConfigAuthority;
using coral::tsf::ImeConfigSnapshot;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

auto initial_snapshot()
{
    auto result = std::make_shared<ImeConfigSnapshot>();
    result->revision = 7;
    result->theme = "light";
    return result;
}

void blocked_reader()
{
    std::promise<void> entered, release;
    auto released = release.get_future();
    auto started = entered.get_future();
    std::vector<std::function<void()>> jobs;
    int reads = 0;
    ConfigAuthority cache([&] {
        ++reads;
        entered.set_value();
        released.wait();
        auto next = initial_snapshot();
        next->theme = "dark";
        return next;
    }, [&](auto job) { jobs.push_back(std::move(job)); return true; }, initial_snapshot());

    const auto previous = cache.current();
    for (int i = 0; i != 10000; ++i) require(cache.refresh() == previous, "hot reads retain snapshot");
    require(jobs.empty() && reads == 0, "hot reads never schedule or load settings");
    cache.request_refresh();
    cache.request_refresh();
    require(jobs.size() == 1, "only one refresh may be in flight");
    std::thread worker(std::move(jobs.back()));
    started.wait();
    const auto begin = std::chrono::steady_clock::now();
    bool unchanged = true;
    for (int i = 0; i != 10000; ++i) unchanged = unchanged && cache.refresh() == previous;
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    cache.request_refresh();
    release.set_value();
    worker.join();
    require(unchanged && elapsed < std::chrono::seconds(1), "blocked I/O does not block hot reads");
    require(reads == 1 && jobs.size() == 1, "slow refresh does not accumulate work");
    require(cache.current()->theme == "dark" && cache.current()->revision == 8, "complete result publishes once");
    require(previous->theme == "light", "published snapshots remain immutable");
}

void failure_and_shutdown()
{
    std::function<void()> job;
    int mode = 0, reads = 0;
    bool accept = false;
    ConfigAuthority cache([&]() -> std::shared_ptr<ImeConfigSnapshot> {
        ++reads;
        if (mode == 0) throw std::runtime_error("synthetic read failure");
        if (mode == 1) return {};
        auto value = initial_snapshot();
        if (mode == 3) value->theme = "dark";
        return value;
    }, [&](auto next) { if (!accept) return false; job = std::move(next); return true; }, initial_snapshot());
    const auto previous = cache.current();
    cache.request_refresh();
    require(!job && reads == 0, "scheduler rejection does not run loader");
    accept = true;
    for (mode = 0; mode != 4; ++mode) {
        cache.request_refresh();
        require(static_cast<bool>(job), "refresh retries after failure");
        std::exchange(job, {})();
        if (mode < 3) require(cache.current() == previous, "failure, empty or unchanged result keeps snapshot");
    }
    cache.request_refresh();
    const auto valid = cache.current();
    cache.stop();
    const auto before = reads;
    std::exchange(job, {})();
    require(reads == before && cache.current() == valid, "stopped queued work does not load or publish");

    std::function<void()> retired;
    {
        ConfigAuthority dying([&] { ++reads; return initial_snapshot(); },
            [&](auto next) { retired = std::move(next); return true; }, initial_snapshot());
        dying.request_refresh();
    }
    retired();
    require(reads == before, "queued work owns state safely after authority destruction");
}

void stop_during_read()
{
    std::promise<void> entered, release;
    auto started = entered.get_future();
    auto released = release.get_future();
    std::function<void()> job;
    auto cache = std::make_unique<ConfigAuthority>([&] {
        entered.set_value();
        released.wait();
        auto result = initial_snapshot();
        result->theme = "dark";
        return result;
    }, [&](auto next) { job = std::move(next); return true; }, initial_snapshot());
    cache->request_refresh();
    std::thread worker(std::move(job));
    started.wait();
    const auto begin = std::chrono::steady_clock::now();
    cache.reset();
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    release.set_value();
    worker.join();
    require(elapsed < std::chrono::seconds(1), "teardown does not join slow disk work");
}

void write_settings(const std::filesystem::path& path, int count)
{
    std::ofstream file(path);
    file << "[settingsconfig]\ncandidateCount=" << count << "\nisFullWidth=false\n"
         << "[placeholder]\nsample_editor=2\n";
    require(file.good(), "synthetic INI written");
}

void production_reader(const std::filesystem::path& root)
{
    const auto path = root / "snapshot.ini";
    write_settings(path, 6);
    auto frozen = SettingsConfig::read_only_snapshot(path.string(),
        {{"candidateCount", "9"}, {"isFullWidth", "true"}});
    require(frozen && frozen->getCandidateCount() == 9 && !frozen->runtime_full_width(),
        "shared snapshot merges only shared fields");
    require(frozen->getPlaceHolderValue("sample_editor") == 2, "per-application placeholders are frozen too");
    require(!SettingsConfig::read_only_snapshot((root / "absent.ini").string(), {}),
        "missing INI fails without creating defaults");
    std::function<void()> retired_job;
    int retired_reads = 0;
    ConfigAuthority cache([&] { ++retired_reads; return initial_snapshot(); },
        [&](auto job) { retired_job = std::move(job); return true; }, initial_snapshot());
    cache.request_refresh();
    cache.initialize(path.string());
    const auto activated = cache.current();
    cache.request_refresh();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(cache.current() == activated, "reactivation shares the pending-work limit");
    retired_job();
    require(retired_reads == 0, "old activation cannot load or publish after reactivation");
    auto wait_for = [&](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        do {
            cache.request_refresh();
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    };
    require(wait_for([&] { return cache.current()->candidateCount == 6; }),
        "absent shared memory uses INI in native background reader");
    require(cache.current()->placeholder_scheme("sample_editor.exe") == 2,
        "native snapshot preserves per-application placeholder lookup");
    sConfig->init(path.string());
    SharedMemoryConfig writer;
    require(writer.initialize(), "isolated shared writer ready");
    require(writer.setConfig("candidateCount", "11"), "shared configuration changed");
    require(wait_for([&] { return cache.current()->candidateCount == 11; }), "native background callback observes shared change");
    const auto previous = cache.current();
    std::filesystem::rename(path, root / "saved.ini");
    cache.request_refresh();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(cache.current() == previous, "unreadable INI retains last complete snapshot");
    std::filesystem::rename(root / "saved.ini", path);
    require(writer.setConfig("candidateCount", "12"), "shared configuration changed again");
    require(wait_for([&] { return cache.current()->candidateCount == 12; }), "reader recovers without reactivation");
    require(frozen->getCandidateCount() == 9, "reader never mutates prior settings view");
    cache.stop();
}
} // 匿名命名空间结束

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "isolated user directory required");
        blocked_reader();
        failure_and_shutdown();
        stop_during_read();
        production_reader(std::filesystem::path(argv[1]));
        std::cout << "Configuration snapshot concurrency and native reader checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

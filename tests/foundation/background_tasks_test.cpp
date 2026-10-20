#include "background_tasks.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using coral::foundation::BackgroundTasks;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        BackgroundTasks tasks;
        require(tasks.finish(), "empty finish");
        std::atomic<int> completed{0};
        require(!tasks.start({BackgroundTasks::Task{}}), "reject absent task");
        require(tasks.start({[&] { ++completed; return true; }, [&] { ++completed; return true; }}), "start batch");
        require(!tasks.start({[] { return true; }}), "reject replacement before collection");
        require(tasks.finish() && completed == 2, "collect every job");
        require(tasks.start({[]() -> bool { throw std::runtime_error("expected"); },
            [&] { ++completed; return true; }, [] { return false; }}), "start failure batch");
        require(!tasks.finish() && completed == 3, "report exceptions and still drain other work");
        require(tasks.finish(), "idempotent finish");

        std::promise<void> entered, release;
        auto barrier = release.get_future().share();
        require(tasks.start({[&] { entered.set_value(); barrier.wait(); return true; }}), "start blocked task");
        entered.get_future().wait();
        auto drained = std::async(std::launch::async, [&] { return tasks.finish(); });
        const bool waited = drained.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
        release.set_value();
        require(waited, "finish must wait for task lifetime");
        require(drained.get(), "finish completes after release");
        {
            BackgroundTasks owned;
            require(owned.start({[&] { ++completed; return true; }}), "owned task");
        }
        require(completed == 4, "destructor drains during ordinary object lifetime");
        for (int i = 0; i < 100; ++i) {
            require(tasks.start({[&] { ++completed; return true; }}), "restart batch");
            require(tasks.finish(), "finish repeated batch");
        }
        require(completed == 104, "each repeated task ran once");
        std::cout << "Background task ownership contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

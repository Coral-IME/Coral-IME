#include "pch.h"
#include "core_operations.h"
#include "runtime_context.h"
#include "syllable_parser.h"
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3 || !std::getenv("CORAL_TEST_NAMESPACE") || !std::getenv("CORAL_USER_DATA_DIR")) return 2;
    const auto runtime = std::filesystem::absolute(argv[1]);
    const auto user = std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(user / "config");
    std::filesystem::copy_file(runtime / "config/coralpinyinConfig.ini", user / "config/coralpinyinConfig.ini",
        std::filesystem::copy_options::overwrite_existing);
    if (!coral::runtime::start_runtime(user.string().c_str(), false)) return 3;
    // 停止历史工作线程（加载可能创建它）之前，关机必须等待。
    coral::runtime::stop_runtime();
    const bool primary_ready = coral::phonetics::SyllableParser::instance()
        .get_lexicon_manager().get_primary_lexicon().is_loaded();
    coral::runtime::stop_runtime();
    if (primary_ready || coral::Engine::compatInstance().initialized()) return 4;
    std::cout << "Asynchronous initialization drained and model released at shutdown\n";
    // 静态可执行文件清理在 DLL 加载器锁之外演练自有加载任务。
    return 0;
}

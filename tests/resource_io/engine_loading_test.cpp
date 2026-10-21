#include "pch.h"
#include "core_operations.h"
#include "runtime_context.h"
#include "syllable_parser.h"
#include "ini_store.h"
#include "text_file_helpers.hpp"
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

// 在真实资源映射的锁处暂停加载；保护期限避免旧版本失败时残留持锁线程。
class LoadingBlocker {
public:
    explicit LoadingBlocker(const std::wstring& name) {
        mutex_ = CreateMutexW(nullptr, FALSE, (name + L"_Mutex").c_str());
        require(mutex_ != nullptr, "create isolated loading blocker");
        auto unlocked = unlock_.get_future();
        std::promise<bool> acquired;
        auto locked = acquired.get_future();
        owner_ = std::thread([this, unlocked = std::move(unlocked), acquired = std::move(acquired)]() mutable {
            const bool held = WaitForSingleObject(mutex_, 1000) == WAIT_OBJECT_0;
            acquired.set_value(held);
            if (held) {
                unlocked.wait_for(std::chrono::seconds(3));
                ReleaseMutex(mutex_);
            }
        });
        if (!locked.get()) { release(); CloseHandle(mutex_); throw std::runtime_error("acquire loading blocker"); }
    }
    ~LoadingBlocker() { release(); CloseHandle(mutex_); }
    void release() {
        if (owner_.joinable()) { unlock_.set_value(); owner_.join(); }
    }
private:
    HANDLE mutex_ = nullptr;
    std::promise<void> unlock_;
    std::thread owner_;
};

bool query_greeting() {
    std::vector<std::string> words, pinyins;
    std::vector<int> fixed;
    return coral::phonetics::SyllableParser::instance().parse_input("nihao", words, pinyins, fixed, 0, 7, 0) &&
        std::find(words.begin(), words.end(), u8"你好") != words.end();
}

void check_first_query(const std::filesystem::path& runtime, const std::filesystem::path& user,
    const std::string& resource) {
    std::filesystem::create_directories(user / "config");
    std::filesystem::copy_file(runtime / "config/coralpinyinConfig.ini", user / "config/coralpinyinConfig.ini",
        std::filesystem::copy_options::overwrite_existing);
    IniFileManager config((user / "config/coralpinyinConfig.ini").string());
    const auto version = config.getIniValue("app", "Version");
    LoadingBlocker blocker(hlp::buildSHNameW(version, resource));
    require(coral::runtime::start_runtime(user.string().c_str(), false), "async runtime accepted");
    auto first = std::async(std::launch::async, query_greeting);
    const bool waited = first.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout;
    blocker.release();
    const bool correct = first.get();
    coral::runtime::stop_runtime();
    require(waited, "first query must wait for every background model task");
    require(correct, "first query uses the completed model without losing candidates");
}

coral::phonetics::LexiconLocations model_locations() {
    const auto& options = coral::Engine::compatInstance().options();
    IniFileManager config(options.userConfigIni.string());
    const auto data = options.dataDir;
    const auto general = data / "generallexicon";
    auto resource = [&](const std::filesystem::path& file, const char* key) {
        return coral::phonetics::ResourceLocation{file, config.getIniValue("LexiconFiles", key)};
    };
    return {resource(data / "bushou.marisa", "BushouTrie"), resource(data / "bushou.dat", "BushouData"),
        resource(data / "stroke.marisa", "StrokeTrie"), resource(data / "stroke.dat", "StrokeData"),
        resource(general / "languagemodel.coral", "BigramGBin"), resource(general / "english.dat", "EnglishData"),
        resource(general / "english.marisa", "EnglishMarisa"), resource(general / "primary.dat", "PrimaryData"),
        resource(general / "primary.marisa", "PrimaryMarisa"), resource(general / "correction_pinyin_model.dat", "CorrectionData"),
        resource(general / "char_lm.dat", "CharLMData"), options.userLexiconFile, data, config.getIniValue("app", "Version")};
}

void require_failed_query() {
    std::vector<std::string> words{"stale"}, pinyins{"stale"};
    std::vector<int> fixed{1};
    auto& parser = coral::phonetics::SyllableParser::instance();
    require(!parser.parse_input("nihao", words, pinyins, fixed, 0, 7, 0), "failed load cannot publish model readiness");
    bool has_next = true;
    parser.is_exist_next_page(has_next);
    require(words.empty() && pinyins.empty() && fixed.empty() && !has_next, "failed load clears all query outputs");
}

void check_failure_and_retry(const std::filesystem::path& runtime, const std::filesystem::path& user) {
    std::filesystem::create_directories(user / "config");
    std::filesystem::copy_file(runtime / "config/coralpinyinConfig.ini", user / "config/coralpinyinConfig.ini",
        std::filesystem::copy_options::overwrite_existing);
    require(coral::runtime::start_runtime(user.string().c_str(), true), "start failure fixture");
    const auto paths = model_locations();
    auto bad = paths;
    bad.correctionData.identifier = "invalid-checksum";
    auto& parser = coral::phonetics::SyllableParser::instance();
    require(!parser.load(bad, true), "synchronous failure reported");
    require(!parser.load(bad, true), "repeated failure cannot be mistaken for a loaded model");
    require_failed_query();
    require(parser.load(bad, false), "asynchronous failing batch accepted");
    require_failed_query();
    require_failed_query();
    require(parser.load(paths, true) && query_greeting(), "explicit recovery after a failed batch");
    auto queries = std::async(std::launch::async, [] {
        for (int index = 0; index < 24; ++index) require(query_greeting(), "query during serialized reload");
    });
    auto reload = std::async(std::launch::async, [&] {
        for (int index = 0; index < 4; ++index) require(parser.load(paths, true), "reload during serialized query");
    });
    queries.get(); reload.get();
    coral::runtime::stop_runtime();

    auto missing = paths;
    missing.correctionData.path = user / "recovered-correction.dat";
    auto& retrying = coral::phonetics::SyllableParser::instance();
    require(!retrying.load(missing, true, true), "retry fixture initially unavailable");
    require_failed_query();
    std::filesystem::copy_file(paths.correctionData.path, missing.correctionData.path);
    require(query_greeting(), "retry policy recovers when missing data becomes available");
    coral::runtime::stop_runtime();
}
}

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
    try {
        check_first_query(runtime, user / "primary", "PrimaryLexiconGBin");
        check_first_query(runtime, user / "model", "CorrectionModel");
        check_failure_and_retry(runtime, user / "failure");
    } catch (const std::exception& error) {
        coral::runtime::stop_runtime();
        std::cerr << error.what() << '\n';
        return 1;
    }
    // 静态可执行文件清理在 DLL 加载器锁之外演练自有加载任务。
    return 0;
}

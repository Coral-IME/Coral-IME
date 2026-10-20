#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include "syllable_helpers.h"
#include "core_operations.h"
#include "coral/composition/session_port.h"

namespace fs = std::filesystem;
static int failures = 0;
static void Check(bool ok, const char* name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) ++failures;
}
static std::vector<std::string> Parse(const std::string& input, int mode = 0, int offset = 0, int count = 7) {
    std::vector<std::string> words, pinyin;
    int found = 0;
    coral::runtime::parse_candidates("", input.c_str(), std::back_inserter(words), std::back_inserter(pinyin),
        offset, count, 512, &found, mode);
    return words;
}
static bool Has(const std::vector<std::string>& words, const char* word) {
    return std::find(words.begin(), words.end(), word) != words.end();
}
static bool Mapped(const std::string& version, const char* dictionary) {
    const std::string name = hlp::buildSHName(version, std::string(dictionary) + "SharedMemory") + "SharedMemory";
    const HANDLE handle = ::OpenFileMappingW(FILE_MAP_READ, FALSE, std::wstring(name.begin(), name.end()).c_str());
    if (!handle) return false;
    ::CloseHandle(handle);
    return true;
}
static void CheckAnnotated(coral::composition::SessionPort& port, coral::composition::SessionToken session,
    const char* input, const char* word, const char* label, int mode = 0) {
    const auto parsed = port.query_page(session, 1, "", input, 0, 60, mode);
    const auto& candidates = parsed.output.candidates.candidates;
    const auto found = std::find_if(candidates.begin(), candidates.end(),
        [word, label](const coral::composition::Candidate& c) { return c.word == word && c.display_text == label; });
    Check(found != candidates.end(), "annotated candidate has separate label and commit text");
    if (found == candidates.end()) return;
    const int index = static_cast<int>(found - candidates.begin());
    const auto cached = port.query_page(session, 2, "", input, index, 1, mode);
    Check(cached.output.candidates.candidates.size() == 1 &&
        cached.output.candidates.candidates[0].word == word &&
        cached.output.candidates.candidates[0].display_text == label, "paged annotated candidate preserves metadata");
    const auto selected = port.select_candidate(session, 3, 0);
    Check(selected.output.commit_text == word, "selection commits only the word");
}
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const auto data = fs::absolute(argv[1]), user = fs::absolute(argv[2]);
    fs::create_directories(user / "config");
    fs::copy_file(data / "config" / "coralpinyinConfig.ini", user / "config" / "coralpinyinConfig.ini",
        fs::copy_options::overwrite_existing);
    _putenv_s("CORAL_LANGENGINE_DATA_DIR", data.string().c_str());
    char versionBuffer[256]{};
    ::GetPrivateProfileStringA("app", "Version", "", versionBuffer, sizeof(versionBuffer),
        (data / "config" / "coralpinyinConfig.ini").string().c_str());
    const std::string version(versionBuffer);
    const bool isolated = version.find("uv-test-") == 0;
    const auto start = std::chrono::steady_clock::now();
    Check(coral::runtime::start_runtime(user.string().c_str(), true), "initialization");
    std::cout << "init_us=" << std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count() << '\n';
    const int fuzzy[19]{};
    coral::runtime::apply_runtime_settings(true, true, true, false, false, false, 100, 0, fuzzy, 19);
    Check(Has(Parse("nihao"), u8"你好"), "ordinary pinyin");
    Check(Parse("u") == std::vector<std::string>{"u"}, "U prefix handled locally in engine");
    Check(Parse("uu") == std::vector<std::string>{"uu"}, "UU prefix handled");
    Check(Parse("v") == std::vector<std::string>{"v"}, "V prefix handled");
    Check(Has(Parse("v1+2"), "3"), "V calculator");
    Check(Has(Parse("V1+2", 1), "3"), "doublepinyin uppercase V");
    Check(!Has(Parse("v1+2", 1), "3"), "doublepinyin lowercase v is ordinary");
    if (isolated) Check(!Mapped(version, "stroke") && !Mapped(version, "bushou"), "U dictionaries stay unloaded during ordinary and V input");
    Check(Has(Parse("uong"), u8"用"), "unmatched U input corrects to yong");
    Check(Has(Parse("uong"), u8"用"), "cached U correction candidates remain available");
    if (argc > 3 && std::string(argv[3]) == "--missing") {
        Check(Parse("umumumu").empty(), "missing radical is nonfatal");
        Check(Parse("ushhsh").empty(), "missing strokes are nonfatal");
        Check(Parse("uudw").empty(), "missing symbols are nonfatal");
    } else {
        Check(Has(Parse("umumumu"), u8"瞐"), "radical dictionary");
        if (isolated) Check(Mapped(version, "bushou") && !Mapped(version, "stroke"), "radical-only query loads only radicals");
        Check(Has(Parse("ushhsh"), u8"㐀"), "stroke dictionary");
        if (isolated) Check(Mapped(version, "stroke"), "stroke dictionary loads on its first query");
        Check(Has(Parse("uudw"), u8"℃"), "symbol dictionary");
        Check(Has(Parse("Uumumumu", 1), u8"瞐") == false, "UU is a symbol trigger");
        Check(Has(Parse("Umumumu", 1), u8"瞐"), "doublepinyin U");
        auto port = coral::composition::make_local_session_port();
        const auto session = port->open_session();
        std::string correction_input;
        coral::composition::SessionResult corrected;
        for (char letter : std::string("uong")) {
            correction_input += letter;
            corrected = port->query_page(session, 10, "", correction_input, 0, 7, 0);
        }
        const auto& corrected_candidates = corrected.output.candidates.candidates;
        const auto corrected_word = std::find_if(corrected_candidates.begin(), corrected_candidates.end(),
            [](const coral::composition::Candidate& candidate) { return candidate.word == u8"用"; });
        Check(corrected_word != corrected_candidates.end(), "incremental U input exposes corrected candidates through session");
        if (corrected_word != corrected_candidates.end()) {
            const auto selected = port->select_candidate(session, 11, static_cast<int>(corrected_word - corrected_candidates.begin()));
            Check(selected.output.commit_text == u8"用" && selected.output.composition.empty(),
                "corrected U selection commits word and consumes the whole input");
        }
        CheckAnnotated(*port, session, "umumumu", u8"瞐", u8"瞐(mò)");
        CheckAnnotated(*port, session, "ushhsh", u8"㐀", u8"㐀(qiū)");
        CheckAnnotated(*port, session, "Umumumu", u8"瞐", u8"瞐(mò)", 1);
        CheckAnnotated(*port, session, "shaizi", u8"骰子", u8"骰(tou)子");
        CheckAnnotated(*port, session, "dafubianbian", u8"大腹便便", u8"大腹便(pian)便(pian)");
        coral::runtime::apply_runtime_settings(true, true, true, false, true, false, 100, 0, fuzzy, 19);
        CheckAnnotated(*port, session, "dafubianbian", u8"大腹便便", u8"大腹便(pian)便(pian)");
        CheckAnnotated(*port, session, "gangfuziyong", u8"剛愎自用", u8"剛愎(bi)自用");
        coral::runtime::apply_runtime_settings(true, true, true, false, false, false, 100, 0, fuzzy, 19);
        const auto normal = port->query_page(session, 4, "", "nihao", 0, 7, 0);
        Check(std::all_of(normal.output.candidates.candidates.begin(), normal.output.candidates.candidates.end(),
            [](const coral::composition::Candidate& c) { return c.display_text.empty(); }), "ordinary candidates have no annotation metadata");
        const auto expression = port->query_page(session, 5, "", "v(1+2)*3", 0, 7, 0);
        Check(std::any_of(expression.output.candidates.candidates.begin(), expression.output.candidates.candidates.end(),
            [](const coral::composition::Candidate& c) { return c.word == "(1+2)*3=9" && c.display_text.empty(); }),
            "literal parentheses in V expressions are preserved");
        port->close_session(session);
        const auto first = Parse("uudw");
        for (int i = 0; i < 10; ++i) Check(Parse("uudw") == first, "stable repeated symbols");
        Check(!Parse("uudw", 0, 7).empty(), "symbols paging");
    }
    Check(Has(Parse("v1+2"), "3"), "cache populated before disabling");
    coral::runtime::apply_runtime_settings(false, false, true, false, false, false, 100, 0, fuzzy, 19);
    Check(!Has(Parse("v1+2"), "3"), "V disabled, including cached candidates");
    Check(!Has(Parse("uudw"), u8"℃"), "U disabled");
    Check(Has(Parse("nihao"), u8"你好"), "ordinary pinyin after disabled modes");
    coral::runtime::stop_runtime();
    std::cout.flush();
    // 避免无关 DLL
    // 测试进程退出时的静态线程清理。
    ::TerminateProcess(::GetCurrentProcess(), failures ? 1 : 0);
}

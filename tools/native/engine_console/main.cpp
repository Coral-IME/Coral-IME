// coral_engine_console：静态词典核心的交互式控制台调试工具。
//
// 用途：脱离 TSF/服务进程，直接驱动引擎（start_runtime / parse_candidates /
// remember_candidate / clear_parse_cache / read_next_page_state），复现输入法主路径行为。
//
// 用法：coral_engine_console <runtimeDir> [pageSize]
//   runtimeDir：运行资源根目录（含 data/ 与 config/），也可直接传其 data/ 子目录
//   pageSize：候选页大小（默认 7，与 TSF 候选窗一致）
//
// 命令（交互式 REPL）：
//   <拼音串>      解析并打印候选（如 nihao）
//   :c <词>       设置前置词 condition（模拟连续输入）
//   :n / :p       下一页 / 上一页
//   :s <idx>      选当前页第 idx 个候选（1-based；触发 remember_candidate）
//   :r            重置（清 condition 与引擎缓存，模拟焦点切换）
//   :q            退出
//
// conditionId 语义（引擎现状，见根目录 docs/architecture.md）：
//   remember_candidate 返回被选句子的 phraseId，仅用户词典词有有效值；
//   系统词典词返回 INVALID_PHRASE_ID(0xFFFFFFFF)。条件输入链路在引擎侧
//   尚未启用（parse_candidates 的 condition 参数未使用），控制台照实打印。
//
// 说明：
//   - 系统词库目录通过 CORAL_LANGENGINE_DATA_DIR 指到构建树，不依赖注册表；
//   - 用户词典/配置使用临时 scratch 目录，不污染真实 LocalLow；
//   - 交互退出仍使用历史 TerminateProcess 策略；完整特殊模式生命周期整改待后续批次。

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>  // comm_help.hpp 依赖 Sddl.h 需先有 windows.h

#include "syllable_helpers.h"  // hlp::ImportResult 等，api.h 依赖
#include "core_operations.h"

namespace fs = std::filesystem;

namespace {

std::string TrimNul(const std::string& s) {
    auto pos = s.find('\0');
    return pos == std::string::npos ? s : s.substr(0, pos);
}

// 引擎静态析构存在已知段错误（线程池静态析构期 join），
// 输出已 flush 后直接退出，不触发 DLL 静态析构。
[[noreturn]] void Finish(int code) {
    std::cout.flush();
    std::cerr.flush();
    ::TerminateProcess(::GetCurrentProcess(), (UINT)code);
}

// UTF-8 字符串按显示宽度截断（中文 2 列），用于候选对齐
size_t DisplayWidth(const std::string& s) {
    size_t w = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        w += (len == 1) ? 1 : 2;
        i += len;
    }
    return w;
}

std::string PadTo(const std::string& s, size_t width) {
    size_t w = DisplayWidth(s);
    std::string out = s;
    while (w < width) { out += ' '; ++w; }
    return out;
}

struct ConsoleState {
    std::string condition;   // 连续输入前置词（UTF-8 词文本）
    std::string pinyin;      // 当前拼音串
    int pageOffset = 0;      // 当前页起始下标
    int pageSize = 7;
    uint32_t conditionId = 0; // remember_candidate 返回的 phraseId
};

// 解析当前状态并打印候选页
void ParseAndShow(ConsoleState& st) {
    std::vector<std::string> words, pys;
    int counts = 0;
    bool ok = coral::runtime::parse_candidates(st.condition.c_str(), st.pinyin.c_str(),
                                         std::back_inserter(words),
                                         std::back_inserter(pys),
                                         st.pageOffset, st.pageSize, 200, &counts,
                                         0);
    bool hasNext = false;
    coral::runtime::read_next_page_state(hasNext);

    std::cout << "cond=\"" << st.condition << "\" in=\"" << st.pinyin
              << "\" off=" << st.pageOffset
              << " => ok=" << (ok ? 1 : 0) << " counts=" << counts
              << " next=" << (hasNext ? 1 : 0) << "\n";
    size_t n = std::min(words.size(), pys.size());
    if (n == 0) {
        std::cout << "  (no candidates)\n";
        return;
    }
    // 对齐：词列宽 = 最长词显示宽度 + 2
    size_t wordW = 0;
    for (size_t i = 0; i < n; ++i)
        wordW = std::max(wordW, DisplayWidth(TrimNul(words[i])));
    for (size_t i = 0; i < n; ++i) {
        int idx = st.pageOffset + static_cast<int>(i) + 1;  // 全局 1-based 序号
        std::cout << "  " << idx << ". "
                  << PadTo(TrimNul(words[i]), wordW + 2)
                  << TrimNul(pys[i]) << "\n";
    }
}

bool SetupScratch(const fs::path& dataDirIn, fs::path& scratch) {
    // 布局：<运行资源根目录>/{data,config}。支持传根目录或其 data/ 子目录。
    fs::path runtimeDir = (fs::exists(dataDirIn / "data")) ? dataDirIn : dataDirIn.parent_path();
    fs::path configDir = runtimeDir / "config";
    scratch = runtimeDir / "console-scratch";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch / "config", ec);
    if (ec) return false;
    // 引擎从 coralDir/config/coralpinyinConfig.ini 读配置，用户词典也落在 coralDir
    fs::copy_file(configDir / "coralpinyinConfig.ini",
                  scratch / "config" / "coralpinyinConfig.ini",
                  fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    // 系统词库目录通过环境变量选择运行资源根目录（引擎内部会拼 data/…）
    std::string dir = runtimeDir.string();
    _putenv_s("CORAL_LANGENGINE_DATA_DIR", dir.c_str());
    return true;
}

void PrintHelp() {
    std::cout <<
        "commands:\n"
        "  <pinyin>     parse pinyin string (e.g. nihao)\n"
        "  :c <word>    set condition word (continuous input prefix)\n"
        "  :n / :p      next / previous page\n"
        "  :s <idx>     select candidate idx (1-based in current page)\n"
        "  :r           reset (clear condition & engine cache)\n"
        "  :q           quit\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: coral_engine_console <runtimeDir | runtimeDataDir> [pageSize]\n";
        Finish(2);
    }
    fs::path dataDir = fs::absolute(argv[1]);
    int pageSize = (argc > 2) ? std::atoi(argv[2]) : 7;
    if (pageSize <= 0) pageSize = 7;

    // 控制台 UTF-8 输出（候选词为 UTF-8 字节串）
    ::SetConsoleOutputCP(65001);
    ::SetConsoleCP(65001);

    fs::path scratch;
    if (!SetupScratch(dataDir, scratch)) {
        std::cerr << "failed to setup scratch dir under " << scratch << "\n";
        Finish(2);
    }

    if (!coral::runtime::start_runtime(scratch.string().c_str(), true)) {
        std::cerr << "start_runtime failed\n";
        Finish(3);
    }

    ConsoleState st;
    st.pageSize = pageSize;

    std::cout << "coral_engine_console ready. dataDir=" << dataDir.string()
              << " pageSize=" << st.pageSize << "\n";
    PrintHelp();

    std::string line;
    while (std::cout << "> ", std::getline(std::cin, line)) {
        // 去首尾空白
        size_t b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r\n");
        line = line.substr(b, e - b + 1);
        if (line.empty()) continue;

        if (line == ":q" || line == ":quit" || line == ":exit") {
            break;
        } else if (line == ":h" || line == ":help") {
            PrintHelp();
        } else if (line == ":n") {
            st.pageOffset += st.pageSize;
            ParseAndShow(st);
        } else if (line == ":p") {
            st.pageOffset = std::max(0, st.pageOffset - st.pageSize);
            ParseAndShow(st);
        } else if (line == ":r") {
            st.condition.clear();
            st.pinyin.clear();
            st.pageOffset = 0;
            st.conditionId = 0;
            coral::runtime::clear_parse_cache();
            std::cout << "reset. condition cleared, engine cache cleared\n";
        } else if (line.rfind(":c ", 0) == 0) {
            st.condition = line.substr(3);
            st.pageOffset = 0;
            std::cout << "condition = \"" << st.condition << "\"\n";
            if (!st.pinyin.empty()) ParseAndShow(st);
        } else if (line.rfind(":s ", 0) == 0) {
            int idx = std::atoi(line.c_str() + 3);  // 当前页内 1-based
            if (idx < 1) {
                std::cout << "index must be >= 1\n";
                continue;
            }
            // 重新解析当前页拿候选（与 TSF SelectIndex 行为一致：页内选择）
            std::vector<std::string> words, pys;
            int counts = 0;
            bool ok = coral::runtime::parse_candidates(st.condition.c_str(), st.pinyin.c_str(),
                                                 std::back_inserter(words),
                                                 std::back_inserter(pys),
                                                 st.pageOffset, st.pageSize, 200, &counts,
                                                 0);
            if (!ok || idx > static_cast<int>(words.size())) {
                std::cout << "invalid index (page has "
                          << (ok ? words.size() : 0) << " candidates)\n";
                continue;
            }
            std::string word = TrimNul(words[idx - 1]);
            std::string py = TrimNul(pys[idx - 1]);
            uint32_t condId = 0;
            coral::runtime::remember_candidate(py, word, condId);
            st.conditionId = condId;
            // 选词后：词成为新 condition（连续输入），清拼音串
            st.condition = word;
            st.pinyin.clear();
            st.pageOffset = 0;
            std::cout << "selected \"" << word << "\" (py=" << py
                      << ", conditionId=" << condId
                      << (condId == 0xFFFFFFFF ? " [system word, no phraseId]" : "")
                      << ") -> condition\n";
        } else if (line[0] == ':') {
            std::cout << "unknown command: " << line << " (try :h)\n";
        } else {
            // 普通拼音输入
            st.pinyin = line;
            st.pageOffset = 0;
            ParseAndShow(st);
        }
    }

    Finish(0);
}

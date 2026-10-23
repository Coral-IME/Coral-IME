// parse_bench：批量驱动 parse_candidates，统计解析耗时与命中率。
//
// 用法：coral_parse_bench <runtimeDir> <csv文件...> [--size N] [--out 明细.csv] [--summary 汇总.csv]
//   runtimeDir：运行资源根目录（含 data/ 与 config/），或其 data/ 子目录
//   csv 文件格式：拼音,期望候选1,期望候选2,...（UTF-8，# 开头为注释行）
//
// 输出：每次 parse_candidates 的耗时统计（min/median/mean/p95/max）、
//       目标词在返回候选内的命中数、目标词排在首位的命中数。
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "syllable_helpers.h"  // hlp::ImportResult，core_operations.h 依赖
#include "core_operations.h"

namespace fs = std::filesystem;

namespace coral::benchmark {

[[noreturn]] void finish(int code) {
    coral::runtime::stop_runtime();
    std::cout.flush();
    std::cerr.flush();
    ::TerminateProcess(::GetCurrentProcess(), (UINT)code);
}

std::string trim_nul(const std::string& s) {
    auto pos = s.find('\0');
    return pos == std::string::npos ? s : s.substr(0, pos);
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        size_t comma = line.find(',', start);
        if (comma == std::string::npos) {
            fields.push_back(trim(line.substr(start)));
            break;
        }
        fields.push_back(trim(line.substr(start, comma - start)));
        start = comma + 1;
    }
    return fields;
}

// 解析一行（UTF-8 字节转宽字符用于比较），比较按逐码点
std::wstring to_wide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 1) ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    if (!w.empty()) w.pop_back();
    return w;
}

struct Entry {
    size_t file_index;
    size_t line;
    std::string pinyin;
    std::string target;   // 期望首位候选（候选1，UTF-8）
    std::wstring targetW; // 宽字符形式用于比较
};

bool setup_scratch(const fs::path& dataDirIn, fs::path& scratch) {
    const auto absoluteDir = fs::absolute(dataDirIn);
    fs::path runtimeDir = fs::exists(absoluteDir / "data") ? absoluteDir : absoluteDir.parent_path();
    fs::path configDir = runtimeDir / "config";
    scratch = runtimeDir / ("bench-scratch-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(::GetTickCount64()));
    std::error_code ec;
    fs::create_directories(scratch / "config", ec);
    if (ec) return false;
    fs::copy_file(configDir / "coralpinyinConfig.ini",
                  scratch / "config" / "coralpinyinConfig.ini",
                  fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    _putenv_s("CORAL_LANGENGINE_DATA_DIR", runtimeDir.string().c_str());
    _putenv_s("CORAL_USER_DATA_DIR", scratch.string().c_str());
    _putenv_s("CORAL_TEST_NAMESPACE", ("parse_bench_" + std::to_string(::GetCurrentProcessId())).c_str());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace coral::benchmark;
    if (argc < 3) {
        std::cerr << "usage: coral_parse_bench <runtimeDir> <csv文件...> [--size N] [--out details.csv] [--summary summary.csv]\n";
        return 2;
    }
    ::SetConsoleOutputCP(65001);

    int pageSize = 8;
    fs::path outCsv, summaryCsv;
    std::vector<fs::path> inputs;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--size" && i + 1 < argc) pageSize = std::atoi(argv[++i]);
        else if (a == "--out" && i + 1 < argc) outCsv = argv[++i];
        else if (a == "--summary" && i + 1 < argc) summaryCsv = argv[++i];
        else inputs.push_back(argv[i]);
    }

    fs::path scratch;
    if (pageSize <= 0 || pageSize > 1000 || inputs.empty()) return 2;
    if (!setup_scratch(fs::path(argv[1]), scratch)) {
        std::cerr << "failed to setup scratch dir\n";
        finish(2);
    }
    if (!coral::runtime::start_runtime(scratch.string().c_str(), true)) {
        std::cerr << "start_runtime failed\n";
        finish(3);
    }
    std::cerr << "runtime ready, loading entries...\n";

    std::vector<Entry> entries;
    size_t skipped = 0, headers = 0;
    for (size_t fileIndex = 0; fileIndex < inputs.size(); ++fileIndex) {
        const auto& file = inputs[fileIndex];
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            std::cerr << "cannot open " << file.string() << "\n";
            finish(2);
        }
        std::string line;
        size_t lineNumber = 0;
        while (std::getline(in, line)) {
            ++lineNumber;
            if (lineNumber == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            auto fields = split_csv_line(line);
            if (fields.size() >= 2 && (fields[0] == "PinYin" || fields[0] == "Pinyin" || fields[0] == "pinyin")
                && (fields[1] == "Target" || fields[1] == "target")) { ++headers; continue; }
            if (fields.size() < 2 || fields[0].empty() || fields[1].empty()) { ++skipped; continue; }
            entries.push_back({fileIndex, lineNumber, fields[0], fields[1], to_wide(fields[1])});
        }
    }
    std::cerr << "loaded " << entries.size() << " entries (skipped " << skipped << ", headers " << headers << "), parsing...\n";
    if (entries.empty()) { std::cerr << "no valid entries\n"; finish(2); }

    // 同步加载后的预热，不计入解析时间。每个独立词条与会话桥接一致先清解析缓存。
    for (size_t i = 0; i < (std::min)(size_t{64}, entries.size()); ++i) {
        std::vector<std::string> words, pys;
        int count = 0;
        coral::runtime::clear_parse_cache();
        if (!coral::runtime::parse_candidates("", entries[i].pinyin.c_str(), std::back_inserter(words),
            std::back_inserter(pys), 0, pageSize, 256, &count, 0)) {
            std::cerr << "warmup parse failed\n"; finish(3);
        }
    }

    std::ofstream missOut;
    if (!outCsv.empty()) {
        missOut.open(outCsv, std::ios::binary);
        if (!missOut) { std::cerr << "cannot write details: " << outCsv.string() << " errno=" << errno << '\n'; finish(2); }
        missOut << "file,line,pinyin,target,ok,count,target_rank,parse_us";
        for (int j = 1; j <= pageSize; ++j) missOut << ",candidate" << j;
        missOut << '\n' << std::fixed << std::setprecision(3);
    }

    struct FileStats { size_t entries = 0, hit = 0, first = 0, failed = 0; double total_us = 0; };
    std::vector<FileStats> fileStats(inputs.size());

    std::vector<double> timesUs;
    timesUs.reserve(entries.size());
    size_t hitAny = 0, hitFirst = 0, hasCandidate = 0, failed = 0;
    double totalParseUs = 0;

    LARGE_INTEGER freq;
    ::QueryPerformanceFrequency(&freq);

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::vector<std::string> words, pys;
        int counts = 0;
        coral::runtime::clear_parse_cache(); // 不卸载词库；不计入本次函数耗时
        LARGE_INTEGER t0, t1;
        ::QueryPerformanceCounter(&t0);
        bool ok = coral::runtime::parse_candidates("", e.pinyin.c_str(),
                                         std::back_inserter(words),
                                         std::back_inserter(pys),
                                         0, pageSize, 256, &counts, 0);
        ::QueryPerformanceCounter(&t1);
        double us = (t1.QuadPart - t0.QuadPart) * 1e6 / freq.QuadPart;
        timesUs.push_back(us);
        totalParseUs += us;
        auto& stats = fileStats[e.file_index];
        ++stats.entries; stats.total_us += us;
        int rank = 0;
        if (!ok) { ++failed; ++stats.failed; }
        if (ok && counts > 0 && !words.empty()) {
            ++hasCandidate;
            for (size_t j = 0; j < words.size(); ++j)
                if (to_wide(trim_nul(words[j])) == e.targetW) { rank = static_cast<int>(j + 1); break; }
            if (rank) { ++hitAny; ++stats.hit; }
            if (rank == 1) { ++hitFirst; ++stats.first; }
        }
        if (missOut.is_open()) {
            auto quote = [](const std::string& value) {
                std::string result = "\"";
                for (char ch : value) { if (ch == '"') result += '"'; result += ch; }
                return result + '"';
            };
            missOut << quote(inputs[e.file_index].filename().string()) << ',' << e.line << ',' << quote(e.pinyin)
                    << ',' << quote(e.target) << ',' << ok << ',' << counts << ',' << rank << ',' << us;
            for (int j = 0; j < pageSize; ++j)
                missOut << ',' << quote(static_cast<size_t>(j) < words.size() ? trim_nul(words[j]) : "");
            missOut << '\n';
        }
    }

    std::sort(timesUs.begin(), timesUs.end());
    auto pct = [&](double p) {
        size_t idx = (size_t)(p / 100.0 * (timesUs.size() - 1) + 0.5);
        return timesUs[std::min(idx, timesUs.size() - 1)];
    };
    double meanUs = entries.empty() ? 0 : (double)totalParseUs / entries.size();

    std::cout << "\n==== parse_candidates 基准结果 ====\n";
    std::cout << "词条总数:     " << entries.size() << "\n";
    std::cout << "前 " << pageSize << " 候选目标命中: " << hitAny << " (" << 100.0 * hitAny / entries.size() << "%)\n";
    std::cout << "有候选: " << hasCandidate << "; 解析失败: " << failed << "; 跳过表头: " << headers << "; 无效行: " << skipped << "\n";
    std::cout << "首位命中:     " << hitFirst << " (" << (entries.empty() ? 0.0 : 100.0 * hitFirst / entries.size()) << "%)\n";
    std::cout << "---- 单次解析耗时 (us) ----\n";
    if (!timesUs.empty()) {
        std::cout << "min:    " << timesUs.front() << "\n";
        std::cout << "p50:    " << pct(50) << "\n";
        std::cout << "mean:   " << meanUs << "\n";
        std::cout << "p95:    " << pct(95) << "\n";
        std::cout << "p99:    " << pct(99) << "\n";
        std::cout << "max:    " << timesUs.back() << "\n";
        std::cout << "总耗时: " << totalParseUs / 1000.0 << " ms\n";
    }
    if (!outCsv.empty())
        std::cout << "逐条明细已写入: " << outCsv.string() << "\n";
    if (missOut.is_open()) { missOut.flush(); if (!missOut) finish(2); }
    if (!summaryCsv.empty()) {
        std::ofstream summary(summaryCsv, std::ios::binary);
        if (!summary) { std::cerr << "cannot write summary\n"; finish(2); }
        summary << std::fixed << std::setprecision(6)
                << "file,entries,hit_top" << pageSize << ",hit_first,hit_pct,first_pct,parse_failed,total_ms,mean_us\n";
        for (size_t i = 0; i < inputs.size(); ++i) {
            const auto& s = fileStats[i];
            summary << inputs[i].filename().string() << ',' << s.entries << ',' << s.hit << ',' << s.first << ','
                    << (s.entries ? 100.0 * s.hit / s.entries : 0) << ',' << (s.entries ? 100.0 * s.first / s.entries : 0)
                    << ',' << s.failed << ',' << s.total_us / 1000 << ',' << (s.entries ? s.total_us / s.entries : 0) << '\n';
        }
        summary << "TOTAL," << entries.size() << ',' << hitAny << ',' << hitFirst << ','
                << 100.0 * hitAny / entries.size() << ',' << 100.0 * hitFirst / entries.size()
                << ',' << failed << ',' << totalParseUs / 1000 << ',' << meanUs << '\n';
        summary.flush(); if (!summary) finish(2);
    }
    finish(0);
}

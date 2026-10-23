// 用户词库导入：真实主词典上的自动注音、拼音规范化、查重、写入与撤销。
#include "coral/lexicon_io/text_import.h"
// 用法：lexicon_import_engine_test <runtimeDataDir>
// 用户词库写入 dataDir/../lexicon-import-scratch，不触碰真实用户数据。
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <windows.h>

#include "syllable_helpers.h"
#include "core_operations.h"

namespace fs = std::filesystem;

namespace {
int g_failures = 0;

[[noreturn]] void Finish(int code) {
    std::cout.flush();
    std::cerr.flush();
    // 引擎后台线程静态析构存在已知竞态，测试使用立即退出策略
    ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(code));
}
void Check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
UserLexiconImportItem Item(int line, const std::string& phrase, const std::string& pinyin = {}, const std::string& key = {}) {
    UserLexiconImportItem item;
    item.line = line;
    item.phrase = phrase;
    item.rawPinyin = pinyin;
    item.rawLookupKey = key;
    return item;
}
bool HasCandidate(const std::string& input, const std::string& word) {
    std::vector<std::string> words, pys;
    int counts = 0;
    coral::runtime::parse_candidates("", input.c_str(), std::back_inserter(words), std::back_inserter(pys), 0, 20, 200, &counts, 0);
    return std::any_of(words.begin(), words.end(), [&](const std::string& w) { return w.c_str() == word; });
}
const char* StatusName(UserLexiconImportStatus status) {
    switch (status) {
    case UserLexiconImportStatus::Ready: return "Ready";
    case UserLexiconImportStatus::Exists: return "Exists";
    case UserLexiconImportStatus::Repeated: return "Repeated";
    default: return "Invalid";
    }
}
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) { std::cerr << "usage: lexicon_import_engine_test <runtimeDataDir>\n"; Finish(2); }
    const fs::path dataDir = fs::absolute(argv[1]);
    const char* user = std::getenv("CORAL_USER_DATA_DIR");
    const fs::path scratch = user && *user ? fs::u8path(user) / "lexicon-import-scratch" : dataDir / ".." / "lexicon-import-scratch";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch / "config", ec);
    fs::copy_file(dataDir / "config" / "coralpinyinConfig.ini", scratch / "config" / "coralpinyinConfig.ini",
        fs::copy_options::overwrite_existing, ec);
    if (ec) { std::cerr << "cannot prepare scratch dir\n"; Finish(2); }
    _putenv_s("CORAL_LANGENGINE_DATA_DIR", dataDir.string().c_str());
    if (!coral::runtime::start_runtime(scratch.string().c_str(), true)) { std::cerr << "start_runtime failed\n"; Finish(3); }
    coral::runtime::clear_user_lexicon();

    std::vector<UserLexiconImportItem> items = {
        Item(1, "重庆"),                       // 词典整词
        Item(2, "银行"),                       // 多音字由词典确定
        Item(3, "珊瑚拼音"),                   // 非词典词：分段拼接
        Item(4, "重庆", "Chóng Qìng"),         // 带声调/空格 → 与第 1 行重复
        Item(5, "长城", "chang2cheng2"),       // 数字声调
        Item(6, "西安", "xian"),               // 无分隔：按字数切分
        Item(7, "绿色", "lǜ sè"),              // ü
        Item(8, "重庆", "chong"),              // 字数不符
        Item(9, "abc"),                        // 非汉字
        Item(10, "你好", "nh", "n'h"),         // 自有 CSV：简拼键 + 全拼字段（rawPinyin 无效）
        Item(11, "你好吗", "ni'hao'ma", "n'h'm"),
    };
    std::cout << "prepare\n";
    Check(coral::runtime::prepare_user_import(items, {}), "prepare succeeds once the engine is loaded");
    for (const auto& item : items)
        std::cout << "    " << item.line << " " << item.phrase << " -> [" << item.pinyin << "] key=[" << item.lookupKey
                  << "] " << StatusName(item.status) << (item.polyphonic ? " polyphonic" : "") << " " << item.message << "\n";
    const auto& at = [&](int line) -> const UserLexiconImportItem& { return items[static_cast<size_t>(line - 1)]; };
    Check(at(1).pinyin == "chong'qing" && at(1).source == UserLexiconPinyinSource::Generated && !at(1).polyphonic, "重庆 auto pinyin from dictionary");
    Check(at(2).pinyin == "yin'hang", "银行 polyphone resolved by dictionary word");
    Check(at(3).pinyin == "shan'hu'pin'yin" && at(3).status == UserLexiconImportStatus::Ready, "珊瑚拼音 built from segments");
    Check(at(4).status == UserLexiconImportStatus::Repeated, "same phrase + pinyin in file is repeated");
    Check(at(5).pinyin == "chang'cheng" && at(5).source == UserLexiconPinyinSource::File, "tone digits stripped");
    Check(at(6).pinyin == "xi'an", "unsplit pinyin split by character count");
    Check(at(7).pinyin == "lv'se", "ü normalized to v");
    Check(at(8).status == UserLexiconImportStatus::Invalid, "syllable count mismatch rejected");
    Check(at(9).status == UserLexiconImportStatus::Invalid, "non-Han phrase rejected");
    Check(at(10).status == UserLexiconImportStatus::Invalid, "CSV with invalid full pinyin rejected");
    Check(at(11).status == UserLexiconImportStatus::Ready && at(11).lookupKey == "n'h'm" && at(11).pinyin == "ni'hao'ma",
        "CSV lookup key kept");

    std::cout << "commit\n";
    Check(!HasCandidate("shpy", "珊瑚拼音"), "jianpin candidate absent before import");
    const auto report = coral::runtime::commit_user_import(items);
    std::cout << "    imported=" << report.imported << " skipped=" << report.skipped << " failed=" << report.failed << "\n";
    Check(report.imported == 7 && report.skipped == 1 && report.failed == 3, "report counts");
    Check(HasCandidate("shanhupinyin", "珊瑚拼音"), "imported phrase is a candidate for full pinyin");
    Check(HasCandidate("shpy", "珊瑚拼音"), "imported phrase is a candidate for jianpin");
    Check(HasCandidate("nhm", "你好吗"), "CSV lookup key is searchable");

    std::cout << "re-prepare\n";
    auto again = items;
    Check(coral::runtime::prepare_user_import(again, {}), "prepare again");
    Check(again[2].status == UserLexiconImportStatus::Exists && again[0].status == UserLexiconImportStatus::Exists, "imported entries now exist");
    auto noSkip = items;
    UserLexiconImportOptions options;
    options.skipExisting = false;
    coral::runtime::prepare_user_import(noSkip, options);
    Check(noSkip[2].status == UserLexiconImportStatus::Ready, "skipExisting=false keeps existing entries importable");
    auto manual = std::vector<UserLexiconImportItem>{ Item(1, "珊瑚拼音") };
    options.autoPinyin = false;
    coral::runtime::prepare_user_import(manual, options);
    Check(manual[0].status == UserLexiconImportStatus::Invalid, "autoPinyin=false requires file pinyin");

    std::cout << "revert\n";
    Check(coral::runtime::revert_user_import(), "revert last import");
    Check(!HasCandidate("shpy", "珊瑚拼音"), "reverted phrase is gone");
    auto afterRevert = items;
    coral::runtime::prepare_user_import(afterRevert, {});
    Check(afterRevert[2].status == UserLexiconImportStatus::Ready, "reverted entries importable again");

    std::cout << "bulk\n";
    {
        coral::lexicon_io::ImportedText migration;
        coral::lexicon_io::parse_import_text(
            L"word\tpinyin\tsource_weight_raw_max\tsource_records\n珊瑚词库迁移\tshan hu ci ku qian yi\t999999\t3\n",
            coral::LanguageVariant::Simplified, migration);
        Check(migration.items.size() == 1, "migration TSV header excluded");
        Check(coral::runtime::prepare_user_import(migration.items, {}), "prepare migrated TSV");
        Check(migration.items[0].status == UserLexiconImportStatus::Ready &&
            migration.items[0].pinyin == "shan'hu'ci'ku'qian'yi", "migrated pinyin normalized by engine");
        const auto migrated = coral::runtime::commit_user_import(migration.items);
        Check(migrated.imported == 1 && HasCandidate("shanhucikuqianyi", "珊瑚词库迁移"),
            "migrated TSV reaches searchable user dictionary");
        coral::runtime::prepare_user_import(migration.items, {});
        Check(migration.items[0].status == UserLexiconImportStatus::Exists, "migrated entry duplicate detection");
        Check(coral::runtime::revert_user_import(), "migrated TSV import can be reverted");
        // 主词典可能临时组出相同长句；撤销以用户条目已删除、可重新导入为准。
        coral::runtime::prepare_user_import(migration.items, {});
        Check(migration.items[0].status == UserLexiconImportStatus::Ready,
            "reverted migration is absent from the user dictionary and importable again");
    }
    {
        // 常用字两两/三三组合：大部分不是词典词，走分段注音最慢的路径
        const std::string common = "的一是在不了有和人这中大为上个国我以要他时来用们生到作地于出就分对成会可主发年动同工也能下过子说产种面而方后多定行学法所民得经十三之进着等部度家电力里如水化高自二理起小物现实加量都两体制机当使点从业本去把性好应开它合还因由其些然前外天政四日那社义事平形相全表间样与关各重新线内数正心反你明看原又么利比或但质气第向道命此变条只没结解问意建月公无系军很情者最立代想已通并提直题党程展五果料象员革位入常文总次品式活设及管特件长求老头基资边流路级少图山统接知较将组见计别她手角期根论运农指几九区强放决西被干做必战先回则任取据处理府研";
        std::vector<std::string> chars;
        for (size_t i = 0; i < common.size(); i += 3) chars.push_back(common.substr(i, 3));
        std::vector<UserLexiconImportItem> bulk;
        std::uint32_t seed = 12345;
        const auto next = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % chars.size(); };
        for (int i = 0; i < 20000; ++i) {
            std::string phrase;
            const int length = 2 + i % 3;
            for (int c = 0; c < length; ++c) phrase += chars[next()];
            bulk.push_back(Item(i + 1, phrase));
        }
        const DWORD start = GetTickCount();
        coral::runtime::prepare_user_import(bulk, {});
        const DWORD prepared = GetTickCount();
        const auto bulkReport = coral::runtime::commit_user_import(bulk);
        const DWORD committed = GetTickCount();
        std::cout << "    20000 items: prepare " << (prepared - start) << " ms, commit " << (committed - prepared)
                  << " ms, imported=" << bulkReport.imported << " skipped=" << bulkReport.skipped << " failed=" << bulkReport.failed << "\n";
        Check(bulkReport.imported > 10000 && prepared - start < 15000 && committed - prepared < 15000, "bulk import within time budget");
        Check(coral::runtime::revert_user_import(), "bulk import reverted");
    }

    std::cout << (g_failures ? "LEXICON IMPORT FAIL\n" : "LEXICON IMPORT PASS\n");
    Finish(g_failures ? 1 : 0);
}

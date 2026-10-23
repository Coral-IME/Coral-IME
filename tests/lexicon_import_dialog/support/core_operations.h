#pragma once
// 内存引擎替身：用小型读音表代替主词库。
#include "coral/vocabulary/lexicon_import.h"

#include <map>
#include <set>
#include <string>
#include <vector>

struct TestEngine {
    bool ready = true;
    bool reverted = false;
    std::set<std::string> existing{ "北京" };
    std::map<std::string, std::string> readings{
        { "重", "chong" }, { "庆", "qing" }, { "北", "bei" }, { "京", "jing" }, { "你", "ni" },
        { "好", "hao" }, { "上", "shang" }, { "海", "hai" }, { "银", "yin" }, { "行", "hang" },
        { "长", "chang" }, { "城", "cheng" }, { "输", "shu" }, { "入", "ru" }, { "法", "fa" },
    };
    std::set<std::string> polyphones{ "行", "长", "重" };
    std::vector<UserLexiconImportItem> committed;
    int prepareCalls = 0;
};
inline TestEngine testEngine;

namespace coral::runtime {
inline std::vector<std::string> SplitChars(const std::string& text) {
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        out.push_back(text.substr(i, n));
        i += n;
    }
    return out;
}
inline bool prepare_user_import(std::vector<UserLexiconImportItem>& items, const UserLexiconImportOptions& options) {
    ++testEngine.prepareCalls;
    if (!testEngine.ready) return false;
    std::map<std::string, int> seen;
    for (auto& item : items) {
        item.status = UserLexiconImportStatus::Ready;
        item.polyphonic = false;
        item.message.clear();
        if (item.phrase.empty()) { item.status = UserLexiconImportStatus::Invalid; item.message = "未识别到词语"; continue; }
        const auto chars = SplitChars(item.phrase);
        if (!item.rawPinyin.empty()) {
            item.pinyin = item.rawPinyin;
            // 迁移工具使用空格分音节；替身与生产规范化保持这部分行为一致。
            std::replace(item.pinyin.begin(), item.pinyin.end(), ' ', '\'');
            item.source = UserLexiconPinyinSource::File;
            if (std::count(item.pinyin.begin(), item.pinyin.end(), '\'') + 1 != static_cast<long long>(chars.size())) {
                item.status = UserLexiconImportStatus::Invalid;
                item.message = "拼音与词语字数不符或含无效拼音：" + item.rawPinyin;
                continue;
            }
        } else if (options.autoPinyin) {
            item.pinyin.clear();
            item.source = UserLexiconPinyinSource::Generated;
            for (const auto& ch : chars) {
                const auto it = testEngine.readings.find(ch);
                if (it == testEngine.readings.end()) { item.pinyin.clear(); break; }
                if (!item.pinyin.empty()) item.pinyin += '\'';
                item.pinyin += it->second;
                if (testEngine.polyphones.count(ch)) item.polyphonic = true;
            }
            if (item.pinyin.empty()) {
                item.status = UserLexiconImportStatus::Invalid;
                item.source = UserLexiconPinyinSource::None;
                item.message = "无法为“" + item.phrase + "”生成拼音，请在文件中提供拼音";
                continue;
            }
        } else {
            item.status = UserLexiconImportStatus::Invalid;
            item.message = "缺少拼音（未开启自动补全拼音）";
            continue;
        }
        item.lookupKey = item.rawLookupKey.empty() ? item.pinyin : item.rawLookupKey;
        const std::string key = item.lookupKey + "|" + item.phrase;
        if (const auto it = seen.find(key); it != seen.end()) {
            item.status = UserLexiconImportStatus::Repeated;
            item.message = "与第 " + std::to_string(it->second) + " 行重复";
        } else if (options.skipExisting && testEngine.existing.count(item.phrase)) {
            item.status = UserLexiconImportStatus::Exists;
            item.message = "用户词库中已存在";
        } else {
            seen[key] = item.line;
        }
    }
    return true;
}
inline UserLexiconImportReport commit_user_import(const std::vector<UserLexiconImportItem>& items) {
    UserLexiconImportReport report;
    for (const auto& item : items) {
        if (item.status == UserLexiconImportStatus::Ready) {
            if (item.phrase == "输入法") {  // 模拟写入失败
                ++report.failed;
                report.failures.push_back({ item.line, "写入失败" });
                continue;
            }
            ++report.imported;
            testEngine.committed.push_back(item);
        } else if (item.status == UserLexiconImportStatus::Invalid) {
            ++report.failed;
        } else {
            ++report.skipped;
        }
    }
    return report;
}
inline bool revert_user_import() {
    testEngine.reverted = true;
    testEngine.committed.clear();
    return true;
}
}

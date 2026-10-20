#pragma once
#ifndef CORAL_MODULES_PHONETICS_PRONUNCIATION_LABELER_H_INCLUDED
#define CORAL_MODULES_PHONETICS_PRONUNCIATION_LABELER_H_INCLUDED

#include <unordered_map>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <limits>

// 注音结果结构
namespace coral::phonetics {
struct PronunciationResult {
    uint32_t index;
    std::string pinyin;
    int frequency;
    std::string word;
    std::string fullPinyin;
    double backprob;
    
    PronunciationResult() : index(UINT32_MAX), frequency(1100), backprob(0.000000) {}
};
} // 命名空间 coral::phonetics 结束（批次 3）


namespace coral::phonetics {
class PronunciationLabeler {
public:
    PronunciationLabeler();
    ~PronunciationLabeler();
    
    // 初始化，加载coral_lexicon.csv数据
    bool initialize(const std::string& lSortFile);
    
    // 为词语注音
    // annotateOnFullMatch: 完全命中时是否直接注音，true表示直接注音，false表示继续处理
    std::vector<coral::phonetics::PronunciationResult> annotate_word(const std::string& word, bool annotateOnFullMatch);
    
    // 判断是否为多音字
    bool is_duoyinzi(const std::string& charStr) const;
    
    // 检查是否已初始化
    bool is_initialized() const { return initialized_; }
    
private:
    // 加载coral_lexicon.csv数据
    bool load_l_sort(const std::string& filename);
    
    // 统一的内部辅助函数：处理多音字注音
    std::set<std::string> resolve_duoyinzi_internal(const std::string& word, int startPos);
    
    // 根据词语查询拼音
    std::set<std::string> get_pinyin_by_word(const std::string& word) const;
    
    // 解析CSV行
    std::vector<std::string> parse_csv_line(const std::string& line);
    
    // 词语 -> 所有拼音（存储所有读音）
    std::unordered_map<std::string, std::vector<std::string>> word_dict_all_;
    
    // 多音字集合（isPolyphonic=1的单字）
    std::set<std::string> duoyinzi_set_;
    
    bool initialized_;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // PINYINANNOTATOR_H

#include "pch.h"
#include "pronunciation_labeler.h"

namespace
{
	// 将拼音字符串转换为简拼形式
	std::string pinyinToSimple(const std::string& pinyin_str) {
		if (pinyin_str.empty()) {
			return "";
		}

		std::string result;
		for (size_t i = 0; i < pinyin_str.length(); ++i) {
			char current_char = pinyin_str[i];
			if (i == 0 || pinyin_str[i - 1] == '\'')
			{
				result += current_char;
			}
			else if (current_char == '\'')
			{
				result += current_char;
			}
		}
		return result;
	}

	// 添加全拼和简拼结果到结果列表
	void addFullAndSimpleResults(std::vector<coral::phonetics::PronunciationResult>& results, 
		const std::string& word, const std::string& fullPinyin) {
		std::string simplePinyin = pinyinToSimple(fullPinyin);
		
		// 全拼结果
		coral::phonetics::PronunciationResult annResultFull;
		annResultFull.index = UINT32_MAX;
		annResultFull.pinyin = fullPinyin;
		annResultFull.frequency = 1100;
		annResultFull.word = word;
		annResultFull.fullPinyin = fullPinyin;
		annResultFull.backprob = 0.000000;
		results.push_back(annResultFull);
		
		// 简拼结果
		coral::phonetics::PronunciationResult annResultSimple;
		annResultSimple.index = UINT32_MAX;
		annResultSimple.pinyin = simplePinyin;
		annResultSimple.frequency = 1100;
		annResultSimple.word = word;
		annResultSimple.fullPinyin = fullPinyin;
		annResultSimple.backprob = 0.000000;
		results.push_back(annResultSimple);
	}
}

coral::phonetics::PronunciationLabeler::PronunciationLabeler() : initialized_(false) {
}

coral::phonetics::PronunciationLabeler::~PronunciationLabeler() {
}

std::vector<std::string> coral::phonetics::PronunciationLabeler::parse_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (c == ',') {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(field); // 添加最后一列
    
    return fields;
}

bool coral::phonetics::PronunciationLabeler::load_l_sort(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open coral_lexicon.csv: " + filename);
        return false;
    }
    
    // 读取并处理表头（检查BOM标记）
    std::string line;
    if (!std::getline(file, line)) {
        LOG_ERROR("Failed to read header from coral_lexicon.csv");
        return false;
    }
    
    // 检查并跳过BOM标记
    if (line.length() >= 3 && 
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF) {
        line = line.substr(3);
    }
    
    // 解析表头并构建列索引映射
    std::vector<std::string> headers = parse_csv_line(line);
    std::unordered_map<std::string, int> headerMap;
    for (size_t i = 0; i < headers.size(); ++i) {
        std::string& header = headers[i];
        // 去除空白字符
        hlp::trimWhitespace(header);
        headerMap[header] = static_cast<int>(i);
    }
    
    // 获取列索引（使用find简化）
    auto getColIndex = [&headerMap](const std::string& name) -> int {
        auto it = headerMap.find(name);
        return (it != headerMap.end()) ? it->second : -1;
    };
    
    int wordCol = getColIndex("word");
    int fullPinyinCol = getColIndex("fullPinyin");
    int identifierCol = getColIndex("identifier");
    int isPolyphonicCol = getColIndex("isPolyphonic");
    
    if (wordCol < 0 || fullPinyinCol < 0) {
        LOG_ERROR("Required columns not found in CSV header");
        return false;
    }
    
    // 辅助函数：安全获取整数字段
    auto getIntField = [](const std::vector<std::string>& fields, int col, int defaultValue = 0) -> int {
        if (col < 0 || col >= static_cast<int>(fields.size())) return defaultValue;
        try {
            return std::stoi(fields[col]);
        } catch (...) {
            return defaultValue;
        }
    };
    
    // 辅助函数：安全获取字符串字段
    auto getStrField = [](const std::vector<std::string>& fields, int col, const std::string& defaultValue = "") -> std::string {
        if (col < 0 || col >= static_cast<int>(fields.size())) return defaultValue;
        return fields[col];
    };
    
    // 处理数据行
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> fields = parse_csv_line(line);
        if (fields.size() < headers.size()) continue;
        
        // 排除identifier=4的词条
        if (getIntField(fields, identifierCol) == 4) continue;
        
        // 获取必要字段
        const std::string& word = getStrField(fields, wordCol);
        if (word.empty()) continue;
        
        // 存储所有读音（用于支持一个词多个读音的情况）
        std::string fullPinyin = getStrField(fields, fullPinyinCol);
        if (!fullPinyin.empty()) {
            word_dict_all_[word].push_back(fullPinyin);
        }
        
        // 标记多音字（单字且isPolyphonic=1）
        if (hlp::utf8ToWstringSize(word) == 1 && getIntField(fields, isPolyphonicCol) == 1) {
            duoyinzi_set_.insert(word);
        }
    }
    
    LOG_INFO("Successfully loaded " + std::to_string(word_dict_all_.size()) + " words from coral_lexicon.csv");
    return true;
}

bool coral::phonetics::PronunciationLabeler::initialize(const std::string& lSortFile) {
    if (initialized_) {
        return true;
    }
    
    bool success = load_l_sort(lSortFile);
    if (success) {
        initialized_ = true;
    } else {
        LOG_ERROR("PinyinAnnotator initialization failed");
    }
    return success;
}

std::set<std::string> coral::phonetics::PronunciationLabeler::get_pinyin_by_word(const std::string& word) const {
    auto it = word_dict_all_.find(word);
    if (it == word_dict_all_.end()) {
        return std::set<std::string>();
    }
    
    // 提取所有拼音并自动去重
    std::set<std::string> pinyinSet(it->second.begin(), it->second.end());
    return pinyinSet;
}

bool coral::phonetics::PronunciationLabeler::is_duoyinzi(const std::string& charStr) const {
    return duoyinzi_set_.count(charStr) > 0;
}

// 直接返回多音字的所有读音，不进行复杂的上下文判断
std::set<std::string> coral::phonetics::PronunciationLabeler::resolve_duoyinzi_internal(const std::string& word, int startPos) {
    std::wstring wword = hlp::str2wstr(word);
    size_t word_size = wword.size();
    
    // 检查边界条件：统一进行范围检查，防止负数转换为极大正数
    if (startPos < 0 || static_cast<size_t>(startPos) >= word_size) {
        return std::set<std::string>();
    }
    size_t startPosSize = static_cast<size_t>(startPos);
    
    // 获取多音字字符
    std::wstring charWstr(1, wword[startPosSize]);
    std::string charStr = hlp::wstr2str(charWstr);
    
    // 使用主词典中该单字的拼音（已自动去重）
    std::set<std::string> pinyinSet = get_pinyin_by_word(charStr);
    if (!pinyinSet.empty()) {
        return pinyinSet;
    }
    
    // 最后的兜底
    return std::set<std::string>{"?"};
}


std::vector<coral::phonetics::PronunciationResult> coral::phonetics::PronunciationLabeler::annotate_word(const std::string& word, bool annotateOnFullMatch) {
    std::vector<coral::phonetics::PronunciationResult> results;
    
    if (!initialized_) {
        LOG_ERROR("PinyinAnnotator not initialized");
        return results;
    }
    
    if (word.empty()) {
        return results;
    }
    
    // 步骤1: 尝试完整匹配（全部命中优先）
    std::set<std::string> wordPinyins = get_pinyin_by_word(word);
    if (!wordPinyins.empty()) {
        // 根据 annotateOnFullMatch 参数决定是否直接注音
        if (annotateOnFullMatch) {
            // 遍历所有读音（已自动去重）
            for (const std::string& fullPinyin : wordPinyins) {
                addFullAndSimpleResults(results, word, fullPinyin);
            }
            
            return results;
        } else {
            // 如果 annotateOnFullMatch 为 false 且完全命中，直接返回空结果，不进行后续处理
            return results;
        }
    }
    
    std::wstring wword = hlp::str2wstr(word);
    int word_length = static_cast<int>(wword.size());
    
    // 使用map存储每个位置可能的拼音列表（支持多音字的多个读音）
    std::map<int, std::set<std::string>> charPinyinsMap;
    std::set<int> matchedPositions;
    
    // 步骤2: 识别所有多音字位置并处理
    std::vector<int> allDuoyinziPositions;
    for (int i = 0; i < word_length; ++i) {
        std::wstring charWstr(1, wword[i]);
        std::string charStr = hlp::wstr2str(charWstr);
        if (!charStr.empty() && is_duoyinzi(charStr)) {
            allDuoyinziPositions.push_back(i);
        }
    }
    
    // 处理所有多音字（直接返回所有可能的读音）
    for (int start : allDuoyinziPositions) {
        // 使用多音字注音逻辑（直接返回所有可能的读音，已包含兜底逻辑）
        charPinyinsMap[start] = resolve_duoyinzi_internal(word, start);
        matchedPositions.insert(start);
    }
    
    // 步骤3: 处理剩余的非多音字（直接单字注音）
    for (int i = 0; i < word_length; ++i) {
        if (matchedPositions.count(i) == 0) {
            // 使用项目提供的UTF-8处理接口：从wstring获取字符
            std::wstring charWstr(1, wword[i]);
            std::string charStr = hlp::wstr2str(charWstr);
            std::set<std::string> charPinyins = get_pinyin_by_word(charStr);
            if (!charPinyins.empty()) {
                charPinyinsMap[i] = charPinyins;
            } else {
                charPinyinsMap[i] = {"?"};
            }
        }
    }
    
    // 步骤4: 生成所有可能的拼音组合
    // 递归生成所有可能的组合（笛卡尔积）
    std::function<void(int, std::vector<std::string>&)> generateCombinations = 
        [&](int pos, std::vector<std::string>& current) {
            if (pos >= word_length) {
                // 生成一个完整的拼音组合
                std::string combinedFullPinyin;
                for (size_t i = 0; i < current.size(); ++i) {
                    if (i > 0) {
                        combinedFullPinyin += "'";
                    }
                    combinedFullPinyin += current[i];
                }
                
                // 添加全拼和简拼结果
                addFullAndSimpleResults(results, word, combinedFullPinyin);
                
                return;
            }
            
            // 尝试当前位置的所有可能拼音
            for (const auto& pinyin : charPinyinsMap[pos]) {
                current.push_back(pinyin);
                generateCombinations(pos + 1, current);
                current.pop_back();
            }
        };
    
    std::vector<std::string> current;
    generateCombinations(0, current);
    
    return results;
}

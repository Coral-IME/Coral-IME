#pragma once
#include <filesystem>
#include <unordered_map>
#include "candidate_result.h"
namespace coral::assist {
class SymbolHandler {
public:
    bool load(const std::filesystem::path& filePath);
    bool parse(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) const;
private:
    std::unordered_map<std::string, std::vector<std::string>> values_;
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


#include "symbol_handler.h"
#include <fstream>
#include <sstream>

bool coral::assist::SymbolHandler::load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return false;
    decltype(values_) values;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string key, value;
        if (!(fields >> key)) continue;
        auto& list = values[key];
        while (fields >> value) if (value != ":") list.push_back(std::move(value));
    }
    if (file.bad() || values.empty()) return false;
    values_ = std::move(values);
    return true;
}
bool coral::assist::SymbolHandler::parse(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) const {
    if (input.size() <= 2) return false;
    const auto found = values_.find(input.substr(2));
    if (found == values_.end()) return false;
    for (const auto& word : found->second) {
        coral::phonetics::CandidateResult s;
        s.sentence = word;
        s.pinyin.push_back(input);
        s.transform_flags = MiddlewareType::NoStoreUserLexicon;
        sents.push_back(std::move(s));
    }
    return !found->second.empty();
}

#include "coral/language_model/character_model.h"
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    coral::language_model::CharacterModel model;
    std::string error;
    if (!model.load_file(std::filesystem::u8path(argv[1]), error)) { std::cerr << error; return 1; }
    const bool batch = argc == 3 && std::string(argv[2]) == "--batch";
    if (batch) std::cerr << "weight_bytes=" << model.weight_bytes() << '\n';
    // 可选坏文件用于验证重载失败后仍保留旧模型。
    if (argc == 3 && !batch && model.load_file(std::filesystem::u8path(argv[2]), error)) return 3;
    std::cout << std::setprecision(12);
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto split = line.find('\t');
        if (split == std::string::npos) return 2;
        std::vector<double> scores;
        std::vector<std::string_view> texts;
        auto begin = split + 1;
        do {
            const auto end = batch ? line.find('\t', begin) : std::string::npos;
            texts.push_back(std::string_view(line).substr(begin, end == std::string::npos ? end : end-begin));
            if (end == std::string::npos) break;
            begin = end+1;
        } while (true);
        if (!model.score_candidates(line.substr(0, split), texts, scores)) return 4;
        for (size_t i = 0; i < scores.size(); ++i) std::cout << (i ? "\t" : "") << scores[i];
        std::cout << std::endl;
    }
    return 0;
}

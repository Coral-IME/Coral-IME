#include "coral/language_model/character_model.h"
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    coral::language_model::CharacterModel model;
    std::string error;
    if (!model.load_file(std::filesystem::u8path(argv[1]), error)) { std::cerr << error; return 1; }
    // 可选坏文件用于验证重载失败后仍保留旧模型。
    if (argc == 3 && model.load_file(std::filesystem::u8path(argv[2]), error)) return 3;
    std::cout << std::setprecision(12);
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto split = line.find('\t');
        if (split == std::string::npos) return 2;
        std::vector<double> scores;
        if (!model.score_candidates(line.substr(0, split), {std::string_view(line).substr(split + 1)}, scores)) return 4;
        std::cout << scores.front() << '\n';
    }
    return 0;
}

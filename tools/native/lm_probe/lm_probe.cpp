#include "coral/language_model/surface_model.h"
#include "coral/language_model/pair_bonus_model.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "Usage: coral_lm_probe MODEL LEXICON\n"; return 2; }
    std::ifstream input(argv[1], std::ios::binary);
    char magic[8]{};
    input.read(magic, sizeof(magic));
    if (input && std::string(magic, 8) == "CBGN0001") {
        coral::language_model::PairBonusModel pairs;
        std::string error;
        if (!pairs.load_file(argv[1], error)) { std::cerr << error << '\n'; return 1; }
        // 词面增益没有主词库绑定；实际词库缓存由引擎加载验证。
        if (!std::ifstream(argv[2], std::ios::binary)) return 1;
        std::cout << std::setprecision(17);
        std::string line;
        while (std::getline(std::cin, line)) {
            std::istringstream query(line);
            char operation;
            std::string previous, next, trailing;
            if (!(query >> operation >> std::quoted(previous) >> std::quoted(next)) ||
                operation != 'B' || (query >> trailing)) return 2;
            std::cout << pairs.bonus(pairs.word_state(previous), pairs.word_state(next)) << '\n';
        }
        return 0;
    }
    coral::language_model::SurfaceModel model;
    std::string error;
    if (!model.load_file(argv[1], argv[2], error)) { std::cerr << error << '\n'; return 1; }
    std::cout << std::setprecision(17);
    // 单行命令便于批量独立对照，不将词面或正文写入程序输出。
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        char operation;
        uint32_t context, item;
        std::string trailing;
        if (!(input >> operation >> context >> item) || (input >> trailing)) return 2;
        if (operation == 'S') std::cout << model.score_token(context, item) << '\n';
        else if (operation == 'P') {
            coral::language_model::PhraseScore score;
            if (!model.score_phrase(context, item, score)) std::cout << "unmapped\n";
            else std::cout << score.log_probability << ' ' << score.next_state << ' ' << score.first_arc_hit << '\n';
        } else return 2;
    }
    return 0;
}

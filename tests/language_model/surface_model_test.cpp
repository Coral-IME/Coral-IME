#include "coral/language_model/surface_model.h"
#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    coral::language_model::SurfaceModel model;
    std::string error;
    if (!model.load_file(argv[1], argv[2], error)) { std::cerr << error; return 1; }
    const auto close = [](double a, double b) { return std::abs(a - b) < 1e-12; };
    if (!close(model.score_token(0, 2), std::log(0.2)) ||
        !close(model.score_token(1, 2), std::log(0.6)) ||
        !close(model.score_token(1, 1), std::log(0.2))) return 3;
    coral::language_model::PhraseScore score, alias;
    if (!model.score_phrase(1, 10, score) || !model.score_phrase(1, 11, alias) ||
        !close(score.log_probability, alias.log_probability) || score.next_state != 3 ||
        !close(score.log_probability, std::log(0.6 * 0.1))) return 4;
    if (model.phrase_end(10) != 3 || model.phrase_end(99) != 0 ||
        model.score_phrase(1, 99, score)) return 5;
    // 对所有前文核对启发上界，包括正回退系数以及多词元的内部转移。
    for (uint32_t context = 0; context <= 3; ++context)
        for (uint32_t phrase : {10u, 11u, 12u}) {
            if (!model.score_phrase(context, phrase, score) ||
                model.phrase_upper_bound(phrase) + 1e-12 < score.log_probability) return 6;
        }
    const double before = model.score_token(1, 2);
    if (model.load_file(argv[3], argv[2], error) || !model.valid() ||
        !close(model.score_token(1, 2), before)) return 7;
    if (std::isfinite(model.score_token(4, 1)) || std::isfinite(model.score_token(0, 0))) return 8;
    return 0;
}

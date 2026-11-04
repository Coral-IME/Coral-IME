#include "coral/language_model/candidate_ranker.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    using namespace coral::language_model;
    if (argc == 2 && std::string(argv[1]) == "--features") {
        CandidateRankingInput a, b;
        a.base = -1; a.neural = -4; a.legacy = -.9; a.characters = 4; a.letters = 8;
        a.path.words = 2; a.path.single_words = 1; a.path.longest_word = 3; a.path.pair_available = true;
        b = a; b.base = -1.1; b.neural = -3; b.legacy = -.8;
        const auto f = make_ranking_features({a,b}, 3);
        write_ranking_trace(std::cout, "a\tb", "前\n文", 0, "候选", f[0]);
        write_ranking_trace(std::cout, "a\tb", "前\n文", 1, "其他", f[1]);
        return 0;
    }
    if (argc != 2 && argc != 3) return 1;
    CandidateRanker model;
    std::string error;
    if (!model.load_file(std::filesystem::u8path(argv[1]), error)) { std::cerr << error; return 2; }
    const bool benchmark = argc == 3 && std::string(argv[2]) == "--benchmark";
    if (argc == 3 && !benchmark && model.load_file(std::filesystem::u8path(argv[2]), error)) return 4;
    std::cout << std::setprecision(17);
    std::vector<RankingFeatures> batch;
    for (std::string line; std::getline(std::cin,line);) {
        if (line.empty()) continue;
        std::istringstream input(line);
        RankingFeatures features{};
        for (double& value : features) {
            std::string token;
            if (!(input >> token)) return 3;
            try { std::size_t used; value = std::stod(token,&used); if (used != token.size()) return 3; }
            catch (...) { return 3; }
        }
        std::string extra;
        if (input >> extra) return 3;
        if (benchmark) { batch.push_back(features); continue; }
        double score;
        if (model.predict(features,score)) std::cout << score << '\n';
        else std::cout << "error\n";
    }
    if (benchmark) {
        if (batch.empty() || batch.size() > 8192) return 3;
        const size_t group_size = std::min(size_t{8}, batch.size());
        if (batch.size() % group_size) return 3;
        const size_t groups = batch.size() / group_size;
        std::vector<double> timings;
        double checksum = 0;
        for (int repeat = 0; repeat < 10020; ++repeat) {
            const auto start = std::chrono::steady_clock::now();
            const size_t offset = (static_cast<size_t>(repeat) % groups) * group_size;
            for (size_t i = 0; i < group_size; ++i) {
                double score;
                if (!model.predict(batch[offset + i], score)) return 3;
                checksum += score;
            }
            const double microseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            if (repeat >= 20) timings.push_back(microseconds);
        }
        std::sort(timings.begin(), timings.end());
        std::cout << "{\"candidates\":" << group_size << ",\"groups\":" << groups << ",\"repeats\":10000,\"p50_us\":" << timings[5000]
            << ",\"p95_us\":" << timings[9500] << ",\"p99_us\":" << timings[9900] << ",\"checksum\":" << checksum << "}\n";
    }
    return 0;
}

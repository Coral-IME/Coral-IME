#include "correction_path.h"
#include <iostream>
#include <functional>
#include <random>
#include <stdexcept>

using coral::phonetics::detail::CorrectionPathEdge;
using coral::phonetics::detail::solve_correction_path;
using Graph = std::vector<std::vector<CorrectionPathEdge>>;

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void check_invalid(CorrectionPathEdge invalid, const char* message) {
    Graph edges(6);
    edges[0].push_back(std::move(invalid));
    std::vector<std::string> sequences{"unchanged"};
    int types = 19;
    const auto result = solve_correction_path(edges, "nihao", sequences, types);
    require(result.first.empty() && sequences == std::vector<std::string>{"unchanged"} && types == 19, message);
}
}

int main() {
    try {
        check_invalid({-1, "bad", 5, {0, -1}, 1}, "negative target rejected");
        check_invalid({6, "bad", 5, {0, 6}, 1}, "oversized target rejected");
        check_invalid({0, "bad", 5, {0, 0}, 1}, "initial self-loop rejected");
        check_invalid({5, "bad", 5, {-1, 5}, 1}, "negative substring start rejected");
        check_invalid({5, "bad", 5, {0, 6, 5}, 1}, "oversized substring start rejected");
        check_invalid({5, "bad", 5, {0, 3, 2, 5}, 1}, "reversed substring interval rejected");
        check_invalid({5, "bad", 5, {0, 4}, 1}, "endpoint mismatch rejected");
        check_invalid({5, "bad", 5, {}, 1}, "missing boundaries rejected");
        check_invalid({5, "bad", std::numeric_limits<float>::infinity(), {0, 5}, 1}, "nonfinite weight rejected");
        {
            Graph edges(6);
            edges[0].push_back({2, "ni", -1, {0, 2}, 0});
            // 旧递推会把 from[2] 改为 2，随后从终点还原时无法前进。
            edges[2].push_back({2, "a", 1, {2, 2}, 1});
            edges[2].push_back({0, "back", 9, {2, 0}, 1});
            edges[2].push_back({5, "hao", -1, {2, 5}, 1});
            std::vector<std::string> sequences;
            int types = 0;
            auto result = solve_correction_path(edges, "nihao", sequences, types);
            require(result.first == "nihao" && result.second == -2 && types == 1 &&
                sequences == std::vector<std::string>{"ni", "hao"}, "positive self-loop and backward edge cannot poison reconstruction");
        }
        // 手写原输入字节边界：覆盖开头、末尾插入/删除及重复字母。
        const std::vector<std::pair<std::string, std::vector<int>>> cases{
            {"nihao", {0, 0, 2, 5}}, {"nihao", {0, 2, 5, 5}},
            {"nnihao", {0, 3, 6}}, {"nihaoo", {0, 2, 6}},
            {"nihao", {0, 2, 2, 5}}, {"nihao", {0, 2, 5}}
        };
        for (const auto& item : cases) {
            Graph edges(item.first.size() + 1);
            edges[0].push_back({static_cast<int>(item.first.size()), "corrected", -3, item.second, 1});
            std::vector<std::string> sequences;
            int types = 0;
            const auto result = solve_correction_path(edges, item.first, sequences, types);
            std::string joined;
            for (const auto& sequence : sequences) joined += sequence;
            require(result.first == "corrected" && joined == item.first &&
                sequences.size() + 1 == item.second.size(), "valid correction preserves exact original input coverage");
        }
        {
            Graph edges(6);
            edges[0].push_back({5, "ordinary", 1, {0, 5}, 0});
            std::vector<std::string> sequences{"keep"};
            int types = 7;
            require(solve_correction_path(edges, "nihao", sequences, types).first.empty() &&
                sequences == std::vector<std::string>{"keep"} && types == 7, "uncorrected path preserves outputs");
        }
        // 小图穷举全部向前路径，独立核对分数选择，兼容负权与正权词条。
        std::mt19937 random(20261020);
        for (int sample = 0; sample < 128; ++sample) {
            const int length = 1 + random() % 8;
            Graph edges(length + 1);
            edges[0].push_back({length, "direct", -3.f, {0, length}, 1});
            for (int source = 0; source < length; ++source) {
                for (int to = source + 1; to <= length; ++to) {
                    if (random() % 3 == 0) edges[source].push_back({to, "part", static_cast<float>(static_cast<int>(random() % 9) - 4), {source, to}, 1});
                }
            }
            float expected = -std::numeric_limits<float>::infinity();
            std::function<void(int, float)> enumerate = [&](int source, float score) {
                if (source == length) {
                    expected = std::max(expected, score);
                    return;
                }
                for (const auto& edge : edges[source]) enumerate(edge.to, score + edge.weight);
            };
            enumerate(0, 0.f);
            for (int source = 0; source <= length; ++source) {
                edges[source].push_back({source, "self", 99.f, {source, source}, 1});
                edges[source].push_back({length + 1, "outside", 99.f, {source, length + 1}, 1});
            }
            std::vector<std::string> sequences;
            int types = 0;
            const std::string original(length, 'a');
            const auto result = solve_correction_path(edges, original, sequences, types);
            std::string joined;
            for (const auto& sequence : sequences) joined += sequence;
            require(result.second == expected && !result.first.empty() && joined == original, "path score matches exhaustive oracle with malicious edge noise");
        }
        std::cout << "Correction path boundary cases passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#pragma once
#ifndef CORAL_MODULES_PHONETICS_CORRECTION_PATH_H_INCLUDED
#define CORAL_MODULES_PHONETICS_CORRECTION_PATH_H_INCLUDED

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace coral::phonetics::detail {
struct CorrectionPathEdge {
    int to = 0;
    std::string phrase;
    float weight = 0.f;
    std::vector<int> boundaries;
    int correction_types = 0;
};

// 纠错图的顶点是原输入的字节边界；只接受向前消耗输入的边。
inline std::pair<std::string, float> solve_correction_path(
    const std::vector<std::vector<CorrectionPathEdge>>& edges,
    const std::string& original, std::vector<std::string>& sequences, int& correction_types) {
    const auto length = original.size();
    if (length == 0 || edges.size() != length + 1) return {"", 0.f};
    const float unreachable = -std::numeric_limits<float>::infinity();
    std::vector<float> scores(edges.size(), unreachable);
    std::vector<std::size_t> previous(edges.size(), edges.size());
    std::vector<const CorrectionPathEdge*> selected(edges.size(), nullptr);
    scores[0] = 0;
    for (std::size_t source = 0; source < edges.size(); ++source) {
        if (scores[source] == unreachable) continue;
        for (const auto& edge : edges[source]) {
            if (edge.to < 0 || static_cast<std::size_t>(edge.to) <= source ||
                static_cast<std::size_t>(edge.to) > length || !std::isfinite(edge.weight) ||
                edge.boundaries.size() < 2 || edge.boundaries.front() < 0 ||
                static_cast<std::size_t>(edge.boundaries.front()) != source ||
                edge.boundaries.back() != edge.to) continue;
            bool valid = true;
            for (std::size_t i = 1; i < edge.boundaries.size(); ++i) {
                if (edge.boundaries[i] < edge.boundaries[i - 1] ||
                    static_cast<std::size_t>(edge.boundaries[i]) > length) {
                    valid = false;
                    break;
                }
            }
            if (!valid) continue;
            const float score = scores[source] + edge.weight;
            if (std::isfinite(score) && score > scores[edge.to]) {
                scores[edge.to] = score;
                previous[edge.to] = source;
                selected[edge.to] = &edge;
            }
        }
    }
    if (scores[length] == unreachable) return {"", 0.f};
    std::vector<std::string> phrases, restored;
    int types = 0;
    for (auto current = length; current != 0; current = previous[current]) {
        // 严格递减既限定还原步数，也拒绝自环和损坏的前驱。
        if (previous[current] >= current || !selected[current]) return {"", 0.f};
        const auto& edge = *selected[current];
        phrases.push_back(edge.phrase);
        for (auto i = edge.boundaries.size(); i > 1; --i) {
            const auto left = static_cast<std::size_t>(edge.boundaries[i - 2]);
            const auto right = static_cast<std::size_t>(edge.boundaries[i - 1]);
            restored.push_back(original.substr(left, right - left));
        }
        types |= edge.correction_types;
    }
    if (types == 0) return {"", 0.f};
    std::reverse(restored.begin(), restored.end());
    std::string sentence;
    for (auto it = phrases.rbegin(); it != phrases.rend(); ++it) sentence += *it;
    sequences = std::move(restored);
    correction_types = types;
    return {sentence, scores[length]};
}
}
#endif

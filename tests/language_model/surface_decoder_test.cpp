#include "pch.h"
#include "candidate_decoder.h"
#include "lexicon_catalog.h"
#include <functional>
#include <iostream>
#include <random>

namespace {
struct Edge { int from, to; uint32_t id; float fallback; };
const char* word(uint32_t id) {
    switch (id) {
    case 10: return "怎么了";
    case 12: return "在";
    case 13: return "再";
    case 14: return "它";
    default: return "甲";
    }
}
// 固定夹具的手写概率，独立于资源读者与解码器。
double token_log(uint32_t previous, uint32_t token) {
    const double initial[]{0, .7, .2, .1}, lower[]{0, .6, .3, .1};
    if (previous == 0) return std::log(initial[token]);
    if (previous == 1) return std::log(token == 1 ? .2 : 2 * lower[token]);
    if (previous == 3) return std::log(token == 2 ? .6 : (.4/.7) * lower[token]);
    return std::log(lower[token]);
}
uint32_t last(uint32_t id) { return id == 10 ? 3 : id >= 12 && id <= 14 ? id - 11 : 0; }
double step(uint32_t previous, const Edge& edge) {
    if (!last(edge.id)) return edge.fallback;
    if (edge.id == 10) return token_log(previous, 2) + token_log(2, 3);
    return token_log(previous, last(edge.id));
}
double decode(const std::vector<Edge>& edges, int length, uint32_t condition,
    float influence, const coral::phonetics::ContextScorer& model, std::string* first = nullptr,
    int penalty_mode = 0) {
    coral::phonetics::support::ParseArenaScope arena;
    coral::phonetics::LexiconCatalog lexicon;
    coral::phonetics::CandidateLattice lattice;
    const std::string input(length, 'a');
    lattice.init_graph(length + 1, input, lexicon);
    for (const auto& edge : edges) {
        coral::phonetics::PhoneticSequence sequence({"a"}, {}, edge.from, edge.to, 3, false, false);
        sequence.is_fuzzy_match_ = penalty_mode == 1 || penalty_mode == 2;
        if (penalty_mode == 2) sequence.sub_sequences_.push_back("a");
        sequence.is_edit_corrected_ = penalty_mode == 3;
        coral::phonetics::ParsePhrases phrases;
        phrases.emplace_back(word(edge.id), "a", edge.fallback, edge.id, 0.0f);
        bool hit = false, english = false, span = false;
        if (!lattice.process_phrases(phrases, sequence, lexicon, false, 25, length + 1,
            false, {}, input, hit, english, span)) throw std::runtime_error("Cannot create fixture lattice");
    }
    coral::phonetics::CandidateDecoder decoder;
    decoder.set_condition_id(condition, influence);
    if (!decoder.init(lattice, model)) throw std::runtime_error("Cannot initialize decoder");
    vector<coral::phonetics::CandidateResult> solutions;
    coral::phonetics::ParseTextSet seen;
    float shortest = kMaxWeight;
    if (!decoder.get_parsed_res(solutions, {}, seen, {}, shortest)) throw std::runtime_error("No decoded path");
    if (first) *first = solutions.front().sentence;
    return shortest;
}
double enumerate(const std::vector<Edge>& edges, int length, uint32_t condition, double influence) {
    double best = std::numeric_limits<double>::infinity();
    std::function<void(int,uint32_t,bool,double)> visit = [&](int at, uint32_t context, bool pending, double cost) {
        if (at == length) { best = std::min(best, cost); return; }
        for (const auto& edge : edges) {
            if (edge.from != at) continue;
            double weight = step(context, edge);
            if (pending && last(edge.id)) weight = step(0, edge) + influence * (weight - step(0, edge));
            visit(edge.to, last(edge.id), false, cost - weight);
        }
    };
    const auto context = influence > 0 ? last(condition) : 0;
    visit(0, context, context != 0, 0);
    return best;
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    coral::phonetics::ContextScorer model;
    std::string error;
    if (!model.load_surface_model(argv[1], argv[2], error)) { std::cerr << error; return 1; }
    if (std::abs(model.get_bigram(12, 13) - std::log(.6)) > 1e-6) return 3;
    std::mt19937 generator(7919);
    for (int trial = 0; trial < 80; ++trial) {
        const int length = 2 + trial % 4;
        std::vector<Edge> edges;
        for (int at = 0; at < length; ++at) {
            for (uint32_t id : {12u,13u,14u,99u})
                if (id == 12 || generator() % 3) edges.push_back({at, at + 1, id, -float(.1 + generator()%200/10.)});
            if (at + 2 <= length) edges.push_back({at, at + 2, 10, -.01f});
        }
        const uint32_t condition = trial % 2 ? 12 : kInvalidPhraseId;
        const float influence = trial % 3 == 0 ? 0.f : trial % 3 == 1 ? .7f : 1.f;
        const double actual = decode(edges, length, condition, influence, model);
        const double expected = enumerate(edges, length, condition, influence);
        if (std::abs(actual - expected) > 2e-5) {
            std::cerr << "DAG mismatch " << trial << ' ' << actual << ' ' << expected; return 4;
        }
    }
    std::string first;
    decode({{0,1,12,-.001f},{0,1,13,-12.f}}, 1, 12, 1.f, model, &first);
    if (first != "再") { std::cerr << "Direct ranking ignored model"; return 5; }
    const double unknown = decode({{0,1,99,-.15f},{1,2,13,-12.f}}, 2, 12, .7f, model);
    if (std::abs(unknown - (.15 - std::log(.2))) > 1e-5) return 6;
    for (int mode : {1,2,3}) {
        const double penalty = mode == 1 ? kFuzzyPenaltyQuan : mode == 2 ? kFuzzyPenaltyJian : kEditPenalty;
        const double cost = decode({{0,1,12,-.001f}},1,kInvalidPhraseId,1.f,model,nullptr,mode);
        if (std::abs(cost - (-std::log(.7)-penalty)) > 1e-5) return 7;
    }
    std::cout << "80 DAG cases, direct ranking, unknown boundary, and context checks passed\n";
    return 0;
}

#include "pch.h"
#include "candidate_decoder.h"
#include "lexicon_catalog.h"
#include "coral/language_model/pair_bonus_model.h"
#include <functional>
#include <iostream>
#include <random>

namespace {
struct Edge { int from, to; uint32_t id; float weight; };
std::string_view word(uint32_t id) {
    switch (id) { case 12: return "甲"; case 13: return "乙"; case 14: return "丙";
        case 15: return "甲乙"; default: return "丁"; }
}
double decode(const std::vector<Edge>& edges, int length, uint32_t condition, float influence,
    const coral::phonetics::ContextScorer& model, std::string* first = nullptr) {
    using namespace coral::phonetics;
    support::ParseArenaScope arena;
    LexiconCatalog lexicon;
    CandidateLattice lattice;
    const std::string input(length, 'a');
    lattice.init_graph(length + 1, input, lexicon);
    for (int i = 0; i <= length; ++i) lattice.set_input_offset(i, i);
    for (const auto& edge : edges) {
        const auto reading = std::string_view(input).substr(edge.from, edge.to - edge.from);
        PhoneticSequence sequence({reading}, {}, edge.from, edge.to, 3, false, false);
        ParsePhrases phrases;
        phrases.emplace_back(word(edge.id), std::string(reading), edge.weight, edge.id, 0.f);
        bool hit = false, english = false, span = false;
        if (!lattice.process_phrases(phrases, sequence, lexicon, false, 25, length + 1,
            false, {}, input, hit, english, span)) throw std::runtime_error("Cannot build bonus fixture");
    }
    CandidateDecoder decoder;
    decoder.set_condition_id(condition, influence);
#if CORAL_ENABLE_CHARACTER_MODEL
    decoder.set_smart_candidates(false);
#endif
    if (!decoder.init(lattice, model)) throw std::runtime_error("Cannot initialize bonus decoder");
    vector<CandidateResult> results;
    ParseTextSet seen;
    float cost = kMaxWeight;
    if (!decoder.get_parsed_res(results, {}, seen, {}, cost)) throw std::runtime_error("No bonus path");
    if (first) *first = results.front().sentence;
    return cost;
}
}

int main(int argc, char** argv) {
    if (argc != 7) return 2;
    try {
        using namespace coral::language_model;
        using namespace coral::phonetics;
        PairBonusModel table;
        std::string error;
        if (!table.load_file(argv[1], error)) throw std::runtime_error(error);
        const auto a = table.word_state("甲"), b = table.word_state("乙");
        if (table.bonus(a, b) != 8 || table.bonus(b, a) != 0 || table.bonus(0, b) != 0) return 3;
        const auto count = table.pair_count();
        if (table.load_file(argv[2], error) || table.pair_count() != count || table.bonus(a,b) != 8) return 4;
        PairBonusModel deleted;
        if (!deleted.load_file(argv[6],error) || deleted.bonus(deleted.word_state("甲"),deleted.word_state("乙")) != 0 ||
            deleted.bonus(deleted.word_state("甲"),deleted.word_state("甲")) != 1 ||
            deleted.bonus(deleted.word_state("乙"),deleted.word_state("丙")) != 2 ||
            deleted.bonus(deleted.word_state("甲乙"),deleted.word_state("丙")) != 1) return 11;
        ContextScorer model, zero, plain, renumbered;
        if (!model.load_pair_bonus_model(argv[1], argv[4], error) ||
            !zero.load_pair_bonus_model(argv[3], argv[4], error) ||
            !renumbered.load_pair_bonus_model(argv[1], argv[5], error)) throw std::runtime_error(error);
        if (zero.pair_bonus_model() || zero.surface_model() || zero.gbin()) return 5;
        if (model.pair_bonus_state(12) != a || renumbered.pair_bonus_state(112) != a ||
            renumbered.pair_bonus_state(12) != 0) return 6;
        if (model.load_pair_bonus_model(argv[2], argv[4], error) || model.pair_bonus_state(12) != a) return 7;
        std::mt19937 random(20261030);
        const auto expected_bonus = [](uint32_t previous, uint32_t next) {
            if (previous == 12 && next == 13) return 8.;
            if (previous == 12 && next == 12) return 1.;
            if (previous == 13 && next == 14) return 2.;
            if (previous == 15 && next == 14) return 1.;
            if (previous == 0 && next == 13) return .5;
            return 0.;
        };
        for (int trial = 0; trial < 180; ++trial) {
            const int length = 4 + random() % 3;
            std::vector<Edge> edges;
            for (int from = 0; from < length; ++from) {
                for (int size = 1; size <= 2 && from + size <= length; ++size) {
                    for (int k = 0; k < 2; ++k) {
                        const uint32_t id = random() % 5 == 0 ? 99 : 12 + random() % 4;
                        edges.push_back({from, from + size, id, -.1f * float(1 + random() % 35)});
                    }
                }
            }
            const uint32_t condition = trial % 3 ? 12 : kInvalidPhraseId;
            const float influence = trial % 2 ? .7f : 0.f;
            double expected = std::numeric_limits<double>::infinity();
            std::function<void(int,uint32_t,bool,double)> visit = [&](int at, uint32_t previous, bool first, double cost) {
                if (at == length) { expected = std::min(expected, cost); return; }
                for (const auto& edge : edges) if (edge.from == at) {
                    const bool external = first && condition != kInvalidPhraseId && influence > 0;
                    const double reward = expected_bonus(first ? (external ? condition : 0) : previous, edge.id);
                    // 未知词之后只是断开上下文，不能误当作新的句首。
                    const double actual = !first && previous == 99 ? 0 : reward;
                    visit(edge.to, edge.id, false, cost - edge.weight - (external ? influence : 1.) * actual);
                }
            };
            visit(0,0,true,0);
            const auto actual = decode(edges,length,condition,influence,model);
            if (std::abs(actual - expected) > 1e-4) {
                std::cerr << "Bonus DAG mismatch " << trial << ' ' << actual << ' ' << expected; return 8;
            }
            std::string baseline, empty;
            const auto base = decode(edges,length,condition,influence,plain,&baseline);
            const auto off = decode(edges,length,condition,influence,zero,&empty);
            if (base != off || baseline != empty) return 9;
        }
        // 整词内部不拆分计奖；完整词库候选继续由基础权重决定。
        std::string first;
        decode({{0,1,12,-.1f},{0,1,13,-2.f}},1,12,1.f,model,&first);
        if (first != "甲") return 10;
        {
            // 原前缀稳定机制必须叠加在新奖励路径上，五字内不罚，强证据仍可翻转。
            PrefixStability history, independent;
            history.prepare("", "aaaa", 0);
            history.observe("乙丁甲乙", "a'a'a'a");
            const auto& anchors = history.prepare("", "aaaaa", 0);
            if (anchors.size() != 2 || !independent.prepare("", "aaaaaa", 0).empty()) return 12;
            const auto edges = [](int length, float gap) {
                std::vector<Edge> result{{0,1,12,-1.f},{0,1,13,-1.f-gap}};
                for (int i=1;i<length;++i) {
                    const uint32_t id = i == 1 ? 99 : i == 2 ? 12 : i == 3 ? 13 : 99;
                    result.push_back({i,i+1,id,-1.f});
                }
                return result;
            };
            {
                PrefixStabilityScope scope(anchors);
                decode(edges(5,.7f),5,kInvalidPhraseId,1.f,model,&first);
                if (first != "甲丁甲乙丁") return 13;
                decode(edges(6,.7f),6,kInvalidPhraseId,1.f,model,&first);
                if (first != "乙丁甲乙丁丁") return 14;
                decode(edges(6,4.f),6,kInvalidPhraseId,1.f,model,&first);
                if (first != "甲丁甲乙丁丁") return 15;
            }
            // 没有该会话的历史时，同一新模型仍选择基础证据更强的首词。
            decode(edges(6,.7f),6,kInvalidPhraseId,1.f,model,&first);
            if (first != "甲丁甲乙丁丁") return 16;
            history.reset();
            if (!history.prepare("", "aaaaaa", 0).empty()) return 17;
        }
        std::cout << "180 exhaustive DAG checks, empty-table replay, dictionary replacement and reload checks passed\n";
        return 0;
    } catch (const std::exception& failure) { std::cerr << failure.what(); return 1; }
}

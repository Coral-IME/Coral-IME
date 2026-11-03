#include "pch.h"
#include "candidate_decoder.h"
#include "lexicon_catalog.h"
#include "coral/language_model/character_rerank.h"
#include <functional>
#include <iostream>
#include <random>
#include <future>
#include <thread>

namespace {
struct Edge { int from, to; uint32_t id; float fallback; };
const char* word(uint32_t id) {
    switch (id) {
    case 10: return "怎么了";
    case 12: return "在";
    case 13: return "再";
    case 14: return "它";
    case 15: return "再再";
    case 16: return "在在";
    case 17: return "在在在在";
    case 18: return "再在在在";
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
    int penalty_mode = 0, bool smart_candidates = true) {
    coral::phonetics::support::ParseArenaScope arena;
    coral::phonetics::LexiconCatalog lexicon;
    coral::phonetics::CandidateLattice lattice;
    const std::string input(length, 'a');
    lattice.init_graph(length + 1, input, lexicon);
    for (const auto& edge : edges) {
        const auto reading = std::string_view(input).substr(edge.from, edge.to - edge.from);
        coral::phonetics::PhoneticSequence sequence({reading}, {}, edge.from, edge.to, 3, false, false);
        sequence.is_fuzzy_match_ = penalty_mode == 1 || penalty_mode == 2;
        if (penalty_mode == 2) sequence.sub_sequences_.push_back("a");
        sequence.is_edit_corrected_ = penalty_mode == 3;
        coral::phonetics::ParsePhrases phrases;
        phrases.emplace_back(word(edge.id), std::string(reading), edge.fallback, edge.id, 0.0f);
        bool hit = false, english = false, span = false;
        if (!lattice.process_phrases(phrases, sequence, lexicon, false, 25, length + 1,
            false, {}, input, hit, english, span)) throw std::runtime_error("Cannot create fixture lattice");
    }
    coral::phonetics::CandidateDecoder decoder;
    decoder.set_condition_id(condition, influence);
#if CORAL_ENABLE_CHARACTER_MODEL
    decoder.set_smart_candidates(smart_candidates);
#endif
    if (!decoder.init(lattice, model)) throw std::runtime_error("Cannot initialize decoder");
    vector<coral::phonetics::CandidateResult> solutions;
    coral::phonetics::ParseTextSet seen;
    float shortest = kMaxWeight;
    if (!decoder.get_parsed_res(solutions, {}, seen, {}, shortest)) throw std::runtime_error("No decoded path");
    if (first) *first = solutions.front().sentence;
    return shortest;
}
struct TerminalEdge { int from, to; std::string_view word, reading; float weight; };
std::pair<std::string,double> terminal_decode(std::string_view input,
    const std::vector<TerminalEdge>& edges, float bonus = 1.0f, int correction = 0) {
    using namespace coral::phonetics;
    support::ParseArenaScope arena;
    LexiconCatalog lexicon;
    ContextScorer model;
    CandidateLattice lattice;
    lattice.init_graph(static_cast<int>(input.size()+1), input, lexicon);
    for (const auto& edge : edges) {
        ParseTextViews readings;
        for (size_t at = 0; at < edge.reading.size();) {
            const auto end = edge.reading.find('\'', at);
            const auto size = (end == std::string_view::npos ? edge.reading.size() : end) - at;
            readings.push_back(edge.reading.substr(at, size)); at += size + 1;
        }
        PhoneticSequence sequence(readings, {}, edge.from, edge.to, 3, false, false);
        sequence.is_fuzzy_match_ = correction == 1;
        sequence.is_edit_corrected_ = correction == 2;
        ParsePhrases phrases;
        phrases.emplace_back(edge.word, std::string(edge.reading), edge.weight, 100, 0.f);
        phrases.back().is_corrected = correction == 3;
        bool hit=false, english=false, span=false;
        if (!lattice.process_phrases(phrases, sequence, lexicon, false, 25,
            static_cast<int>(input.size()+1), false, {}, std::string(input), hit, english, span))
            throw std::runtime_error("Cannot build terminal fixture");
    }
    CandidateDecoder decoder;
    decoder.set_terminal_bonus(bonus);
#if CORAL_ENABLE_CHARACTER_MODEL
    decoder.set_smart_candidates(false);
#endif
    if (!decoder.init(lattice, model)) throw std::runtime_error("Cannot initialize terminal fixture");
    vector<CandidateResult> results; ParseTextSet seen; float cost=kMaxWeight;
    if (!decoder.get_parsed_res(results, {}, seen, {}, cost)) throw std::runtime_error("No terminal result");
    return {results.front().sentence, cost};
}

void check_terminal_bonus() {
    const std::pair<std::string_view,std::string_view> alternatives[]{
        {"啊","阿"},{"吗","马"},{"吧","把"},{"呢","讷"},{"呀","压"},{"啦","拉"}};
    const std::string_view readings[]{"a","ma","ba","ne","ya","la"};
    for (size_t i=0;i<6;++i) {
        const int end=static_cast<int>(readings[i].size());
        const std::vector<TerminalEdge> edges{{0,end,alternatives[i].first,readings[i],-.7f},
            {0,end,alternatives[i].second,readings[i],-.1f}};
        if (terminal_decode(readings[i],edges).first!=alternatives[i].first ||
            terminal_decode(readings[i],edges,0).first!=alternatives[i].second)
            throw std::runtime_error("Terminal direct reward mismatch");
    }
    const std::vector<TerminalEdge> split{{0,2,"你","ni",-.1f},{2,5,"好","hao",-.1f},
        {5,7,"吧","ba",-.2f},{5,7,"把","ba",-.1f}};
    const std::vector<TerminalEdge> compound{{0,2,"你","ni",-.1f},
        {2,7,"好吧","hao'ba",-.3f},{2,7,"好把","hao'ba",-.2f}};
    const auto a=terminal_decode("nihaoba",split), b=terminal_decode("nihaoba",compound);
    if (a.first!="你好吧" || b.first!=a.first || std::abs(a.second+.6)>1e-5 ||
        std::abs(b.second-a.second)>1e-5 || terminal_decode("nihaoba",split,0).first!="你好把")
        throw std::runtime_error("Terminal search reward or segmentation mismatch");
    const auto full=terminal_decode("nihaoba",{{0,7,"你好吧","ni'hao'ba",-.4f},
        {0,7,"你好把","ni'hao'ba",-.3f}});
    if (full.first!=a.first || std::abs(full.second-a.second)>1e-5)
        throw std::runtime_error("Terminal full dictionary reward mismatch");
    if (terminal_decode("bani",{{0,2,"吧","ba",-.2f},{0,2,"把","ba",-.1f},
            {2,4,"你","ni",-.1f}}).first!="把你" ||
        terminal_decode("b",{{0,1,"吧","b",-.2f},{0,1,"把","b",-.1f}}).first!="把" ||
        terminal_decode("ni",{{0,2,"呢","ni",-.2f},{0,2,"泥","ni",-.1f}}).first!="泥" ||
        terminal_decode("ba",{{0,2,"吧","ba",-2.f},{0,2,"把","ba",-.1f}}).first!="把")
        throw std::runtime_error("Terminal scope or bounded reward mismatch");
    for (int mode=1;mode<=3;++mode)
        if (terminal_decode("nihaoba",split,1,mode).first!="你好把")
            throw std::runtime_error("Corrected arc received terminal reward");
    if (terminal_decode("niba",{{0,2,"你","ni",-.1f},{2,4,"ba","ba",-.2f},
            {2,4,"把","ba",-.1f}}).first!="你把")
        throw std::runtime_error("Non-Chinese terminal received reward");
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
    try { check_terminal_bonus(); } catch (const std::exception& e) { std::cerr<<e.what(); return 58; }
    if (argc == 5 && std::string(argv[4]) == "reading-priors") {
        coral::phonetics::ContextScorer pronunciation_model;
        std::string error;
        if (!pronunciation_model.load_surface_model(argv[1], argv[2], error)) { std::cerr << error; return 30; }
        // 手写的九比一读音分布，与输入法候选文字及主词典检索别名数量无关。
        if (std::abs(pronunciation_model.reading_log_prior(12) - std::log(.9)) > 2e-6 ||
            std::abs(pronunciation_model.reading_log_prior(13) - std::log(.1)) > 2e-6 ||
            pronunciation_model.reading_log_prior(14) != 0) return 31;
        const double common = decode({{0,1,12,-1.f},{1,2,14,-1.f}},2,kInvalidPhraseId,1.f,pronunciation_model);
        const double rare = decode({{0,1,13,-1.f},{1,2,14,-1.f}},2,kInvalidPhraseId,1.f,pronunciation_model);
        if (std::abs(common + std::log(.7 * .9 * .2)) > 2e-5 ||
            std::abs(rare + std::log(.2 * .1 * .1)) > 2e-5) return 32;
        // 失败重载保留已有模型及读音先验，不能出现新旧资源混用。
        if (pronunciation_model.load_surface_model(argv[3], argv[2], error) ||
            std::abs(pronunciation_model.reading_log_prior(13) - std::log(.1)) > 2e-6) return 33;
        std::string first;
        decode({{0,1,12,-.01f},{0,1,13,-.02f}},1,kInvalidPhraseId,1.f,pronunciation_model,&first);
        if (first != "在") return 34;
        std::cout << "reading priors passed\n"; return 0;
    }
    if (argc != 3 && argc != 4) return 2;
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
        // 全句精确命中直接采用词库权重；其余图继续以穷举检查语言模型搜索。
        double expected = enumerate(edges, length, condition, influence);
        for (const auto& edge : edges)
            if (edge.from == 0 && edge.to == length) expected = -edge.fallback;
        if (std::abs(actual - expected) > 2e-5) {
            std::cerr << "DAG mismatch " << trial << ' ' << actual << ' ' << expected; return 4;
        }
    }
    std::string first;
    decode({{0,1,12,-.001f},{0,1,13,-12.f}}, 1, 12, 1.f, model, &first);
    if (first != "在") { std::cerr << "Direct dictionary ranking was overridden by model"; return 5; }
    // 未加载任何统计模型时，完整词库命中也必须能直接产生结果。
    coral::phonetics::ContextScorer empty_model;
    decode({{0,1,12,-.001f},{0,1,13,-12.f}},1,12,1.f,empty_model,&first);
    if (first != "在") return 15;
    decode({{0,1,kInvalidPhraseId,-.001f}},1,12,1.f,empty_model,&first);
    if (first != "甲") { std::cerr << "User dictionary hit without primary ID was lost"; return 17; }
    const double unknown = decode({{0,1,99,-.15f},{1,2,13,-12.f}}, 2, 12, .7f, model);
    if (std::abs(unknown - (.15 - std::log(.2))) > 1e-5) return 6;
    for (int mode : {1,2,3}) {
        const double penalty = mode == 1 ? kFuzzyPenaltyQuan : mode == 2 ? kFuzzyPenaltyJian : kEditPenalty;
        const double cost = decode({{0,1,12,-.001f}},1,kInvalidPhraseId,1.f,model,nullptr,mode);
        if (std::abs(cost - (-std::log(.7)-penalty)) > 1e-5) return 7;
    }
#if CORAL_ENABLE_CHARACTER_MODEL
    if (argc == 4) {
        const std::vector<Edge> alternatives{{0,1,12,-1.f},{0,1,13,-1.f},{1,2,12,-1.f}};
        if (!model.load_character_model(argv[3],0,error)) return 8;
        decode(alternatives,2,kInvalidPhraseId,1.f,model,&first);
        if (first != "在在") return 9;
        if (!model.load_character_model(argv[3],1,error)) return 10;
        // 四字词库直出必须压过模型更偏好的同音词及可用的多弧组合路径。
        decode({{0,4,17,-2.f},{0,4,18,-2.1f},{0,1,13,-.01f},
                {1,2,12,-.01f},{2,3,12,-.01f},{3,4,12,-.01f}},
            4,kInvalidPhraseId,1.f,model,&first);
        if (first != "在在在在") { std::cerr << "Dictionary hit entered neural or composed ranking"; return 16; }
        decode(alternatives,2,kInvalidPhraseId,1.f,model,&first);
        if (first != (CORAL_CHARACTER_RERANK_THRESHOLD < 2 ? "再在" : "在在")) return 11;
        // 默认 N=2 时分别验证两字跳过、三字和四字重排；同时覆盖自定义 N 的相邻边界。
        const std::vector<int> lengths{2,3,4,CORAL_CHARACTER_RERANK_THRESHOLD,
            CORAL_CHARACTER_RERANK_THRESHOLD + 1};
        for (int length : lengths) {
            if (length < 1) continue;
            std::vector<Edge> edges{{0,1,12,-1.f},{0,1,13,-1.f}};
            for (int at = 1; at < length; ++at) edges.push_back({at,at+1,12,-1.f});
            decode(edges,length,kInvalidPhraseId,1.f,model,&first);
            std::string expected = length > 1 && length > CORAL_CHARACTER_RERANK_THRESHOLD ? "再" : "在";
            for (int at = 1; at < length; ++at) expected += "在";
            if (first != expected) { std::cerr << "Neural character boundary mismatch: " << length; return 12; }
        }
        // 同一份已加载模型在关闭状态下只使用基础组句；重新开启才恢复神经选优。
        const int long_length = std::max(4, CORAL_CHARACTER_RERANK_THRESHOLD + 1);
        std::vector<Edge> long_edges{{0,1,12,-1.f},{0,1,13,-1.f}};
        for (int at = 1; at < long_length; ++at) long_edges.push_back({at,at+1,12,-1.f});
        {
            std::vector<Edge> many_paths;
            for (int at = 0; at < 4; ++at)
                for (auto id : {12u,13u,14u,99u}) many_paths.push_back({at,at+1,id,-1.f-float(id)*.0001f});
            coral::language_model::CharacterRerankBatch batch;
            coral::language_model::CharacterRerankScope capture(batch);
            decode(many_paths,4,kInvalidPhraseId,1.f,model,&first);
            if (CORAL_CHARACTER_RERANK_PATHS > 1 && CORAL_CHARACTER_RERANK_THRESHOLD < 4 &&
                (batch.empty() || batch.front().texts.size() != size_t(CORAL_CHARACTER_RERANK_PATHS))) {
                std::cerr << "Neural shortlist ignored its configured limit"; return 26;
            }
        }
        {
            using namespace coral::language_model;
            CharacterRerankBatch captured;
            {
                CharacterRerankScope capture(captured);
                decode(long_edges,long_length,kInvalidPhraseId,1.f,model,&first);
            }
            std::string baseline;
            for (int i = 0; i < long_length; ++i) baseline += "在";
            if (first != baseline || captured.empty()) { std::cerr << "Capture must return baseline without scoring"; return 21; }
            std::promise<void> entered, release;
            auto released = release.get_future().share();
            AsyncCharacterReranker worker([&] { entered.set_value(); released.wait(); });
            auto obsolete = worker.submit(captured);
            entered.get_future().wait();
            auto replaced = worker.submit(captured);
            auto latest = worker.submit(captured);
            release.set_value();
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            bool done = false, succeeded = false;
            CharacterRerankBatch completed;
            while (!done && std::chrono::steady_clock::now() < until) {
                {
                    std::lock_guard<std::mutex> lock(latest->mutex);
                    done = latest->finished; succeeded = latest->succeeded;
                    if (done) completed = latest->batch;
                }
                if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            worker.stop();
            if (!obsolete->cancelled || !replaced->cancelled || !done || !succeeded) {
                std::cerr << "Latest-only worker or shutdown failed"; return 22;
            }
            {
                CharacterRerankScope replay(static_cast<const CharacterRerankBatch&>(completed));
                decode(long_edges,long_length,kInvalidPhraseId,1.f,model,&first);
                if (!replay.replayed_all() || first != "再" + baseline.substr(std::string("在").size())) {
                    std::cerr << "Async scores changed decoder result"; return 23;
                }
            }
            std::vector<double> scores{123};
            int checks = 0;
            if (model.character_model()->score_candidates("", {"在在在在", "再在在在"}, scores,
                [&] { return ++checks >= 3; }) || scores != std::vector<double>{123}) {
                std::cerr << "Cancellation published partial scores"; return 24;
            }
            // 不同上下文不能误用旧分数，也不能在重放范围内退回同步推理。
            CharacterRerankScope replay(static_cast<const CharacterRerankBatch&>(completed));
            if (CharacterRerankScope::score(model.shared_character_model(), "甲", {"在在在在", "再在在在"}, scores) || replay.replayed_all()) return 25;
        }
        for (bool enabled : {false, true, false}) {
            decode(long_edges,long_length,kInvalidPhraseId,1.f,model,&first,0,enabled);
            std::string expected = enabled ? "再" : "在";
            for (int at = 1; at < long_length; ++at) expected += "在";
            if (first != expected) { std::cerr << "Runtime neural gate ignored"; return 18; }
        }
        if (CORAL_CHARACTER_RERANK_THRESHOLD == 3) {
            // 三字参考首选旁边即使有可加分的四字路径，仍须整组保持原排序。
            decode({{0,1,12,-1.f},{0,1,15,-1.8f},{1,2,12,-1.f},{2,3,12,-1.f}},
                3,kInvalidPhraseId,1.f,model,&first);
            if (first != "在在在") { std::cerr << "Short reference triggered neural reranking"; return 13; }
            // 长参考首选允许重排，但更短的三字路径不能获得神经加分。
            decode({{0,1,16,-1.f},{0,1,13,-1.f},{0,1,15,-2.6f},{1,2,16,-1.f}},
                2,kInvalidPhraseId,1.f,model,&first);
            if (first != "再再在在") { std::cerr << "Short alternative affected neural ranking"; return 14; }
        }
    }
#endif
#if CORAL_ENABLE_CHARACTER_MODEL
    if (argc == 4) {
        // 即使环境指定了有效模型，关闭后也不保留权重或加载文件。
        _putenv_s("CORAL_CHARACTER_MODEL", argv[3]);
        model.configure_character_model({}, false);
        if (model.character_model() || model.character_weight() != 0) return 19;
        model.configure_character_model({});
        if (!model.character_model()) return 20;
    }
#endif
    std::cout << "80 lattices, dictionary bypass, neural boundaries, runtime gate and context checks passed\n";
    return 0;
}

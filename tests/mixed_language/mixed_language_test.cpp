#include "pch.h"
#include "syllable_parser.h"
#include "lexicon_table.h"
#include "runtime_context.h"
#include <fstream>
#include <iostream>
#include <cstdlib>

namespace {
int failures = 0;
void expect(bool value, const char* label) {
    if (!value) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
void write(const std::filesystem::path& path, const std::string& value) {
    std::ofstream(path, std::ios::binary) << value;
}
// 完整词条的存在不能让新概率模型丢失内部组词弧；旧入口仍保留原默认策略。
void check_full_hit_composition(const std::filesystem::path& root) {
    const auto dir = root / "pruning";
    std::filesystem::create_directories(dir);
    write(dir / "l.csv", "pinyin,frequency,word,fullPinyin\n"
        "shan'lin,90000000,山林,shan'lin\nshan,1000,山,shan\nlin,1000,林,lin\n");
    write(dir / "m.csv", "prob,prev_word,next_word,prev_fullPinyin,next_fullPinyin\n-0.2,山,林,shan,lin\n");
    expect(build_resource_tables(dir / "l.csv", dir / "m.csv", dir / "mainlexicon.coral",
        dir / "languagemodel.coral"), "pruning fixture build");
    coral::phonetics::LexiconLocations paths;
    paths.primaryMarisa.path = dir / "primary.marisa";
    paths.primaryData.path = dir / "primary.dat";
    paths.versionId = "pruning-test-" + std::to_string(GetCurrentProcessId());
    coral::phonetics::LexiconCatalog lexicon;
    expect(lexicon.load_primary_lexicon(paths), "pruning fixture load");
    for (bool allow : {true, false}) {
        coral::phonetics::support::ParseArenaScope arena;
        coral::phonetics::SyllableLattice graph;
        coral::phonetics::CandidateLattice lattice;
        coral::phonetics::ParseSequences sequences;
        const syllable_strview_map syllables{{"shan", kFullCost}, {"lin", kFullCost}};
        bool full = false, hit = false;
        expect(graph.get_pinyin_sequences("shanlin", syllables, 30, sequences, full, false,
            lattice, lexicon, hit, {}, {}, false, allow), "pruning fixture segmented");
        const auto& arcs = lattice.get_arc_array();
        expect(std::any_of(arcs.begin(), arcs.end(), [](const auto& arc) {
            return arc.from == 0 && arc.data.phrase == u8"山林";
        }), "full word remains in both policies");
        const bool interior = std::any_of(arcs.begin(), arcs.end(), [](const auto& arc) {
            return arc.from > 0 && arc.data.phrase == u8"林";
        });
        expect(interior == !allow, "probability policy retains an otherwise pruned composition path");
    }
}
std::vector<std::string> decoded_candidates(const std::string& input, coral::phonetics::LexiconCatalog& lexicon, const coral::phonetics::ContextScorer& model) {
    coral::phonetics::support::ParseArenaScope arena;
    coral::phonetics::SyllableLattice graph;
    coral::phonetics::CandidateLattice lattice;
    coral::phonetics::ParseSequences sequences;
    syllable_strview_map syllables{{"yong", kFullCost}, {"xie", kFullCost}, {"ai", kFullCost},
        {"ti", kFullCost}, {"me", kFullCost}, {"time", kFullCost},
        {"chi", kFullCost}, {"na", kFullCost}};
    bool full = false, hit = false;
    const std::unordered_set<std::string_view> fuzzy;
    expect(graph.get_pinyin_sequences(input, syllables, 30, sequences, full, false,
        lattice, lexicon, hit, fuzzy, {}, false), "mixed boundaries survive segmentation");
    coral::phonetics::CandidateDecoder decoder;
    expect(decoder.init(lattice, model), "decoder init");
    vector<coral::phonetics::CandidateResult> solutions;
    coral::phonetics::ParseTextSet seen;
    float shortest = kMaxWeight;
    expect(decoder.get_parsed_res(solutions, syllables, seen, sequences, shortest), "decoder result");
    std::vector<std::string> result;
    for (const auto& solution : solutions) result.push_back(solution.sentence);
    return result;
}

std::string parse(const std::string& input, coral::phonetics::LexiconCatalog& lexicon, const coral::phonetics::ContextScorer& model) {
    const auto result = decoded_candidates(input, lexicon, model);
    return result.empty() ? std::string{} : result.front();
}

std::vector<std::string> candidates(const std::string& input, coral::phonetics::LexiconCatalog& lexicon,
                                    const coral::phonetics::ContextScorer& model) {
    coral::phonetics::support::ParseArenaScope arena;
    PinyinParseContext context;
    context.pinyin = context.englishPinyin = input;
    const syllable_strview_map syllables{{"xie", kFullCost}};
    const std::unordered_set<std::string_view> fuzzy;
    expect(context.syllableGraph.get_pinyin_sequences(input, syllables, 30, context.pinyinSequences,
        context.isFullPinyinMatch, false, context.wordLattice, lexicon, context.sentHitDict,
        fuzzy, {}, false), "candidate policy segmentation");
    expect(context.languageDecoder.init(context.wordLattice, model), "candidate policy decoder");
    expect(coral::phonetics::SyllableParser::instance().get_parsed_result(context), "candidate policy final assembly");
    std::vector<std::string> result;
    for (const auto& solution : context.sents) result.push_back(solution.sentence);
    return result;
}

// 使用人工词格核对边界折减，不评测正式词库的准确率。
void check_condition_weight(coral::phonetics::LexiconCatalog& lexicon,
    const coral::phonetics::ContextScorer& model)
{
    const auto* table = model.gbin();
    if (!table) { expect(false, "condition fixture model exists"); return; }
    const float writing = table->uni()[2].weight;
    const float using_weight = table->uni()[0].weight;
    struct Token { std::string_view word, pinyin; uint32_t id; float weight; };
    const auto score = [&](std::initializer_list<Token> tokens, uint32_t condition, float influence) {
        coral::phonetics::support::ParseArenaScope arena;
        std::string input, sentence;
        for (const auto& token : tokens) { input += token.pinyin; sentence += token.word; }
        coral::phonetics::CandidateLattice lattice;
        const int vertices = static_cast<int>(input.size()) + 1;
        lattice.init_graph(vertices, input, lexicon);
        int from = 0;
        for (const auto& token : tokens) {
            const int to = from + static_cast<int>(token.pinyin.size());
            coral::phonetics::PhoneticSequence sequence({token.pinyin}, {}, from, to, 3, false, false);
            coral::phonetics::ParsePhrases phrases;
            phrases.emplace_back(token.word, token.pinyin, token.weight, token.id, 0.0f);
            bool hit = false, english = false, span = false;
            expect(lattice.process_phrases(phrases, sequence, lexicon, false, 25, vertices,
                false, {}, std::string(token.pinyin), hit, english, span), "condition fixture arc");
            from = to;
        }
        coral::phonetics::CandidateDecoder decoder;
        decoder.set_condition_id(condition, influence);
        expect(decoder.init(lattice, model), "condition fixture decoder init");
        vector<coral::phonetics::CandidateResult> solutions;
        coral::phonetics::ParseTextSet seen;
        float shortest = kMaxWeight;
        expect(decoder.get_parsed_res(solutions, {}, seen, {}, shortest), "condition fixture decoded");
        const auto found = std::find_if(solutions.begin(), solutions.end(),
            [&](const auto& candidate) { return candidate.sentence == sentence; });
        expect(found != solutions.end(), "condition fixture candidate exists");
        return found == solutions.end() ? 0.0 : static_cast<double>(found->probability) * (1 + input.size());
    };
    const Token write_token{"写", "xie", 2, writing};
    const Token use_token{"用", "yong", 0, using_weight};
    const Token unknown{"前", "qian", kInvalidPhraseId, -4.0f};
    const double ordinary = score({write_token}, kInvalidPhraseId, 1.0f);
    const double full = score({write_token}, 0, 1.0f);
    const double reduced = score({write_token}, 0, 0.7f);
    expect(std::abs(ordinary - writing) < 1e-5, "no condition retains unigram score");
    expect(std::abs(full - writing) < 1e-5, "full dictionary hit bypasses condition model");
    expect(std::abs(score({write_token}, 0, 0.0f) - ordinary) < 1e-5,
        "zero percent equals no external condition");
    expect(std::abs(reduced - ordinary) < 1e-5,
        "full dictionary hit keeps its own weight at every condition strength");
    expect(std::abs(score({use_token, write_token}, 2, 0.7f) - (using_weight - 0.01)) < 1e-5,
        "internal bigram stays at full strength");
    expect(std::abs(score({unknown, write_token}, 0, 0.7f) - (-4.0 + 0.3 * writing - 0.007)) < 1e-5,
        "unknown prefix cannot bypass condition damping");
    expect(std::abs(score({unknown, write_token}, 0, 0.0f) - (-4.0 + ordinary)) < 1e-5,
        "zero percent clears condition across unknown prefix");
}

void expect_no_short_mixed_english(const std::string& key, const std::string& display,
    coral::phonetics::LexiconCatalog& lexicon, const coral::phonetics::ContextScorer& model) {
    for (const auto& input : {key + "xie", "yong" + key, "yong" + key + "xie",
                              "yong'" + key + "'xie"}) {
        const auto result = decoded_candidates(input, lexicon, model);
        expect(std::none_of(result.begin(), result.end(), [&](const auto& word) {
            return word.find(display) != std::string::npos;
        }), "short English is absent at every mixed-input position");
    }
    // 同时演练显式英文序列与旧的中文未命中
    // 回退路径，包括绕过 is_word_in_lexicon 的两字母词。
    for (const bool english : {false, true}) {
        coral::phonetics::support::ParseArenaScope arena;
        coral::phonetics::CandidateLattice lattice;
        const std::string input = key + "xie";
        lattice.init_graph(static_cast<int>(input.size() + 1), input, lexicon);
        coral::phonetics::PhoneticSequence sequence({key}, {}, 0,
            static_cast<int>(key.size()), 3, english, false);
        bool sentence_hit = false, english_hit = false, span_hit = false;
        expect(lattice.process_sequence(sequence, lexicon, {}, false,
            sentence_hit, english_hit, span_hit), "short English fallback remains parseable");
        expect(!english_hit, "short English fallback cannot report a dictionary hit");
        const auto& arcs = lattice.get_arc_array();
        expect(std::none_of(arcs.begin(), arcs.end(), [&](const auto& arc) {
            return arc.data.phrase == display || arc.data.pinyin_sequence().is_match_english_vocab_;
        }), "short English cannot enter through a fallback arc");
    }
}
}

int main() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("coral-mixed-test-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    write(dir / "l.csv", "pinyin,frequency,word,fullPinyin\n"
        "yong,1000000,用,yong\nyong,3000000,拥,yong\n"
        "xie,1000000,写,xie\nxie,5000000,鞋,xie\n"
        "ti'me,5000,Time,ti'me\nt'i'm'e,9000,time,t'i'm'e\n"
        "ai,100000000,爱,ai\n");
    write(dir / "eng_lexicon.csv", "search_word,display_word,frequency,can_split\npython,Python,6770,0\ntime,time,26,1\nai,AI,26,1\n"
        "prefixa,prefixa,1,0\nprefixb,prefixb,1,0\nprefixc,prefixc,1,0\nprefixd,prefixd,1,0\n"
        "prefixe,prefixe,1,0\nprefixf,prefixf,1,0\nprefixg,prefixg,1,0\nprefixh,prefixh,1,0\n"
        "prefixi,prefixi,1,0\nprefixj,prefixj,1,0\nprefixk,prefixk,1,0\nprefixz,prefixz,900000,0\n"
        "iphone,iPhone,6770,0\nc++,C++,26,1\ncicd,ci/cd,26,0\n"
        "gb,GB,422708,1\napi,API,50000,0\nchina,China,26,0\n");
    write(dir / "m.csv", "prob,prev_word,next_word,prev_fullPinyin,next_fullPinyin\n"
        "-0.1,用,python,yong,python\n-0.1,PYTHON,写,python,xie\n-0.01,用,写,yong,xie\n");
    expect(build_resource_tables(dir / "l.csv", dir / "m.csv", dir / "mainlexicon.coral",
        dir / "languagemodel.coral", dir / "eng_lexicon.csv"), "unified model build");
    coral::phonetics::LatinLexicon english;
    expect(english.build_lexicon(dir / "eng_lexicon.csv", dir / "english.marisa", dir / "english.dat"), "English build");
    // 模拟英文 id 字段意外保存中文 ID 的旧词包。
    {
        std::fstream data(dir / "english.dat", std::ios::in | std::ios::out | std::ios::binary);
        coral::storage::PhraseArchiveHeader header;
        data.read(reinterpret_cast<char*>(&header), sizeof(header));
        std::vector<coral::storage::PhraseArchiveIndex> offsets(header.outer_size);
        data.read(reinterpret_cast<char*>(offsets.data()), offsets.size() * sizeof(coral::storage::PhraseArchiveIndex));
        const size_t base = sizeof(header) + offsets.size() * sizeof(coral::storage::PhraseArchiveIndex);
        const size_t total = offsets.back().begin + offsets.back().count;
        const uint32_t unsafeId = 0;
        for (size_t i = 0; i < total; ++i) {
            data.seekp(base + i * sizeof(coral::storage::PhraseRecord) + offsetof(coral::storage::PhraseRecord, id));
            data.write(reinterpret_cast<const char*>(&unsafeId), sizeof(unsafeId));
        }
    }
    coral::phonetics::LexiconLocations paths;
    paths.primaryMarisa.path = dir / "primary.marisa";
    paths.primaryData.path = dir / "primary.dat";
    paths.englishMarisa.path = dir / "english.marisa";
    paths.englishData.path = dir / "english.dat";
    paths.englishMarisa.identifier = hlp::calculateFileMD5(paths.englishMarisa.path.string());
    paths.englishData.identifier = hlp::calculateFileMD5(paths.englishData.path.string());
    paths.userLexiconFile = dir / "userlexicon.coral";
    paths.versionId = "mixed-test-" + std::to_string(GetCurrentProcessId());
    auto& pinyin = coral::phonetics::SyllableParser::instance();
    auto& lexicon = pinyin.get_lexicon_manager();
    expect(lexicon.load_primary_lexicon(paths), "primary load");
    expect(lexicon.load_pinyin_model(paths, {}), "English/user load");
    coral::phonetics::LexiconPhrase info;
    expect(lexicon.get_eng_word_info("TIME", info) && info.id == 4 &&
        std::abs(info.phrase_weight - std::log(9000.f * kInvMaxcount)) < 1e-5, "shared ID and authoritative weight");
    expect(lexicon.get_eng_word_info("Python", info) && info.id == 7, "English-only compact ID");
    coral::phonetics::ContextScorer model;
    expect(model.load_g_bin_only({dir / "languagemodel.coral", {}}, paths.versionId), "model load");
    expect(model.get_bigram(0, 7) == -0.1f && model.get_bigram(7, 2) == -0.1f, "both language transitions resolve");
    auto config = std::make_shared<coral::EngineConfig>();
    config->pinyin_correction = false;
    coral::Engine::compatInstance().config().update(config);
    check_condition_weight(lexicon, model);
    expect(parse("yongpythonxie", lexicon, model) == "用Python写", "context defeats higher-frequency Chinese homophones");
    expect(parse("yongPythonxie", lexicon, model) == "用Python写", "capitalized mixed input");
    expect(parse("pythonxie", lexicon, model) == "Python写", "English at sentence start");
    expect(parse("yongpython", lexicon, model) == "用Python", "English at sentence end");
    expect(parse("ai", lexicon, model) == "爱", "Chinese competes with short English");
    expect(parse("python", lexicon, model) == "Python", "complete English token");
    expect(parse("PYTHON", lexicon, model) == "PYTHON", "uppercase exact token");
    expect(parse("iPhone", lexicon, model) == "iPhone", "camel-case technical word");
    expect(parse("cicd", lexicon, model) == "ci/cd", "display punctuation is independent of lookup spelling");
    expect(parse("gb", lexicon, model) == "GB", "standalone two-letter English remains available");
    {
        const auto result = candidates("api", lexicon, model);
        expect(std::find(result.begin(), result.end(), "API") != result.end(),
            "standalone three-letter English remains available");
    }
    expect_no_short_mixed_english("gb", "GB", lexicon, model);
    expect_no_short_mixed_english("api", "API", lexicon, model);
    expect_no_short_mixed_english("cicd", "ci/cd", lexicon, model);
    expect(parse("yongchinaxie", lexicon, model).find("China") != std::string::npos,
        "five-letter English remains available in mixed input");
    expect(parse("yong'python'xie", lexicon, model) == "用Python写", "explicit boundaries");
    {
        coral::phonetics::support::ParseArenaScope arena;
        coral::phonetics::ParsePhrases completions;
        lexicon.get_english_phrases("prefix", completions, 10);
        expect(!completions.empty() && completions[0].phrase == "prefixz", "frequency-ranked completion before truncation");
    }
    for (bool show : {false, true}) {
        config->isShowInfos = show;
        coral::Engine::compatInstance().config().update(config);
        coral::phonetics::support::ParseArenaScope arena;
        vector<coral::phonetics::CandidateResult> solutions;
        coral::phonetics::ParseTextSet seen;
        pinyin.add_english_lexicon_results(solutions, "pyth", false, kMaxWeight, seen);
        expect(!solutions.empty() && solutions.front().sentence == "Python" &&
            solutions.front().probability < 0.f, "completion independent of debug display");
    }
    {
        const auto simplified = coral::LanguageVariant::Simplified;
        auto& userData = lexicon.get_user_lexicon();
        auto result = candidates("xie", lexicon, model);
        expect(!result.empty() && result.front() == u8"鞋", "ordinary candidates precede pinning");
        expect(userData.update_user_lexicon("xie", u8"写", simplified), "learn before candidate rules");
        const auto historyCount = userData.history_entries().size();
        const auto history_sequence = userData.history_sequence();
        expect(userData.add_phrase_shortcut("xie", u8"常用语"), "custom phrase rule");
        expect(userData.pin_candidate_first("xie", u8"写", "xie", simplified), "pin to first");
        result = candidates("xie", lexicon, model);
        expect(result.size() >= 2 && result[0] == u8"写" && result[1] == u8"常用语",
            "pinned candidate precedes custom phrases");
        expect(userData.pin_candidate_first("xie", u8"鞋", "xie", simplified), "replace first pin");
        result = candidates("xie", lexicon, model);
        expect(!result.empty() && result.front() == u8"鞋", "replacement is first");
        expect(pinyin.process_candidate_action("xie", u8"鞋", "xie",
            coral::CandidateAction::UnpinFirst, simplified), "unpin explicit action");
        result = candidates("xie", lexicon, model);
        expect(!result.empty() && result.front() == u8"常用语" &&
            std::find(result.begin(), result.end(), u8"鞋") != result.end(),
            "unpin restores ordinary candidate and preserves custom phrase");
        expect(!pinyin.process_candidate_action("xie", u8"鞋", "xie",
            coral::CandidateAction::Disable, simplified), "single character cannot be disabled");
        result = candidates("xie", lexicon, model);
        expect(std::find(result.begin(), result.end(), u8"鞋") != result.end(), "single character remains visible");
        expect(userData.add_phrase_shortcut("xie", u8"词语"), "multi-character candidate fixture");
        expect(pinyin.process_candidate_action("xie", u8"词语", "xie",
            coral::CandidateAction::Disable, simplified), "disable word action");
        result = candidates("xie", lexicon, model);
        expect(std::find(result.begin(), result.end(), u8"词语") == result.end(), "disabled word filtered");
        expect(userData.disable_candidate("xie", u8"常用语", "xie", coral::LanguageVariant::Traditional),
            "independent traditional disable");
        result = candidates("xie", lexicon, model);
        expect(std::find(result.begin(), result.end(), u8"常用语") != result.end(),
            "traditional disable does not filter simplified candidate");
        expect(userData.history_entries().size() == historyCount && userData.history_sequence() == history_sequence,
            "manual rules leave learned history untouched");
        expect(userData.remove_phrase_shortcut("xie", u8"常用语"), "remove custom phrase");
    }
    // 旧词包没有英文模型词表。其按旧词频形状生成的
    // id 不得被意外解析，且其词元必须打断中文邻接关系。
    const auto legacyDir = dir / "legacy";
    std::filesystem::create_directories(legacyDir);
    // 普通中文读音可能优于精确英文区间。在只保留一条
    // 中文组串句子时，最优混合句子仍必须可被选中。
    // 本夹具不含有利的英文二元组。
    std::ofstream(dir / "l.csv", std::ios::binary | std::ios::app)
        << "chi'na,100000000,吃拿,chi'na\n";
    expect(build_resource_tables(dir / "l.csv", dir / "m.csv", legacyDir / "mainlexicon.coral",
        legacyDir / "languagemodel.coral"), "legacy build");
    paths.primaryMarisa.path = legacyDir / "primary.marisa";
    paths.versionId += "-legacy";
    paths.userLexiconFile = legacyDir / "userlexicon.coral";
    coral::phonetics::LexiconCatalog legacyLexicon;
    expect(legacyLexicon.load_primary_lexicon(paths) && legacyLexicon.load_pinyin_model(paths, {}), "legacy lexicons load");
    expect(legacyLexicon.get_eng_word_info("python", info) && info.id == kInvalidPhraseId, "legacy ID is safe");
    coral::phonetics::ContextScorer legacyModel;
    expect(legacyModel.load_g_bin_only({legacyDir / "languagemodel.coral", {}}, paths.versionId), "legacy model load");
    expect(parse("yongpythonxie", legacyLexicon, legacyModel) == "拥Python鞋", "unknown English breaks old Chinese context");
    for (const auto& sample : std::vector<std::pair<std::string, std::string>>{
        {"yongchinaxie", "拥China鞋"}, {"chinaxie", "China鞋"}, {"yongchina", "拥China"}}) {
        const auto result = decoded_candidates(sample.first, legacyLexicon, legacyModel);
        expect(!result.empty() && result.front().find("吃拿") != std::string::npos,
            "higher-scoring Chinese reading remains first");
        expect(std::count(result.begin(), result.end(), sample.second) == 1,
            "lower-scoring mixed sentence survives composition filtering exactly once");
    }
    legacyLexicon.shutdown_user_history();
    lexicon.shutdown_user_history();
    check_full_hit_composition(dir);
    std::cout << "mixed_language failures=" << failures << std::endl;
    // 既有单例线程池的销毁不在本回归测试范围内。
    std::_Exit(failures ? 1 : 0);
}

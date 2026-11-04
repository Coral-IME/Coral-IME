/*
* @author: xuxiang
* @date 2021-6-10 11:50:33
*/
#include "pch.h"
#include "phonetic_types.h"
#include "context_scorer.h"
#include "lexicon_table.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace
{
	// languagemodel.coral 静态图文件名，写入当前工作目录（compile_resource_files 已切换到输出目录）
	constexpr const char* kGBinFileName = "languagemodel.coral";

    bool build_reading_priors(const std::filesystem::path& path,
        std::unordered_map<uint32_t, double>& priors, std::string& error) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) { error = "Cannot open pronunciation lexicon"; return false; }
        const auto size = input.tellg();
        input.seekg(0);
        char magic[8]{};
        input.read(magic, sizeof(magic));
        // 非主词典摘要绑定文件没有读音信息，沿用原来的纯词面模型加载行为。
        if (!input || std::memcmp(magic, kPrimaryBinMagic, sizeof(magic)) != 0) return true;
        if (size < std::streamoff(sizeof(coral::storage::LexiconFileHeader)) ||
            uint64_t(size) > 1024ull * 1024 * 1024) {
            error = "Invalid pronunciation lexicon size"; return false;
        }
        std::vector<char> bytes(static_cast<size_t>(size));
        input.seekg(0); input.read(bytes.data(), bytes.size());
        coral::phonetics::LexiconTable table;
        if (!input || !table.bind_data(bytes.data(), bytes.size())) {
            error = "Invalid pronunciation lexicon"; return false;
        }
        struct Reading {
            std::string_view word, pinyin;
            uint32_t id;
            double weight;
        };
        std::vector<Reading> rows;
        rows.reserve(table.entry_count());
        for (uint32_t i = 0; i < table.entry_count(); ++i) {
            const auto& entry = table.entries()[i];
            const auto word = table.entry_word(entry), pinyin = table.entry_pinyin(entry);
            if (pinyin.empty() || std::all_of(word.begin(), word.end(),
                [](unsigned char c) { return c < 128; })) continue;
            if (!std::isfinite(entry.weight)) { error = "Invalid pronunciation weight"; return false; }
            rows.push_back({word, pinyin, entry.phrase_id, entry.weight});
        }
        std::sort(rows.begin(), rows.end(), [](const Reading& a, const Reading& b) {
            if (a.word != b.word) return a.word < b.word;
            if (a.pinyin != b.pinyin) return a.pinyin < b.pinyin;
            return a.id < b.id;
        });
        struct ReadingRange { size_t begin, end; double weight; };
        std::vector<ReadingRange> readings;
        for (size_t begin = 0; begin < rows.size();) {
            size_t end = begin + 1;
            while (end < rows.size() && rows[end].word == rows[begin].word) ++end;
            readings.clear();
            for (size_t start = begin; start < end;) {
                size_t stop = start + 1;
                double weight = rows[start].weight;
                while (stop < end && rows[stop].pinyin == rows[start].pinyin) {
                    weight = std::max(weight, rows[stop].weight); ++stop;
                }
                // 全拼、简拼和其他检索别名只算同一个读音，不能重复累加使用次数。
                readings.push_back({start, stop, weight}); start = stop;
            }
            if (readings.size() > 1) {
                double peak = readings.front().weight;
                for (const auto& reading : readings) peak = std::max(peak, reading.weight);
                double mass = 0;
                for (const auto& reading : readings) mass += std::exp(reading.weight - peak);
                const double total = peak + std::log(mass);
                for (const auto& reading : readings) {
                    const double prior = std::min(0.0, reading.weight - total);
                    for (size_t i = reading.begin; i < reading.end; ++i) {
                        const auto found = priors.emplace(rows[i].id, prior);
                        if (!found.second && std::abs(found.first->second - prior) > 1e-10) {
                            error = "Conflicting pronunciation identity"; return false;
                        }
                    }
                }
            }
            begin = end;
        }
        return true;
    }
}

coral::phonetics::ContextScorer::ContextScorer() = default;
coral::phonetics::ContextScorer::~ContextScorer() = default;

#if CORAL_ENABLE_CHARACTER_MODEL
void coral::phonetics::ContextScorer::configure_character_model(const std::filesystem::path& default_path, bool enabled)
{
    character_model_.reset();
    character_weight_ = 0;
    candidate_ranker_ = {};
    if (!enabled) return;
#if defined(_WIN32)
    const wchar_t* path = _wgetenv(L"CORAL_CHARACTER_MODEL");
#else
    const char* path = std::getenv("CORAL_CHARACTER_MODEL");
#endif
    const auto model_path = path && *path ? std::filesystem::path(path) : default_path;
    std::error_code ec;
    if (!std::filesystem::exists(model_path, ec)) return;
    double weight = 0.5;
    if (const char* value = std::getenv("CORAL_CHARACTER_WEIGHT")) {
        char* end = nullptr;
        weight = std::strtod(value, &end);
        if (!end || end == value || *end || !std::isfinite(weight) || weight < 0 || weight > 8) {
            LOG_ERROR("Character language model rejected: invalid weight"); return;
        }
    }
    std::string error;
    if (!load_character_model(model_path, weight, error))
        LOG_ERROR("Character language model rejected: " + error);
    else LOG_INFO("Character language model enabled, weight=" + std::to_string(weight));
    if (!character_model_) return;
#if defined(_WIN32)
    const wchar_t* ranker_path = _wgetenv(L"CORAL_CANDIDATE_RANKER");
#else
    const char* ranker_path = std::getenv("CORAL_CANDIDATE_RANKER");
#endif
    // 首轮只允许显式选择实验模型，普通运行不因目录里多一个文件改变排序。
    if (ranker_path && *ranker_path) {
        if (!load_candidate_ranker(std::filesystem::path(ranker_path), error))
            LOG_ERROR("Candidate ranker rejected: " + error);
        else LOG_INFO("Candidate LambdaRank model enabled, feature_version=1");
    }
}

#endif

bool coral::phonetics::ContextScorer::load_surface_model(const filesystem::path& model,
    const filesystem::path& lexicon, std::string& error)
{
    try {
        coral::language_model::SurfaceModel candidate;
        if (!candidate.load_file(model, lexicon, error)) return false;
        std::unordered_map<uint32_t, double> priors;
        if (!build_reading_priors(lexicon, priors, error)) return false;
        surface_model_ = std::move(candidate);
        reading_log_priors_ = std::move(priors);
        pair_bonus_.reset();
        pair_bonus_states_.clear();
        pair_bonus_loaded_ = false;
        return true;
    } catch (const std::exception& failure) {
        error = failure.what(); return false;
    }
}

bool coral::phonetics::ContextScorer::load_pair_bonus_model(const filesystem::path& model,
    const filesystem::path& lexicon, std::string& error)
{
    try {
        coral::language_model::PairBonusModel candidate;
        if (!candidate.load_file(model, error)) return false;
        // 主词库仅用于当前进程的编号缓存，增益文件不绑定其摘要或编号。
        std::ifstream input(lexicon, std::ios::binary | std::ios::ate);
        if (!input) { error = "Cannot open pair bonus lexicon cache source"; return false; }
        const auto length = input.tellg();
        if (length < std::streamoff(sizeof(coral::storage::LexiconFileHeader)) ||
            uint64_t(length) > 1024ull * 1024 * 1024) {
            error = "Invalid pair bonus lexicon cache source size"; return false;
        }
        std::vector<char> bytes(static_cast<size_t>(length));
        input.seekg(0); input.read(bytes.data(), bytes.size());
        coral::phonetics::LexiconTable table;
        if (!input || !table.bind_data(bytes.data(), bytes.size())) {
            error = "Invalid pair bonus lexicon cache source"; return false;
        }
        std::unordered_map<uint32_t, uint32_t> states;
        for (uint32_t i = 0; i < table.entry_count(); ++i) {
            const auto& entry = table.entries()[i];
            const auto state = candidate.word_state(table.entry_word(entry));
            if (state) {
                const auto found = states.emplace(entry.phrase_id, state);
                if (!found.second && found.first->second != state) {
                    error = "Conflicting pair bonus phrase identity"; return false;
                }
            }
        }
        pair_bonus_ = std::move(candidate);
        pair_bonus_states_ = std::move(states);
        pair_bonus_loaded_ = true;
        surface_model_.reset();
        reading_log_priors_.clear();
        error.clear();
        return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

bool coral::phonetics::ContextScorer::load_g_bin_only(
	const coral::phonetics::ResourceLocation& gbinFile,
	const string& versionId)
{
	// languagemodel.coral 是唯一的二元模型格式。老部署包没有 BigramGBin ini 键时现场算文件自身 MD5。
	// POSIX 分支强制校验 identifier 非空匹配，指望文件作为真源。
	std::error_code ec;
	if (!std::filesystem::exists(gbinFile.path, ec))
	{
		LOG_ERROR("Langengine: languagemodel.coral file not found: " + gbinFile.path.string());
		return false;
	}

	std::string identifier = gbinFile.identifier;
	if (identifier.empty())
		identifier = hlp::calculateFileMD5(gbinFile.path.string());
    // 两种格式以独立 magic 区分。新模型绑定实际主词典摘要，绝不把新字段解释为旧 uni。
    std::ifstream input(gbinFile.path, std::ios::binary);
    char magic[8]{};
    input.read(magic, sizeof(magic));
    if (input && std::memcmp(magic, "CBGN0001", 8) == 0) {
        std::string error;
        if (identifier.empty() || identifier != hlp::calculateFileMD5(gbinFile.path.string()) ||
            !load_pair_bonus_model(gbinFile.path, gbinFile.path.parent_path() / "mainlexicon.coral", error)) {
            LOG_ERROR("Pair bonus model rejected: " + error);
            return false;
        }
        LOG_INFO("Pair bonus model loaded: pairs=" + std::to_string(pair_bonus_.pair_count()) +
            " max_bonus=" + std::to_string(pair_bonus_.max_bonus()));
        return true;
    }
    if (input && std::memcmp(magic, "CSLM0001", 8) == 0) {
        std::string error;
        if (identifier.empty() || identifier != hlp::calculateFileMD5(gbinFile.path.string()) ||
            !load_surface_model(gbinFile.path, gbinFile.path.parent_path() / "mainlexicon.coral", error)) {
            LOG_ERROR("Surface language model rejected: " + error);
            return false;
        }
        return true;
    }
	if (identifier.empty() ||
		!gbin_view_.map(gbinFile.path.wstring(), hlp::buildSHNameW(versionId, "bigramGBinSharedMemory"), identifier))
	{
		LOG_ERROR("Langengine: failed to load languagemodel.coral bigram graph: " + gbinFile.path.string());
		return false;
	}

	LOG_INFO("Langengine: bigram language model loaded from languagemodel.coral graph: " + gbinFile.path.string());
    surface_model_.reset();
    reading_log_priors_.clear();
	pair_bonus_.reset();
	pair_bonus_states_.clear();
	pair_bonus_loaded_ = false;
	return true;
}

bool coral::phonetics::ContextScorer::build_g_bin_only(const filesystem::path& modelBigramFile,
	const filesystem::path& unigramSourceFile)
{
	// 一元概率从主词典源 CSV 并入 languagemodel.coral uni 区，构建口径与 buildLexiconForCN 一致
	std::cout << "Building languagemodel.coral bigram model (unigram probabilities merged from primary lexicon source)..." << std::endl;

	if (!std::filesystem::exists(modelBigramFile)) {
		std::cerr << "Error: Bigram model file not found: " << modelBigramFile.string() << std::endl;
		return false;
	}
	if (!std::filesystem::exists(unigramSourceFile)) {
		std::cerr << "Error: Unigram source file not found: " << unigramSourceFile.string() << std::endl;
		return false;
	}

	if (!coral::phonetics::BigramTable::build_from_csv(modelBigramFile, unigramSourceFile, std::filesystem::path(kGBinFileName)))
	{
		std::cerr << "Error: Failed to build languagemodel.coral bigram model." << std::endl;
		return false;
	}

	std::cout << "languagemodel.coral bigram model built successfully!" << std::endl;
	return true;
}

coral::phonetics::Score coral::phonetics::ContextScorer::get_bigram(const uint32_t prevId, const uint32_t successor_id) const
{
    if (const auto* model = surface_model()) {
        const auto context = model->phrase_end(prevId);
        coral::language_model::PhraseScore score;
        if (context && model->score_phrase(context, successor_id, score))
            return static_cast<coral::phonetics::Score>(score.log_probability);
        return kLogInfiniteSimal;
    }
	if (prevId >= kCloudBaseOffset || successor_id >= kCloudBaseOffset)	// 默认云词典专有词不在语言模型中
		return kLogInfiniteSimal;

	const coral::phonetics::BigramTable* g = gbin();
	if (g == nullptr || g->start() < 0)
		return kLogInfiniteSimal;

	// start 态定位前词状态（uni 区直挂）
	int32_t prevState = 0;
	float uniWeight = 0.f;
	if (!g->find_uni(prevId, prevState, uniWeight))
		return kLogInfiniteSimal; // 前词不在模型中，与旧 wsfa miss 语义一致

	int32_t next = 0;
	float weight = 0.f;
	if (g->find(prevState, successor_id, next, weight))
		return static_cast<coral::phonetics::Score>(weight);

	// miss：回退到 succ 一元（与旧 wsfa miss 语义一致，不加 backoffWeight 罚分）
	int32_t succState = 0;
	float succUni = 0.f;
	if (!g->find_uni(successor_id, succState, succUni))
		return kLogInfiniteSimal; // 后继不在模型中，由调用方回退词典权重
	return static_cast<coral::phonetics::Score>(succUni);
}

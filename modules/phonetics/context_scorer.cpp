/*
* @author: xuxiang
* @date 2021-6-10 11:50:33
*/
#include "pch.h"
#include "phonetic_types.h"
#include "context_scorer.h"

namespace
{
	// languagemodel.coral 静态图文件名，写入当前工作目录（compile_resource_files 已切换到输出目录）
	constexpr const char* kGBinFileName = "languagemodel.coral";
}

coral::phonetics::ContextScorer::ContextScorer() = default;
coral::phonetics::ContextScorer::~ContextScorer() = default;

bool coral::phonetics::ContextScorer::load_surface_model(const filesystem::path& model,
    const filesystem::path& lexicon, std::string& error)
{
    return surface_model_.load_file(model, lexicon, error);
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

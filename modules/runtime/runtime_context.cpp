#include "pch.h"
#include "runtime_context.h"
#include "preference_registry.h"
#include "mode_selection.h"

#include "lexicon_source.h"
#include "syllable_parser.h"
#include "preference_registry.h"
#include "script/script_conversion.h"
#include "trace_log.h"

#include <string>
#include <limits>

namespace coral {

Engine& Engine::compatInstance()
{
	static Engine instance;
	return instance;
}

bool Engine::init(const EngineOptions& options)
{
	if (m_initialized || generation_ == std::numeric_limits<std::uint64_t>::max())
	{
		LOG_ERROR("Engine: already initialized, refusing re-init");
		return false;
	}

	LOG_INFO("Engine: initialization started");

	// 1. 用户配置 ini 装载 + 注入引擎全局设置
	sConfig->init(options.userConfigIni.string());
	// 引擎配置快照：从持久配置装配并推送（此后引擎热路径只读快照）。
	// init 首次装配绕过代际门闸（forceRefresh），保证初始快照一定建立
	refreshConfig(true);
	// 全半角/简繁的当前生效值从持久化配置初始化（SettingsConfig 只存持久值）
	inputModeState().reset(sConfig->runtime_full_width(), sConfig->runtime_traditional());
	SettingsIniManager* settings = sConfig->getSettingStore();

	// 2. 词库路径装配（纯路径拼接，无平台调用）
	const std::filesystem::path dataDir = options.dataDir;
	const std::filesystem::path generalLexiconPath = dataDir / "generallexicon";
	const auto versionId = settings->getIniValueAs<std::string>("app", "Version");

	coral::phonetics::LexiconLocations lexiconPath{
		{dataDir / "bushou.marisa", settings->getIniValueAs<std::string>("LexiconFiles", "BushouTrie")},
		{dataDir / "bushou.dat", settings->getIniValueAs<std::string>("LexiconFiles", "BushouData")},
		{dataDir / "stroke.marisa", settings->getIniValueAs<std::string>("LexiconFiles", "StrokeTrie")},
		{dataDir / "stroke.dat", settings->getIniValueAs<std::string>("LexiconFiles", "StrokeData")},
		{generalLexiconPath / "languagemodel.coral", settings->getIniValueAs<std::string>("LexiconFiles", "BigramGBin")},
		{generalLexiconPath / "english.dat", settings->getIniValueAs<std::string>("LexiconFiles", "EnglishData")},
		{generalLexiconPath / "english.marisa", settings->getIniValueAs<std::string>("LexiconFiles", "EnglishMarisa")},
		{generalLexiconPath / "primary.dat", settings->getIniValueAs<std::string>("LexiconFiles", "PrimaryData")},
		{generalLexiconPath / "primary.marisa", settings->getIniValueAs<std::string>("LexiconFiles", "PrimaryMarisa")},
		{generalLexiconPath / "correction_pinyin_model.dat", settings->getIniValueAs<std::string>("LexiconFiles", "CorrectionData")},
		{generalLexiconPath / "char_lm.dat", settings->getIniValueAs<std::string>("LexiconFiles", "CharLMData")},
		options.userLexiconFile,
		dataDir,
		versionId};

	// 3. 兼容访问入口加载可释放的模型实例
	if (!coral::phonetics::SyllableParser::instance().load(lexiconPath, options.syncLoad, options.retryModelLoadUntilSuccess,
        sConfig->get_smart_candidates()))
	{
		LOG_ERROR("Engine: failed to load pinyin model data for version: " + versionId);
		shutdown();
		return false;
	}
	if (!coral::phonetics::ScriptConversion::instance().load(dataDir))
	{
		LOG_ERROR("Engine: failed to load S2T converter");
		shutdown();
		return false;
	}

	m_options = options;
    ++generation_;
	m_initialized = true;
	LOG_INFO("Engine: initialization completed successfully for version: " + versionId);
	return true;
}

void Engine::refreshConfig(bool forceRefresh)
{
	// 代际门闸（Step 4）：无跨进程/本地写时跳过 7 键装配，
	// 共享内存读降为一次原子代际读（共享内存不可用时退化为 INI mtime 检测）。
	// 本地写路径（setValue/setBatchConfigs）同样会推进代际/改写 INI，门闸自然放行。
	if (!forceRefresh && !sConfig->pollForUpdates()) {
		return;
	}
	auto config = std::make_shared<EngineConfig>();
	config->smart_prediction = sConfig->get_smart_prediction();
	config->isUMode = sConfig->getIsUMode();
	config->isVMode = sConfig->getIsVMode();
	config->pinyin_correction = sConfig->getPinyinCorrection();
	config->fuzzy_choices = sConfig->getFuzzyChoice();
	config->limit_count = sConfig->getLimitCount();
	config->isShowInfos = sConfig->getIsShowInfos();
	config->combined_lexicon_version = sConfig->getCombinedLexiconVersion();
	m_config.update(std::move(config));
}

void Engine::shutdown()
{
    m_initialized = false;
    // 析构时先排空加载、停止学习，然后释放模型视图。
    coral::phonetics::SyllableParser::release_instance();
    coral::phonetics::ScriptConversion::instance().reset();
    m_options = {};
    logShutdown();
}

} // 命名空间 coral 结束

#include "preference_registry.h"
#include "credential_protection.h"
#include "coral/translation/languages.h"
#include "coral/translation/shortcut.h"
#include "coral/speech/shortcut.h"
#include <chrono>
#include <thread>
#include <string>
#include <algorithm>
#include "ini_store.h"
#include "shared_preferences.h"
#include "text_file_helpers.hpp"
#include <filesystem>
#include <cstdlib>
#include "trace_log.h"
#include "product_constants.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

SettingsConfig::SettingsConfig()
{
}

SettingsConfig& SettingsConfig::getInstance()
{
	static SettingsConfig instance;
	return instance;
}

void SettingsConfig::prepare_runtime_settings(const std::string& ini_file_path)
{
    m_configStore.reset();
#if defined(_WIN32) || defined(_MSC_VER)
    m_sharedMemoryConfig.reset();
#endif
    m_inited = false;
    m_isShowInfos = false;
    m_configGenerationValid = false;
    m_lastConfigGeneration = 0;
    m_lastIniWriteTimeValid = false;
    m_lastIniWriteTime = {};
    m_iniFilePath.clear();
    init(ini_file_path);
}

void SettingsConfig::init(const std::string& iniFilePath)
{
	if (!m_inited) 
	{
		m_iniFilePath = iniFilePath;
		
#ifdef _WIN32
		// 首次运行时自动复制配置文件
		std::filesystem::path configPath(iniFilePath);
		std::filesystem::path installPath = hlp::getInstallPath();
        // 配置模板与数据覆盖必须位于同一目录树内。
        // 否则测试词典可能继承已安装版本的版本号/MD5。
        if (const char* dataDir = std::getenv("CORAL_LANGENGINE_DATA_DIR"); dataDir && *dataDir)
            installPath = std::filesystem::path(dataDir);
		std::filesystem::path templateConfigPath = installPath / "config" / "coralpinyinConfig.ini";
		std::error_code configError;
		std::error_code templateError;
		const bool configExists = std::filesystem::exists(configPath, configError);
		const bool templateExists = std::filesystem::exists(templateConfigPath, templateError);
		// 沙盒化的 TSF 宿主可能无法查看用户的 LocalLow 目录。
		// 保持激活状态并使用内置设置默认值。
		if (configError)
			LOG_ERROR("SettingsConfig: Cannot inspect user config: " + configError.message());
		if (!configExists && !configError)
		{
			try 
			{
				if (templateExists)
				{
					std::filesystem::create_directories(configPath.parent_path());
					std::filesystem::copy_file(templateConfigPath, configPath, 
						std::filesystem::copy_options::skip_existing);
				} 
				else 
				{
					LOG_ERROR("SettingsConfig: Template config file not found in install directory: " + templateConfigPath.string());
				}
			} 
			catch (const std::exception& e) 
			{
				LOG_ERROR("SettingsConfig: Failed to copy config file: " + std::string(e.what()));
			}
		}
		else if (configExists && templateExists)
		{
			// 配置文件已存在：对比版本号，不一致则同步 [LexiconFiles]
			try 
			{
				IniFileManager installIni(templateConfigPath.string());
				IniFileManager userIni(iniFilePath);
				if (installIni.load() && userIni.load()) 
				{
					std::string installVersion = installIni.getIniValueAs<std::string>("app", "Version", "");
					std::string userVersion = userIni.getIniValueAs<std::string>("app", "Version", "");
					
					if (!installVersion.empty() && installVersion != userVersion) 
					{
						auto lexiconFiles = installIni.getSection("LexiconFiles");
						userIni.updateAtomically([&](IniFileManager& ini) {
							for (const auto& [key, value] : lexiconFiles) 
							{
								ini.setIniValue("LexiconFiles", key, value);
							}
							ini.setIniValue("app", "Version", installVersion);
						});
						LOG_INFO("SettingsConfig: Synced [LexiconFiles] from install directory (version " + userVersion + " -> " + installVersion + ")");
					}
				}
			} 
			catch (const std::exception& e) 
			{
				LOG_ERROR("SettingsConfig: Failed to sync LexiconFiles: " + std::string(e.what()));
			}
		}
#endif
		initialize();
#if defined(_WIN32) || defined(_MSC_VER)
		if(m_sharedMemoryConfig)
		{
			const auto& configMap = getDefaultSettingsConfigMap();
			std::vector<std::pair<std::string, std::string>> configs;
			configs.reserve(configMap.size());
			for (const auto& [key, configItem] : configMap) 
			{
				if (configItem.policy == StoragePolicy::SHARED_MEMORY) {
					std::string value = m_configStore->getSettingsConfig<std::string>(key, configItem.defaultValue);
					configs.emplace_back(key, value);
				}
			}
			m_sharedMemoryConfig->initializeAndSetConfigs(configs);
			// 记录初始代际：本轮 init 已把 INI 全量写入共享内存，
			// 之后的代际变化才对应真实的跨进程/本地写
			std::uint64_t generation = 0;
			if (m_sharedMemoryConfig->getGeneration(generation)) {
				m_lastConfigGeneration = generation;
				m_configGenerationValid = true;
			}
		}
#endif
		m_inited = true;
	}
}

void SettingsConfig::reloadSettings()
{
	if (!m_inited) {
		return;
	}
	// 仅重载持久化存储；全半角/简繁的当前生效值由宿主重置（见 coral::InputModeState）
	m_configStore->load();
}

bool SettingsConfig::pollForUpdates()
{
#if defined(_WIN32) || defined(_MSC_VER)
	// 首选通道：共享内存代际（seqlock m_seqNum，写端每次更新 +2）。
	// 读侧无锁，成本为一次原子读；热路径完全不走 IPC。
	if (m_sharedMemoryConfig) {
		std::uint64_t generation = 0;
		if (m_sharedMemoryConfig->getGeneration(generation)) {
			if (!m_configGenerationValid || generation != m_lastConfigGeneration) {
				m_lastConfigGeneration = generation;
				m_configGenerationValid = true;
				return true;
			}
			return false;
		}
		// 共享内存暂不可用（UWP 服务未就绪等）：落到 INI 回退通道
	}
#endif
	return pollIniFileForUpdates();
}

bool SettingsConfig::pollIniFileForUpdates()
{
	// 回退通道：UWP 宿主偶发无法使用共享内存时，以 INI 文件 mtime 为代际。
	// 写端（设置界面）本就先写 INI 再写共享内存，回退通道无需写端配合。
	if (m_iniFilePath.empty()) {
		return false;
	}
	try {
		const auto lastWrite = std::filesystem::last_write_time(m_iniFilePath);
		if (!m_lastIniWriteTimeValid || lastWrite != m_lastIniWriteTime) {
			m_lastIniWriteTime = lastWrite;
			m_lastIniWriteTimeValid = true;
			reloadSettings();
			return true;
		}
	}
	catch (const std::exception& e) {
		LOG_ERROR("SettingsConfig: poll ini file failed: " + std::string(e.what()));
	}
	return false;
}

const std::unordered_map<std::string, SettingsConfig::ConfigItem>& SettingsConfig::getDefaultSettingsConfigMap()
{
	// 平台相关的存储策略定义
#if defined(_WIN32) || defined(_MSC_VER)
	// Windows: UV模式使用共享内存，支持多进程实时同步
	constexpr StoragePolicy UV_MODE_POLICY = StoragePolicy::SHARED_MEMORY;
#else
	// Mac/Linux: UV模式使用INI文件，不依赖共享内存
	constexpr StoragePolicy UV_MODE_POLICY = StoragePolicy::INI_ONLY;
#endif

	static const std::unordered_map<std::string, ConfigItem> configMap = {
		{"isFullWidth", ConfigItem("false", StoragePolicy::INI_ONLY)},
		{"isUMode", ConfigItem("true", UV_MODE_POLICY)},
        {"smartPrediction", ConfigItem("true", UV_MODE_POLICY)},
        {"smartCandidates", ConfigItem("true", UV_MODE_POLICY)},
		{"isVMode", ConfigItem("true", UV_MODE_POLICY)},
		{"isTraditionalChinese", ConfigItem("false", StoragePolicy::INI_ONLY)},
		{"candidateCount", ConfigItem("7", StoragePolicy::SHARED_MEMORY)},
		{"candidateFontSize", ConfigItem("16", StoragePolicy::SHARED_MEMORY)},
		{"isCtrlAndSpaceChangeLanguageModeOpen", ConfigItem("true", StoragePolicy::SHARED_MEMORY)},
		{"isShiftChangeLanguageModeOpen", ConfigItem("true", StoragePolicy::SHARED_MEMORY)},
		{"isCtrlChangeLanguageModeOpen", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
		{"isShiftAndSpaceChangeCharacterWidthOpen", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
		{"isShortCutChangeCharacterSimplifiedOrTraditionalOpen", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
		{"characterChangeSimplifiedOrTraditional", ConfigItem("F", StoragePolicy::SHARED_MEMORY)},
		{"isCtrlAndPeriodChangePunctuationLanguageModeOpen", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
		{"isPageUpDownFlipPageOpen", ConfigItem("true", StoragePolicy::SHARED_MEMORY)},
		{"translationEnabled", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
        {"translationShortcut", ConfigItem("=", StoragePolicy::SHARED_MEMORY)},
        {"isMinusAndPlusFlipPageOpen", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
        {"translationCredentials", ConfigItem("", StoragePolicy::INI_ONLY)},
        {"translationCredentialRevision", ConfigItem("", StoragePolicy::SHARED_MEMORY)},
        {"translationTargetLanguage", ConfigItem("en", StoragePolicy::SHARED_MEMORY)},
		{"translationKeyConfigured", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
        {"speechEnabled", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
        {"speechShortcut", ConfigItem("ctrl_win_shift", StoragePolicy::SHARED_MEMORY)},
        {"speechPushToTalk", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
        {"speechCredentials", ConfigItem("", StoragePolicy::INI_ONLY)},
        {"speechKeyConfigured", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
        {"speechCredentialRevision", ConfigItem("", StoragePolicy::SHARED_MEMORY)},
		{"isBraceFlipPageOpen", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
		{"isCommaAndPeriodFlipPageOpen", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
		{"isCharacterByCharacterParsingEnabled", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
		{"useEnglishPunctuation", ConfigItem("false", StoragePolicy::INI_ONLY)},
		{"candidateFontName", ConfigItem("Microsoft YaHei", StoragePolicy::SHARED_MEMORY)},
		{"candidateFontList", ConfigItem("[]", StoragePolicy::INI_ONLY)},
		{"theme", ConfigItem("light", StoragePolicy::SHARED_MEMORY)},
		{"candidateSkin", ConfigItem("", StoragePolicy::SHARED_MEMORY)},
		{"isDefaultEnglishMode", ConfigItem("false", StoragePolicy::INI_ONLY)},
		{"limitCount", ConfigItem("25", StoragePolicy::SHARED_MEMORY)},
		{"isAutoCheck", ConfigItem("true", StoragePolicy::SHARED_MEMORY)},
        {"updateLastCheckUtc", ConfigItem("0", StoragePolicy::INI_ONLY)},
        {"updateLastNotifiedVersion", ConfigItem("", StoragePolicy::INI_ONLY)},
		{"lexiconLastMaintenanceTime", ConfigItem("2023-01-01 00:00:00", StoragePolicy::SHARED_MEMORY)},
		{"todayWordFetchCount", ConfigItem("0", StoragePolicy::SHARED_MEMORY)},
		{"baseTime", ConfigItem(hlp::stamp2string(hlp::getTimeStamp(), "%Y-%m-%d"), StoragePolicy::SHARED_MEMORY)},
		{"isDoublepinyinMode", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
		{"doublepinyinSchemeName", ConfigItem("xiaohe", StoragePolicy::SHARED_MEMORY)},
		{"secondThirdCandidateKey", ConfigItem("none", StoragePolicy::SHARED_MEMORY)},
		{"pageTurnKey", ConfigItem("none", StoragePolicy::SHARED_MEMORY)},
		{"wordToCharacterKey", ConfigItem("none", StoragePolicy::SHARED_MEMORY)},
		{"fuzzyChoice", ConfigItem("0000000000000000000", StoragePolicy::SHARED_MEMORY)},// 19个数字分别对应模糊拼音的19个条目 默认全部关闭
		{"floatingToolbarPos", ConfigItem("defaultPos", StoragePolicy::SHARED_MEMORY)},
		{"isFloatingToolbarOpen", ConfigItem("true", StoragePolicy::SHARED_MEMORY)},
		{"isDebugEnabled", ConfigItem("false", StoragePolicy::SHARED_MEMORY)},
		{"pinyinCorrection", ConfigItem("true", StoragePolicy::SHARED_MEMORY)},
		{"combinedLexiconVersion", ConfigItem("0", StoragePolicy::SHARED_MEMORY)},
		{"isAutoSymbolCompletion", ConfigItem("true", StoragePolicy::SHARED_MEMORY)},
		{"isAutoConvertDigitSymbol", ConfigItem("true", StoragePolicy::SHARED_MEMORY)},
		{"isAutoConvertLetterSymbol", ConfigItem("true", StoragePolicy::SHARED_MEMORY)},
		{"isClipboardEnabled", ConfigItem("true", StoragePolicy::SHARED_MEMORY)},
	};
	return configMap;
}

bool SettingsConfig::getIsCharacterByCharacterParsingEnabled() const
{
	return getValue<bool>("isCharacterByCharacterParsingEnabled");
}

void SettingsConfig::setIsCharacterByCharacterParsingEnabled(bool mode)
{
	setValue<bool>("isCharacterByCharacterParsingEnabled", mode);
}

bool SettingsConfig::getIsAutoSymbolCompletion() const
{
	return getValue<bool>("isAutoSymbolCompletion");
}

void SettingsConfig::setIsAutoSymbolCompletion(bool mode)
{
	setValue<bool>("isAutoSymbolCompletion", mode);
}

bool SettingsConfig::getIsAutoConvertDigitSymbol() const
{
	return getValue<bool>("isAutoConvertDigitSymbol");
}

void SettingsConfig::setIsAutoConvertDigitSymbol(bool mode)
{
	setValue<bool>("isAutoConvertDigitSymbol", mode);
}

bool SettingsConfig::get_auto_convert_letter_symbol() const
{
    return getValue<bool>("isAutoConvertLetterSymbol");
}

void SettingsConfig::set_auto_convert_letter_symbol(bool enabled)
{
    setValue<bool>("isAutoConvertLetterSymbol", enabled);
}

bool SettingsConfig::getIsClipboardEnabled() const
{
	return getValue<bool>("isClipboardEnabled");
}

void SettingsConfig::setIsClipboardEnabled(bool enabled)
{
	setValue<bool>("isClipboardEnabled", enabled);
}

void SettingsConfig::set_runtime_full_width(bool isFullWidth)
{
	// 持久化默认值；当前生效值由 coral::InputModeState 持有
	setValue<bool>("isFullWidth", isFullWidth);
}

bool SettingsConfig::runtime_full_width() const
{
	return getValue<bool>("isFullWidth");
}

void SettingsConfig::setIsAutoCheck(bool isAutoCheck)
{
	setValue<bool>("isAutoCheck", isAutoCheck);
}

bool SettingsConfig::getIsAutoCheck() const
{
	return getValue<bool>("isAutoCheck");
}

std::int64_t SettingsConfig::get_update_last_check_utc() const
{
    const auto text = getValue<std::string>("updateLastCheckUtc");
    std::int64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value > 0 ? value : 0;
}

void SettingsConfig::set_update_last_check_utc(std::int64_t value)
{
    setValue("updateLastCheckUtc", std::to_string(value));
}

std::string SettingsConfig::get_update_last_notified_version() const
{
    return getValue<std::string>("updateLastNotifiedVersion");
}

void SettingsConfig::set_update_last_notified_version(const std::string& value)
{
    setValue("updateLastNotifiedVersion", value);
}

void SettingsConfig::setIsDebugEnabled(bool isDebugEnabled)
{
	setValue<bool>("isDebugEnabled", isDebugEnabled);
}

bool SettingsConfig::getIsDebugEnabled() const
{
	return getValue<bool>("isDebugEnabled");
}

int SettingsConfig::getCombinedLexiconVersion() const
{
	return getValue<int>("combinedLexiconVersion");
}

void SettingsConfig::setCombinedLexiconVersion(int version)
{
	setValue<int>("combinedLexiconVersion", version);
}

void SettingsConfig::readCloudLexiconConfigs(LexiconConfigs& configs)
{
	if (!m_configStore)
		return;

	auto iniPath = hlp::getLocalLowCoralPinyinPath() / "config" / "coralpinyinConfig.ini";
	IniFileManager ini(iniPath.string());

	configs.enableAll = ini.getIniValueAs<bool>("lexicons", "enableAll", false);
	configs.disableAll = ini.getIniValueAs<bool>("lexicons", "disableAll", false);
	configs.basePath = (hlp::getLocalLowCoralPinyinPath() / ini.getIniValueAs<std::string>("app", "CloudLexiconPath")).string();
	const auto enabledLexicons = ini.getIniValueAs<std::string>("lexicons", "enabledLexicons");
	if (enabledLexicons == "[empty]")
		return;
	std::vector<std::string> enabledLexiconIds;
	hlp::splitString(enabledLexicons, " ", enabledLexiconIds);
	configs.lexicons.resize(enabledLexiconIds.size());
	for (auto i = 0; i < static_cast<int>(enabledLexiconIds.size()); ++i)
	{
		try
		{
			configs.lexicons[i].id = std::stoi(enabledLexiconIds[i]);
		}
		catch (const std::exception&)
		{
			LOG_ERROR("SettingsConfig: Failed to parse lexicon id: " + enabledLexiconIds[i]);
			configs.lexicons[i].id = -1;
		}
	}

	for (auto& lexicon : configs.lexicons)
	{
		const auto entryName = std::string("lexicon") + std::to_string(lexicon.id);
		if (lexicon.id != ini.getIniValueAs<int>(entryName, "id"))
		{
			LOG_ERROR("SettingsConfig: id mismatch, enabledLexicons.id" + std::to_string(lexicon.id)
				+ " != entry.id" + ini.getIniValueAs<std::string>(entryName, "id"));
			continue;
		}
		auto enabled = ini.getIniValueAs<bool>(entryName, "enabled");
		lexicon.status = enabled ? LexiconStatus::Enabled : LexiconStatus::Disabled;
		lexicon.installed = ini.getIniValueAs<bool>(entryName, "installed");
		std::string name = ini.getIniValueAs<std::string>(entryName, "name");
		std::wstring wstr = hlp::str2wstr(name);
		std::filesystem::path path(wstr);
		lexicon.name = std::to_string(lexicon.id) + "_" + path.string();
		lexicon.versionId = ini.getIniValueAs<std::string>(entryName, "version");
		lexicon.trieIdentifier = ini.getIniValueAs<std::string>({"Lexicon" + std::to_string(lexicon.id)}, "marisaMD5");
		lexicon.dataIdentifier = ini.getIniValueAs<std::string>({"Lexicon" + std::to_string(lexicon.id)}, "datMD5");
	}

	// 排序并去重，防止重复加载
	std::sort(configs.lexicons.begin(), configs.lexicons.end(),
		[](const LexiconEntry& a, const LexiconEntry& b) {
			return a.id < b.id;
		});
	configs.lexicons.erase(
		std::unique(configs.lexicons.begin(), configs.lexicons.end(),
			[](const LexiconEntry& a, const LexiconEntry& b) {
				return a.id == b.id;
			}), configs.lexicons.end());
}

std::string SettingsConfig::getLexiconLastMaintenanceTime() const
{
	return getValue<std::string>("lexiconLastMaintenanceTime");
}

void SettingsConfig::setLexiconLastMaintenanceTime(const std::string& time)
{
	setValue<std::string>("lexiconLastMaintenanceTime", time);
}

void SettingsConfig::set_runtime_traditional(bool isTraditionalChinese)
{
	// 持久化默认值；当前生效值由 coral::InputModeState 持有
	setValue<bool>("isTraditionalChinese", isTraditionalChinese);
}

bool SettingsConfig::runtime_traditional() const
{
	return getValue<bool>("isTraditionalChinese");
}

void SettingsConfig::setShowInfos(bool isShowInfos)
{
    m_isShowInfos = isShowInfos;
}

bool SettingsConfig::getIsShowInfos() const
{
    return m_isShowInfos;
}

void SettingsConfig::setCandidateCount(int count)
{
	setValue<int>("candidateCount", count);
}

int SettingsConfig::getCandidateCount() const
{
	int result = getValue<int>("candidateCount");
	return result > 0 ? result : coralpinyinglobal::DEFAULT_CANDIDATE_COUNT;
}

void SettingsConfig::setCandidateFontSize(int count)
{
	if (count > coralpinyinglobal::MAXIMUM_CANDIDATE_FONT_SIZE || count < coralpinyinglobal::MINIMUM_CANDIDATE_FONT_SIZE)
		count = coralpinyinglobal::DEFAULT_CANDIDATE_FONT_SIZE;
	setValue<int>("candidateFontSize", count);
}

int SettingsConfig::getCandidateFontSize() const
{
	int result = getValue<int>("candidateFontSize");
	return result <= coralpinyinglobal::MAXIMUM_CANDIDATE_FONT_SIZE && result >= coralpinyinglobal::MINIMUM_CANDIDATE_FONT_SIZE ? result : coralpinyinglobal::DEFAULT_CANDIDATE_FONT_SIZE;
}

void SettingsConfig::setCandidateFontName(const std::string& fontName)
{
	setValue<std::string>("candidateFontName", fontName);
}

std::string SettingsConfig::getCandidateFontName() const
{
	std::string result = getValue<std::string>("candidateFontName");
	return result.empty() ? "Microsoft YaHei" : result;
}

void SettingsConfig::setCandidateFontList(const std::string& fonts)
{
	setValue<std::string>("candidateFontList", fonts);
}

std::string SettingsConfig::getCandidateFontList() const
{
	return getValue<std::string>("candidateFontList");
}

void SettingsConfig::setTheme(const std::string& theme)
{
	setValue<std::string>("theme", theme);
}

std::string SettingsConfig::getTheme() const
{
	std::string result = getValue<std::string>("theme");
	return result.empty() ? "light" : result;
}

void SettingsConfig::setCandidateSkin(const std::string& skin)
{
	setValue<std::string>("candidateSkin", skin);
}

std::string SettingsConfig::getCandidateSkin() const
{
	return getValue<std::string>("candidateSkin");
}

void SettingsConfig::setFloatingToolbarPos(const std::string& toolbarPos)
{
	setValue<std::string>("floatingToolbarPos", toolbarPos);
}

std::string SettingsConfig::getFloatingToolbarPos() const
{
	std::string result = getValue<std::string>("floatingToolbarPos");
	return result.empty() ? "defaultPos" : result;
}

bool SettingsConfig::getIsFloatingToolbarOpen() const
{
	return getValue<bool>("isFloatingToolbarOpen");
}

void SettingsConfig::setIsFloatingToolbarOpen(bool mode)
{
	setValue<bool>("isFloatingToolbarOpen", mode);
}

void SettingsConfig::setChangeLanguageModeShortCut(const std::string& shortcut, bool isopen)
{
	setValue<bool>(shortcut, isopen);
}

bool SettingsConfig::getIsCtrlAndSpaceShortCutOpen() const
{
	return getValue<bool>("isCtrlAndSpaceChangeLanguageModeOpen");
}

bool SettingsConfig::getIsShiftShortCutOpen() const
{
	return getValue<bool>("isShiftChangeLanguageModeOpen");
}

bool SettingsConfig::getIsCtrlShortCutOpen() const
{
	return getValue<bool>("isCtrlChangeLanguageModeOpen");
}

void SettingsConfig::setChangeCharacterWidthShortCut(bool isopen)
{
	setValue<bool>("isShiftAndSpaceChangeCharacterWidthOpen", isopen);
}

bool SettingsConfig::getIsShiftAndSpaceShortCutOpen() const
{
	return getValue<bool>("isShiftAndSpaceChangeCharacterWidthOpen");
}

void SettingsConfig::setShortCutChangeCharacterSimOrTra(bool isopen)
{
	setValue<bool>("isShortCutChangeCharacterSimplifiedOrTraditionalOpen", isopen);
}

bool SettingsConfig::getIsShortCutChangeCharacterSimOrTraOpen() const
{
	return getValue<bool>("isShortCutChangeCharacterSimplifiedOrTraditionalOpen");
}

void SettingsConfig::setChangePunctuationLanguageModeShortCut(bool isopen)
{
	setValue<bool>("isCtrlAndPeriodChangePunctuationLanguageModeOpen", isopen);
}

bool SettingsConfig::getIsCtrlAndPeriodShortCutOpen() const
{
	return getValue<bool>("isCtrlAndPeriodChangePunctuationLanguageModeOpen");
}

void SettingsConfig::setFlipPageShortCut(const std::string& shortcut, bool isopen)
{
	setChangeLanguageModeShortCut(shortcut, isopen);
}

bool SettingsConfig::get_page_up_down_enabled() const
{
	return getValue<bool>("isPageUpDownFlipPageOpen");
}

bool SettingsConfig::get_minus_equals_page_enabled() const
{
    return getValue<bool>("isMinusAndPlusFlipPageOpen");
}

std::string SettingsConfig::get_translation_shortcut() const
{
    return coral::translation::shortcut_option(getValue<std::string>("translationShortcut")).value;
}

void SettingsConfig::set_translation_shortcut(const std::string& value)
{
    const auto& option = coral::translation::shortcut_option(value);
    if (value == option.value) setValue<std::string>("translationShortcut", value);
}

bool SettingsConfig::get_translation_enabled() const
{
    return getValue<bool>("translationEnabled");
}

void SettingsConfig::set_translation_enabled(bool enabled)
{
    setValue<bool>("translationEnabled", enabled);
}

std::pair<std::string, std::string> SettingsConfig::get_translation_credentials() const
{
    std::string path;
    { std::lock_guard<std::mutex> lock(m_mutex); path = m_iniFilePath; }
    if (path.empty()) return {};
    // 独立读取器避免与工作线程共享可变的 INI 解析器。
    SettingsIniManager store(path);
    std::pair<std::string, std::string> credentials;
    coral::preferences::unprotect_credentials(store.getSettingsConfig<std::string>("translationCredentials", ""),
        credentials.first, credentials.second);
    return credentials;
}

bool SettingsConfig::set_translation_credentials(const std::string& id, const std::string& secret)
{
    std::string encrypted;
    if (!coral::preferences::protect_credentials(id, secret, encrypted)) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_configStore) return false;
    const std::string configured = encrypted.empty() ? "false" : "true";
    const auto revision = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    if (!m_configStore->updateAtomically([&](IniFileManager& ini) {
        ini.setIniValue("settingsconfig", "translationCredentials", encrypted);
        ini.remove("settingsconfig", "translationApiKey");
        ini.setIniValue("settingsconfig", "translationKeyConfigured", configured);
        ini.setIniValue("settingsconfig", "translationCredentialRevision", revision);
    })) return false;
    if (m_sharedMemoryConfig) m_sharedMemoryConfig->setConfigs({
        {"translationKeyConfigured", configured}, {"translationCredentialRevision", revision}});
    return true;
}

std::string SettingsConfig::get_translation_credential_revision() const
{
    return getValue<std::string>("translationCredentialRevision");
}

std::string SettingsConfig::get_translation_target() const
{
    const auto target = getValue<std::string>("translationTargetLanguage");
    return coral::translation::supported_target(target) ? target : "en";
}

void SettingsConfig::set_translation_target(const std::string& target)
{
    if (coral::translation::supported_target(target)) setValue<std::string>("translationTargetLanguage", target);
}

bool SettingsConfig::get_translation_key_configured() const
{
    return getValue<bool>("translationKeyConfigured");
}

bool SettingsConfig::getIsBraceFlipPageOpen() const
{
	return getValue<bool>("isBraceFlipPageOpen");
}

bool SettingsConfig::get_speech_enabled() const { return getValue<bool>("speechEnabled"); }
void SettingsConfig::set_speech_enabled(bool enabled) { setValue<bool>("speechEnabled", enabled); }
std::string SettingsConfig::get_speech_shortcut() const {
    return coral::speech::speech_shortcut(getValue<std::string>("speechShortcut")).value;
}
void SettingsConfig::set_speech_shortcut(const std::string& value) {
    if (value == coral::speech::speech_shortcut(value).value) setValue<std::string>("speechShortcut", value);
}
bool SettingsConfig::get_speech_push_to_talk() const { return getValue<bool>("speechPushToTalk"); }
void SettingsConfig::set_speech_push_to_talk(bool enabled) { setValue<bool>("speechPushToTalk", enabled); }
bool SettingsConfig::get_speech_key_configured() const { return getValue<bool>("speechKeyConfigured"); }
std::string SettingsConfig::get_speech_credential_revision() const { return getValue<std::string>("speechCredentialRevision"); }
std::pair<std::string, std::string> SettingsConfig::get_speech_credentials() const {
    std::string path;
    { std::lock_guard<std::mutex> lock(m_mutex); path = m_iniFilePath; }
    if (path.empty()) return {};
    SettingsIniManager store(path);
    std::pair<std::string, std::string> result;
    coral::preferences::unprotect_credentials(store.getSettingsConfig<std::string>("speechCredentials", ""), result.first, result.second);
    return result;
}
bool SettingsConfig::set_speech_credentials(const std::string& id, const std::string& secret) {
    std::string encrypted;
    if (!coral::preferences::protect_credentials(id, secret, encrypted)) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_configStore) return false;
    const std::string configured = encrypted.empty() ? "false" : "true";
    const auto revision = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    const auto update = [&](IniFileManager& ini) {
        ini.setIniValue("settingsconfig", "speechCredentials", encrypted);
        ini.setIniValue("settingsconfig", "speechKeyConfigured", configured);
        ini.setIniValue("settingsconfig", "speechCredentialRevision", revision);
    };
    // 本地同值事务可有限重试，吸收文件扫描造成的短暂占用；成功后才发布就绪标记。
    bool saved = false;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (m_configStore->updateAtomically(update, 250)) { saved = true; break; }
        if (attempt < 2) std::this_thread::sleep_for(std::chrono::milliseconds(20 * (attempt + 1)));
    }
    if (!saved) return false;
    if (m_sharedMemoryConfig) m_sharedMemoryConfig->setConfigs({
        {"speechKeyConfigured", configured}, {"speechCredentialRevision", revision}});
    return true;
}

bool SettingsConfig::getIsCommaAndPeriodFlipPageOpen() const
{
	return getValue<bool>("isCommaAndPeriodFlipPageOpen");
}

bool SettingsConfig::getIsUseEnglishPunctuation() const
{
	return getValue<bool>("useEnglishPunctuation");
}

void SettingsConfig::setIsUseEnglishPunctuation(bool isuse)
{
	setValue<bool>("useEnglishPunctuation", isuse);
}

void SettingsConfig::setCharacterChangeSimOrTra(const std::string& character)
{
	setValue<std::string>("characterChangeSimplifiedOrTraditional", character);
}

std::string SettingsConfig::getCharacterChangeSimOrTra() const
{
	std::string result = getValue<std::string>("characterChangeSimplifiedOrTraditional");
	return result.empty() ? "F" : result;
}

bool SettingsConfig::get_smart_prediction() const
{
    return getValue<bool>("smartPrediction");
}

void SettingsConfig::set_smart_prediction(bool enabled)
{
    setValue<bool>("smartPrediction", enabled);
}

bool SettingsConfig::get_smart_candidates() const
{
    return getValue<bool>("smartCandidates");
}

void SettingsConfig::set_smart_candidates(bool enabled)
{
    setValue<bool>("smartCandidates", enabled);
}

bool SettingsConfig::getIsUMode() const
{
	return getValue<bool>("isUMode");
}

void SettingsConfig::setIsUMode(bool isUMode)
{
	setValue<bool>("isUMode", isUMode);
}

bool SettingsConfig::getIsVMode() const
{
	return getValue<bool>("isVMode");
}

void SettingsConfig::setIsVMode(bool isVMode)
{
	setValue<bool>("isVMode", isVMode);
}

bool SettingsConfig::getIsDoublepinyinMode() const
{
	return getValue<bool>("isDoublepinyinMode");
}

void SettingsConfig::setDoublepinyinMode(bool mode)
{
	setValue<bool>("isDoublepinyinMode", mode);
}

void SettingsConfig::setIsDefaultEnglishMode(bool isDefaultEnglishMode)
{
	const auto newVal = isDefaultEnglishMode ? "true" : "false";
	saveToIniFile("isDefaultEnglishMode", newVal);
}

bool SettingsConfig::getIsDefaultEnglishMode() const
{
	// 空防护：引擎初始化失败/未完成时（如 TSF 宿主 ActivateEx 在 start() 失败后仍继续执行），
	// m_configStore 可能为 nullptr，直接解引用会以 [nullptr+0x98] 读 m_isLoad 触发 0xC0000005。
	// 与 getValue 模板的防护风格保持一致，返回默认值 false。
	if (!m_configStore)
		return false;
	return m_configStore->getSettingsConfig<bool>("isDefaultEnglishMode", false);
}

int SettingsConfig::getTodayWordFetchCount() const
{
	int result = getValue<int>("todayWordFetchCount");
	return result > 0 ? result : 0;
}

void SettingsConfig::setTodayWordFetchCount(int selectCount)
{
	setValue<int>("todayWordFetchCount", selectCount);
}

std::string SettingsConfig::getBaseTime() const
{
	return getValue<std::string>("baseTime");
}

void SettingsConfig::setBaseTime(const std::string& time)
{
	setValue<std::string>("baseTime", time);
}

void SettingsConfig::setPinyinCorrection(const bool enableCorrection)
{
	setValue<bool>("pinyinCorrection", enableCorrection);
}

bool SettingsConfig::getPinyinCorrection() const
{
	return getValue<bool>("pinyinCorrection");
}

void SettingsConfig::setFuzzyChoice(const std::vector<int>& fuzzy_choices)
{
	std::string result;
	for (const int i : fuzzy_choices) {
		result.append(std::to_string(i));
	}
	setValue<std::string>("fuzzyChoice", result);
}

std::vector<int> SettingsConfig::getFuzzyChoice() const
{
	const auto value = getValue<std::string>("fuzzyChoice");
	std::vector<int> result;
	result.reserve(FUZZY_CHOICES);
	if (value.length() != FUZZY_CHOICES)
	{
		result.assign(FUZZY_CHOICES, 0);
	}else
	{
		std::transform(value.begin(), value.end(), std::back_inserter(result),
			[](char eachChoice) { return eachChoice == '0' ? 0 : 1; });
	}
	return result;
}

int SettingsConfig::getLimitCount() const
{
	return 10;
}

void SettingsConfig::saveToIniFile(const std::string& key, const std::string& value) 
{
	if (m_configStore)
	{
		m_configStore->updateAtomically([&](IniFileManager& ini) {
			ini.setIniValue("settingsconfig", key, value);
		});
	}
}

void SettingsConfig::setBatchConfigs(const std::vector<std::pair<std::string, std::string>>& configs)
{
	if (configs.empty()) {
		return;
	}
	
	const auto& configMap = getDefaultSettingsConfigMap();
	std::vector<std::pair<std::string, std::string>> sharedMemConfigs;
	sharedMemConfigs.reserve(configs.size());
	
	// 先保存到 INI 文件
	if (m_configStore) {
		m_configStore->updateAtomically([&](IniFileManager& ini) {
			for (const auto& [key, value] : configs) {
				ini.setIniValue("settingsconfig", key, value);
			}
		});
	}
	
	// 再更新共享内存中的配置
	for (const auto& [key, value] : configs) {
		auto configIt = configMap.find(key);
		if (configIt != configMap.end() && configIt->second.policy == StoragePolicy::SHARED_MEMORY) {
			sharedMemConfigs.emplace_back(key, value);
		}
	}
#if defined(_WIN32) || defined(_MSC_VER)
	if (!sharedMemConfigs.empty() && m_sharedMemoryConfig) {
		m_sharedMemoryConfig->setConfigs(sharedMemConfigs);
	}
#endif
}
void SettingsConfig::initialize()
{ 
	if (!m_configStore) 
	{
		m_configStore = std::make_unique<SettingsIniManager>(m_iniFilePath);
	}
//这里安卓就不需要走设置的共享内存了
#if defined(_WIN32) || defined(_MSC_VER)
	if(!m_sharedMemoryConfig)
	{
		m_sharedMemoryConfig = std::make_unique<SharedMemoryConfig>();
	}
#endif
}

SettingsIniManager* SettingsConfig::getSettingStore()
{
	initialize();
	return m_configStore.get();
}

void SettingsConfig::setDoublepinyinSchemeName(const std::string& name) {
	setValue<std::string>("doublepinyinSchemeName", name);
}

std::string SettingsConfig::getDoublepinyinSchemeName() const {
	//todo 变量统一定义
	std::string result = getValue<std::string>("doublepinyinSchemeName");
	return result.empty() ? "xiaohe" : result;
}

void SettingsConfig::setSecondThirdCandidateKey(SecondThirdCandidateKeyType keyType)
{
	std::string keyTypeStr;
	switch (keyType) {
	case SecondThirdCandidateKeyType::NONE:
		keyTypeStr = "none";
		break;
	case SecondThirdCandidateKeyType::SEMICOLON:
		keyTypeStr = "semicolon";
		break;
	default:
		keyTypeStr = "none"; // 默认值
		break;
	}
	setValue<std::string>("secondThirdCandidateKey", keyTypeStr);
}

SecondThirdCandidateKeyType SettingsConfig::getSecondThirdCandidateKey() const
{
	std::string keyType = getValue<std::string>("secondThirdCandidateKey");
	if (keyType == "none") return SecondThirdCandidateKeyType::NONE;
	if (keyType == "semicolon") return SecondThirdCandidateKeyType::SEMICOLON;
	return SecondThirdCandidateKeyType::NONE; // 默认值
}

bool SettingsConfig::getIsSecondThirdCandidateEnabled() const
{
	SecondThirdCandidateKeyType keyType = getSecondThirdCandidateKey();
	return (keyType != SecondThirdCandidateKeyType::NONE);
}

void SettingsConfig::setWordToCharacterKey(WordToCharacterKeyType keyType)
{
	std::string keyTypeStr;
	switch (keyType) {
	case WordToCharacterKeyType::NONE:
		keyTypeStr = "none";
		break;
	case WordToCharacterKeyType::MINUS:
		keyTypeStr = "minus";
		break;
	case WordToCharacterKeyType::BRACKET:
		keyTypeStr = "bracket";
		break;
	default:
		keyTypeStr = "none"; // 默认值
		break;
	}
	setValue<std::string>("wordToCharacterKey", keyTypeStr);
}

WordToCharacterKeyType SettingsConfig::getWordToCharacterKey() const
{
	std::string keyType = getValue<std::string>("wordToCharacterKey");
	if (keyType == "none") return WordToCharacterKeyType::NONE;
	if (keyType == "minus") return WordToCharacterKeyType::MINUS;
	if (keyType == "bracket") return WordToCharacterKeyType::BRACKET;
	return WordToCharacterKeyType::NONE; // 默认值
}

bool SettingsConfig::getIsWordToCharacterEnabled() const
{
	WordToCharacterKeyType keyType = getWordToCharacterKey();
	return (keyType != WordToCharacterKeyType::NONE);
}

int SettingsConfig::getPlaceHolderValue(const std::string& key) const
{
	// 空防护：引擎初始化失败/未完成时（如 TSF 宿主 ActivateEx 在 start() 失败后仍执行
	// setProcessName），m_configStore 可能为 nullptr。返回默认值 0，消费方 getCurrentPlaceholder()
	// 的 switch 会落进 default 分支用空格占位符，不崩溃且不破坏输入流程。
	if (!m_configStore)
		return 0;
	return m_configStore->getIniValueAs<int>("placeholder", key, 0);
}

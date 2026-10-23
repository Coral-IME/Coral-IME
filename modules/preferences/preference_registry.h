#pragma once
#ifndef CORAL_MODULES_PREFERENCES_PREFERENCE_REGISTRY_H_INCLUDED
#define CORAL_MODULES_PREFERENCES_PREFERENCE_REGISTRY_H_INCLUDED

//// 在编译 DLL 时定义 MYLIBRARY_EXPORTS
// #ifdef SETTINGSCONFIGLIBRARY_EXPORTS
// #define SETTINGSCONFIGLIB_API __declspec(dllexport)
//#else
// #define SETTINGSCONFIGLIB_API __declspec(dllimport)
//#endif

#include <string>
#include <utility>
#include <mutex>
#include <unordered_map>
#include <filesystem>
#include <cstdint>
#include "ini_store.h"
#include "scalar_text_codec.h"
#if defined(_WIN32) || defined(_MSC_VER)
#include "shared_preferences.h"
#endif
// 导出宏定义
#ifdef BUILDING_SETTINGSCONFIG_DLL
	#define SETTINGS_API __declspec(dllexport)
#else
	#define SETTINGS_API __declspec(dllimport)
#endif


enum class SecondThirdCandidateKeyType {
	NONE = 0,
	SEMICOLON = 1
};

enum class WordToCharacterKeyType {
	NONE = 0,
	MINUS = 1,
	BRACKET = 2
};

enum PlaceholderScheme {
	U200BHOLDER = 1,
	PINYINHOLDER = 2,
	SPACEHOLDER = 3,
	NOHOLDER = 4
};

class SettingsConfig
{
public:
	enum class StoragePolicy {
		INI_ONLY,           // 只存储到INI文件
		SHARED_MEMORY,      // 存储到共享内存和INI文件
	};

private:
	struct ConfigItem {
		std::string defaultValue;
		StoragePolicy policy;
		
		ConfigItem(const std::string& def, StoragePolicy pol) 
			: defaultValue(def), policy(pol) {}
	};

public:
	static SettingsConfig& getInstance();
	void init(const std::string& iniFilePath);
    // 仅运行时启动阶段调用，此时先前模型及其借用方均已停止。
    void prepare_runtime_settings(const std::string& ini_file_path);
	void reloadSettings();

	// 跨进程配置变更检测（代际门闸，Step 4）：
	// - 共享内存可用：比较共享内存代际（seqlock m_seqNum，写端每次更新 +2），
	//   无锁一次原子读即可判定，热路径不走 IPC；变化返回 true。
	// - 共享内存不可用（UWP 宿主偶发无法使用共享内存）：回退为 INI 文件
	//   修改时间比较，变化时先 reloadSettings() 再返回 true。
	// 引擎边界（Engine::refreshConfig）据此跳过无变化的快照重建。
	bool pollForUpdates();
	SettingsConfig(const SettingsConfig&) = delete;
	SettingsConfig& operator=(const SettingsConfig&) = delete;
	//是否启用解析词来源显示
	void setShowInfos(bool isShowInfos);
	bool getIsShowInfos() const;
	
	//全半角（持久值；当前生效值见 coral::InputModeState）
	void set_runtime_full_width(bool mode);
	bool runtime_full_width() const;
	//简繁体（持久值；当前生效值见 coral::InputModeState）
	void set_runtime_traditional(bool mode);
	bool runtime_traditional() const;
	//候选词数量
	void setCandidateCount(int count);
	int  getCandidateCount() const;
	//候选词字体大小
	void setCandidateFontSize(int count);
	int getCandidateFontSize() const;
	//候选项字体名称
	void setCandidateFontName(const std::string& fontName);
	std::string getCandidateFontName() const;
	// 用户添加的候选字体列表（JSON 数组，仅保存到 INI）
	void setCandidateFontList(const std::string& fonts);
	std::string getCandidateFontList() const;
	//主题设置
	void setTheme(const std::string& theme);
	std::string getTheme() const;
	//候选窗皮肤（序列化的 CandidateThemeConfig）
	void setCandidateSkin(const std::string& skin);
	std::string getCandidateSkin() const;
	//悬浮框位置
	void setFloatingToolbarPos(const std::string& toolbarPos);
	std::string getFloatingToolbarPos() const;
	//悬浮框开关
	bool getIsFloatingToolbarOpen() const;
	void setIsFloatingToolbarOpen(bool mode);
	//U/V模式
	bool get_smart_prediction() const;
    void set_smart_prediction(bool enabled);
    bool get_smart_candidates() const;
    void set_smart_candidates(bool enabled);
	bool getIsUMode() const;
	void setIsUMode(bool mode);
	bool getIsVMode() const;
	void setIsVMode(bool mode);
	//双拼模式
	bool getIsDoublepinyinMode() const;
	void setDoublepinyinMode(bool mode);
	std::string getDoublepinyinSchemeName() const;
	void setDoublepinyinSchemeName(const std::string& name);

	//设置默认中/英文输入
	void setIsDefaultEnglishMode(bool mode);
	bool getIsDefaultEnglishMode() const;

	int getTodayWordFetchCount() const;
	void setTodayWordFetchCount(int selectCount);

	std::string getBaseTime() const;
	void setBaseTime(const std::string& time);
	void setPinyinCorrection(bool enableCorrection);
	bool getPinyinCorrection() const;
	void setFuzzyChoice(const std::vector<int>& fuzzy_choices);
	std::vector<int> getFuzzyChoice() const;


	//设置页面快捷键
	void setChangeLanguageModeShortCut(const std::string& shortcut, bool isopen);
	bool getIsCtrlAndSpaceShortCutOpen() const;
	bool getIsShiftShortCutOpen() const;
	bool getIsCtrlShortCutOpen() const;
	void setChangeCharacterWidthShortCut(bool isopen);
	bool getIsShiftAndSpaceShortCutOpen() const;
	void setShortCutChangeCharacterSimOrTra(bool isopen);
	bool getIsShortCutChangeCharacterSimOrTraOpen() const;
	void setCharacterChangeSimOrTra(const std::string& Character);
	std::string getCharacterChangeSimOrTra() const;
	void setChangePunctuationLanguageModeShortCut(bool isopen);
	bool getIsCtrlAndPeriodShortCutOpen() const;
	void setFlipPageShortCut(const std::string& shortcut, bool isopen);
	bool get_page_up_down_enabled() const;
	bool get_minus_equals_page_enabled() const;
    std::string get_translation_shortcut() const;
    void set_translation_shortcut(const std::string& value);
	bool get_translation_enabled() const;
	void set_translation_enabled(bool enabled);
    std::pair<std::string, std::string> get_translation_credentials() const;
    bool set_translation_credentials(const std::string& id, const std::string& secret);
    std::string get_translation_credential_revision() const;
    std::string get_translation_target() const;
    void set_translation_target(const std::string& target);
	bool get_translation_key_configured() const;
    bool get_speech_enabled() const;
    void set_speech_enabled(bool enabled);
    std::string get_speech_shortcut() const;
    void set_speech_shortcut(const std::string& value);
    bool get_speech_push_to_talk() const;
    void set_speech_push_to_talk(bool enabled);
    std::pair<std::string, std::string> get_speech_credentials() const;
    bool set_speech_credentials(const std::string& id, const std::string& secret);
    bool get_speech_key_configured() const;
    std::string get_speech_credential_revision() const;
	bool getIsBraceFlipPageOpen() const;
	bool getIsCommaAndPeriodFlipPageOpen() const;
	void setSecondThirdCandidateKey(SecondThirdCandidateKeyType keyType);
	SecondThirdCandidateKeyType getSecondThirdCandidateKey() const;
	bool getIsSecondThirdCandidateEnabled() const;
	void setWordToCharacterKey(WordToCharacterKeyType keyType);
	WordToCharacterKeyType getWordToCharacterKey() const;
	bool getIsWordToCharacterEnabled() const;
	bool getIsUseEnglishPunctuation() const;
	void setIsUseEnglishPunctuation(bool isuse);
	//逐字解析
	bool getIsCharacterByCharacterParsingEnabled() const;
	void setIsCharacterByCharacterParsingEnabled(bool mode);
	//符号自动补全
	bool getIsAutoSymbolCompletion() const;
	void setIsAutoSymbolCompletion(bool mode);
	bool getIsAutoConvertDigitSymbol() const;
	void setIsAutoConvertDigitSymbol(bool mode);
	bool get_auto_convert_letter_symbol() const;
	void set_auto_convert_letter_symbol(bool enabled);
	bool getIsClipboardEnabled() const;
	void setIsClipboardEnabled(bool enabled);
	//获取limit，由于值固定，只有get函数，没有set函数
	int getLimitCount() const;
	//升级相关配置
	bool getIsAutoCheck() const;
	void setIsAutoCheck(bool isAutoCheck);
    std::int64_t get_update_last_check_utc() const;
    void set_update_last_check_utc(std::int64_t value);
    std::string get_update_last_notified_version() const;
    void set_update_last_notified_version(const std::string& value);
	//调试模式是否开启
	void setIsDebugEnabled(bool isDebugEnabled);
	bool getIsDebugEnabled() const;
	// 云词库相关配置
	void readCloudLexiconConfigs(LexiconConfigs& configs);
	int getCombinedLexiconVersion() const;
	void setCombinedLexiconVersion(int version);

	//最后一次记录的词库自动维护时间
	std::string getLexiconLastMaintenanceTime() const;
	void setLexiconLastMaintenanceTime(const std::string& time);
	SettingsIniManager* getSettingStore();

	int getPlaceHolderValue(const std::string& key) const;

	void setBatchConfigs(const std::vector<std::pair<std::string, std::string>>& configs);
private:
	SettingsConfig();

	template<typename T>
	T getValue(const std::string& key) const {
		const auto& config = getDefaultSettingsConfigMap();
		auto configIt = config.find(key);
		if (configIt == config.end()) {
			return coral::preferences::ScalarTextCodec<T>::parse("");
		}

		const ConfigItem& item = configIt->second;
#if defined(_WIN32) || defined(_MSC_VER)
		// SHARED_MEMORY 策略：直接从共享内存读取
		if (item.policy == StoragePolicy::SHARED_MEMORY) {
			if (m_sharedMemoryConfig && m_sharedMemoryConfig->ensureConnection()) {
				std::string sharedValue = m_sharedMemoryConfig->getConfig(key);
				if (!sharedValue.empty()) {
					return coral::preferences::ScalarTextCodec<T>::parse(sharedValue);
				}
			}
		}
#endif
		if (m_configStore)
			return m_configStore->getSettingsConfig<T>(key, coral::preferences::ScalarTextCodec<T>::parse(item.defaultValue));

		return coral::preferences::ScalarTextCodec<T>::parse(item.defaultValue);
		
	}

	template<typename T>
	void setValue(const std::string& key, const T& value) {
		const auto& config = getDefaultSettingsConfigMap();
		auto configIt = config.find(key);
		if (configIt == config.end()) {
			return;
		}

		const ConfigItem& item = configIt->second;
		const auto newVal = coral::preferences::ScalarTextCodec<T>::format(value);
		saveToIniFile(key, newVal);
#if defined(_WIN32) || defined(_MSC_VER)
		// SHARED_MEMORY 策略：同时更新共享内存
		if (item.policy == StoragePolicy::SHARED_MEMORY) {
			if (m_sharedMemoryConfig && m_sharedMemoryConfig->ensureConnection()) {
				m_sharedMemoryConfig->setConfig(key, newVal);
			}
		}
#endif
	}

	static const std::unordered_map<std::string, ConfigItem>& getDefaultSettingsConfigMap();
	// INI 回退通道的变更检测：以文件 mtime 为代际，变化时重载 INI
	bool pollIniFileForUpdates();
	void saveToIniFile(const std::string& key, const std::string& value);
	void initialize();

private:
	bool m_isShowInfos = false;
	bool m_inited = false;
	mutable std::mutex m_mutex;
	std::string m_iniFilePath;
	std::unique_ptr<SettingsIniManager> m_configStore;
	// 最近一次观测到的共享内存代际（pollForUpdates 的比较基准）
	std::uint64_t m_lastConfigGeneration = 0;
	bool m_configGenerationValid = false;
	// INI 回退通道：最近一次观测到的 INI 修改时间
	std::filesystem::file_time_type m_lastIniWriteTime{};
	bool m_lastIniWriteTimeValid = false;
#if defined(_WIN32) || defined(_MSC_VER)
	std::unique_ptr<SharedMemoryConfig> m_sharedMemoryConfig;
#endif
};

#define sConfig (&SettingsConfig::getInstance())

#endif // SETTINGSCONFIG_H

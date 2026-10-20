#pragma once
#ifndef CORAL_MODULES_PREFERENCES_INI_STORE_H_INCLUDED
#define CORAL_MODULES_PREFERENCES_INI_STORE_H_INCLUDED

#include <string>
#include <vector>
#include <functional>
#include "thirdparty/simpleini/SimpleIni.h"
#include "scalar_text_codec.h"


namespace Settings {
	enum PinyinMode
	{
		QUANPIN = 0,
		DOUBLEPINYIN
	};
}

class IniFileManager {
public:
	explicit IniFileManager(const std::string& filename);
	IniFileManager(const IniFileManager&) = delete;
	IniFileManager& operator=(const IniFileManager&) = delete;
	IniFileManager(IniFileManager&&) = delete;
	IniFileManager& operator=(IniFileManager&&) = delete;
	virtual ~IniFileManager();
public:
	bool load();
	std::string getIniValue(const std::string& section, const std::string& key, const std::string& defaultValue = std::string());
	void setIniValue(const std::string& section, const std::string& key, const std::string& value);
	template<typename T>
	T getIniValueAs(const std::string& section, const std::string& key, const T& defaultValue = T()) {
		const std::string strValue = getIniValue(section, key, coral::preferences::ScalarTextCodec<T>::format(defaultValue));
		return coral::preferences::ScalarTextCodec<T>::parse(strValue);
	}
	
	template<typename T>
	void setIniValueAs(const std::string& section, const std::string& key, const T& value) {
		setIniValue(section, key, coral::preferences::ScalarTextCodec<T>::format(value));
	}

	void remove(const std::string& section, const std::string& key);
	void removeSection(const std::string& section);
	
	std::vector<std::string> getAllKeys(const std::string& section) const;
	std::vector<std::pair<std::string, std::string>> getSection(const std::string& section) const;
	bool updateAtomically(const std::function<void(IniFileManager &)> &updater, int timeoutMs = 1000);
	bool is_loaded() const;
private:
	bool save(const std::string& filename);

	CSimpleIniA m_ini;
	std::string m_filename;
	std::string m_currentGroup;
	bool m_isLoad;
};

class SettingsIniManager : public IniFileManager {
public:
	explicit SettingsIniManager(const std::string& filename);
	~SettingsIniManager() override;
	SettingsIniManager(const SettingsIniManager&) = delete;
	SettingsIniManager& operator=(const SettingsIniManager&) = delete;
	SettingsIniManager(SettingsIniManager&&) = delete;
	SettingsIniManager& operator=(SettingsIniManager&&) = delete;
	template<typename T>
	T getSettingsConfig(const std::string& key, const T& defaultValue = T()) {
		return getIniValueAs<T>("settingsconfig", key, defaultValue);
	}
	template<typename T>
	void setSettingsConfig(const std::string& key, const T& value) {
		setIniValueAs<T>("settingsconfig", key, value);
	}
};

// 模糊拼音开关条目数（fuzzy_choices 向量长度，settingsconfig 与引擎共用）
constexpr size_t FUZZY_CHOICES = 19;
enum class LexiconStatus
{
	Enabled,
	Disabled,
	LoadFailed // 加载失败
};
struct LexiconEntry
{
	int id = -1;
	bool installed = false;
	LexiconStatus status = LexiconStatus::Disabled;
	std::string versionId;
	std::string name;
	std::string trieIdentifier;
	std::string dataIdentifier;
};
struct LexiconConfigs
{
	std::string basePath;
	std::vector<LexiconEntry> lexicons;
	bool enableAll = false;
	bool disableAll = false;
};

#endif // SETTINGS_H

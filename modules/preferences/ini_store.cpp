// Phase 2：脱离消费者各自的 pch.h（PCH），改为显式依赖，使本文件可收口进独立 target
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <thread>
#include "ini_store.h"
#include "text_file_helpers.hpp"
#if defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
#include "system_mutex_lease.hpp"
#endif

IniFileManager::IniFileManager(const std::string &filename)
	: m_filename(filename), m_isLoad(false)
{
	m_ini.SetUnicode(true);
	load();
}

IniFileManager::~IniFileManager()
{
}

bool IniFileManager::load()
{
	if (m_ini.LoadFile(m_filename.c_str()) < 0) {
		return false;
	}
	m_isLoad = true;
	return true;
}

bool IniFileManager::save(const std::string& filename)
{
	return m_ini.SaveFile(filename.c_str(), true) >= 0;
}

void IniFileManager::setIniValue(const std::string &section, const std::string &key, const std::string &value)
{
	m_ini.SetValue(section.c_str(), key.c_str(), value.c_str());
}

std::string IniFileManager::getIniValue(const std::string &section, const std::string &key, const std::string &defaultValue)
{
	if (!m_isLoad) {
		load();
	}
	if (!m_ini.KeyExists(section.c_str(), key.c_str())) {
		return defaultValue;
	}
	const char* result = m_ini.GetValue(section.c_str(), key.c_str(), defaultValue.c_str());
	return result ? std::string(result) : defaultValue;
}

void IniFileManager::remove(const std::string& section, const std::string& key)
{
	m_ini.Delete(section.c_str(), key.c_str());
}

void IniFileManager::removeSection(const std::string& section)
{
	m_ini.Delete(section.c_str(), nullptr);
}

std::vector<std::string> IniFileManager::getAllKeys(const std::string &groupName) const
{
	std::vector<std::string> ret;
	CSimpleIniA::TNamesDepend keys;
	if (m_ini.GetAllKeys(groupName.c_str(), keys)){
		ret.reserve(keys.size());
		std::transform(keys.begin(), keys.end(), std::back_inserter(ret),
			[](const auto& key) { return key.pItem; });
	}
	return ret;
}

std::vector<std::pair<std::string, std::string>> IniFileManager::getSection(const std::string& groupName) const {
	std::vector<std::pair<std::string, std::string>> ret;
	const CSimpleIniA::TKeyVal* section = m_ini.GetSection(groupName.c_str());
	if (section) {
		ret.reserve(section->size());
		std::transform(section->begin(), section->end(), std::back_inserter(ret),
			[](const auto& kv) {
				return std::make_pair(std::string(kv.first.pItem), std::string(kv.second));
			});
	}
	return ret;
}

bool IniFileManager::updateAtomically(const std::function<void(IniFileManager&)>& updater, int timeoutMs) {
	const std::filesystem::path filePath(m_filename);
#if defined(_WIN32) || defined(_MSC_VER)
	std::wstring mtxName = L"CoralPinyinIniLock_" + filePath.filename().wstring();
	HANDLE hMutex = ::CreateMutexW(hlp::NtLPC_GetDefaultSecurity(), FALSE, mtxName.c_str());
	if (!hMutex) {
		return false;
	}

	DWORD wait = ::WaitForSingleObject(hMutex, static_cast<DWORD>(timeoutMs));
	if (!(wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)) {
		::CloseHandle(hMutex);
		return false;
	}
	struct MutexGuard {
		HANDLE h;
		~MutexGuard() { ::ReleaseMutex(h); ::CloseHandle(h); }
	} guard{hMutex};
#elif defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
	static std::mutex s_inifilemanagerMutex;
	ScopedLock lock(s_inifilemanagerMutex);
#endif

	load();
	updater(*this);

	try {
		std::filesystem::path tempPath = filePath;
		tempPath.replace_extension(filePath.extension().string() + ".tmp");
		if (this->save(tempPath.string())) {
#if defined(_WIN32) || defined(_MSC_VER)
            // 防病毒扫描等短暂占用会阻止替换；只对可恢复的占用错误有限等待。
            for (unsigned attempt = 0; attempt < 6; ++attempt) {
                if (::MoveFileExW(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    return true;
                const auto error = ::GetLastError();
                if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED)
                    return false;
                if (attempt < 5) std::this_thread::sleep_for(std::chrono::milliseconds(10u << attempt));
            }
            return false;
#else
			std::filesystem::rename(tempPath, filePath);
			return true;
#endif
		}
		std::filesystem::remove(tempPath);
		return false;
	} catch (const std::filesystem::filesystem_error&) {
		LOG_ERROR("updateAtomically failed" );
		return false;
	}
}

bool IniFileManager::is_loaded() const {
	return m_isLoad;
}

// SettingsIniManager 实现
SettingsIniManager::SettingsIniManager(const std::string& filename)
	: IniFileManager(filename) {}

SettingsIniManager::~SettingsIniManager() {}

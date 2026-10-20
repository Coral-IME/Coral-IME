#pragma once
#ifndef CORAL_MODULES_PREFERENCES_SHARED_PREFERENCES_H_INCLUDED
#define CORAL_MODULES_PREFERENCES_SHARED_PREFERENCES_H_INCLUDED

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
class SharedMemoryConfig {
public:
	SharedMemoryConfig();
	~SharedMemoryConfig();
	SharedMemoryConfig(const SharedMemoryConfig&) = delete;
	SharedMemoryConfig& operator=(const SharedMemoryConfig&) = delete;
	
	bool initialize();
	//融合成一个函数，只上一次锁
	bool initializeAndSetConfigs(const std::vector<std::pair<std::string, std::string>>& configs);
	bool setConfig(const std::string& key, const std::string& value);
	std::string getConfig(const std::string& key) const;
	bool setConfigs(const std::vector<std::pair<std::string, std::string>>& configs);
	bool ensureConnection() const;
	// 代际门闸：无锁读取当前共享内存代际（m_seqNum，写端每次更新 +2）。
	// 仅用于"配置是否变更"的检测，不需要读到稳定快照值；未连接时返回 false。
	bool getGeneration(std::uint64_t& generation) const;
	std::unordered_map<std::string, std::string> getAllConfigMap() const;

private:
	class Impl;
	std::unique_ptr<Impl> m_pImpl;
};

#endif // SHAREDMEMORYCONFIG_H

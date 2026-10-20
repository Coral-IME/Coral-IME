#include "shared_preferences.h"
#include "text_file_helpers.hpp"
#include "trace_log.h"
#include "system_mutex_lease.hpp"
#include <atomic>
#include <cstring>
#include <cstdint>
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "thirdparty/xxhash/xxhash.h"

namespace boost::interprocess::ipcdetail {
	template<class SizeType1, class SizeType2>
	inline SizeType1 get_rounded_size(SizeType1 origSize, SizeType2 roundTo)
	{
		return ((origSize - 1) / static_cast<SizeType1>(roundTo) + 1) * static_cast<SizeType1>(roundTo);
	}
}

#if defined(_WIN32) || defined(_MSC_VER)
#include <boost/interprocess/managed_windows_shared_memory.hpp>
#if defined(_M_IX86) || defined(_M_X64)
#include <intrin.h>
#endif
#elif defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
#include <boost/interprocess/managed_shared_memory.hpp>
#include <mutex>
#endif
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/indexes/iset_index.hpp>
#include <boost/interprocess/offset_ptr.hpp>

namespace bip = boost::interprocess;
namespace {
	//类型定义，用于共享内存中的标志位
	using ShmFlagType = std::uint32_t;

	//配置常量
	constexpr size_t MAX_KEY_LENGTH = 64;
	constexpr size_t MAX_VALUE_LENGTH = 256;
	constexpr size_t HASH_TABLE_SIZE = 512;//必须是2的整数次幂，否则会导致hash冲突，如果要修改成奇数需要同步修改哈希函数
	constexpr size_t CONFIG_SHM_SIZE = 1024 * 1024;
	//如果要修改共享内存结构一定要命名升级
	constexpr const char* const CONFIG_DATA_NAME = "ConfigDataRootCrossArchV4";
	constexpr const char* const CONFIG_SHM_NAME = "CORALPinyinConfigSharedMemoryV4";
	constexpr const char* const CONFIG_MUTEX_NAME = "CORALPinyinConfigSharedMemoryMutexV4";
	constexpr int MAX_SEQLOCK_RETRIES = 100;

	struct HashPair {
		uint64_t hash;
		uint64_t step;
	};

	inline HashPair computeHash(const char* key, size_t len) {
		uint64_t fullHash = XXH3_64bits(key, len);
		return {
			fullHash % HASH_TABLE_SIZE,
			((fullHash >> 32) % (HASH_TABLE_SIZE / 2)) * 2 + 1
		};
	}

	template<class T>
	using InteropOffsetPtr = bip::offset_ptr<T, std::int64_t, std::uint64_t>;
	
	using InteropMemAlgo = bip::rbtree_best_fit<
		bip::mutex_family,
		InteropOffsetPtr<void>
	>;
}

#if defined(_WIN32) || defined(_MSC_VER)
using SimpleManagedSharedMemory = bip::basic_managed_windows_shared_memory<
	char,
	InteropMemAlgo,
	bip::iset_index
>;
#elif defined(__linux__) || defined(__ANDROID__)|| defined(__APPLE__)
using SimpleManagedSharedMemory = bip::basic_managed_shared_memory<
	char,
	InteropMemAlgo,
	bip::iset_index
>;
#endif
//结构体内使用的类型必须是不同架构系统都支持、位数相同的类型
struct alignas(8) ConfigSlot {
	uint8_t m_occupied = 0;
	char m_key[MAX_KEY_LENGTH] = {};
	char m_value[MAX_VALUE_LENGTH] = {};
};

struct alignas(64) ConfigBuffer {
	ConfigSlot m_slots[HASH_TABLE_SIZE];
	ConfigBuffer() = default;
};

struct SharedConfigData {
	std::atomic<ShmFlagType> m_seqNum;
	std::atomic<ShmFlagType> m_activeIndex;
	std::atomic<ShmFlagType> m_initialized;
	ConfigBuffer m_buffers[2];
	
	SharedConfigData()
		: m_seqNum(0)
		, m_activeIndex(0)
		, m_initialized(0)
		, m_buffers{} {}
};

class SharedMemoryConfig::Impl {
public:
	Impl()
		: m_segment()
		, m_configData(nullptr)
		, m_sharedMemoryName()
		, m_mutexName()
		, m_initialized(false)
#if defined(_WIN32) || defined(_MSC_VER)
		, m_hMutex(nullptr)
#endif
	{
	}

	~Impl() {
#if defined(_WIN32) || defined(_MSC_VER)
		if (m_hMutex) {
			CloseHandle(m_hMutex);
			m_hMutex = nullptr;
		}
#endif
	}
	
	int findSlotInBuffer(const ConfigBuffer& buffer, const char* key, size_t len) const {
		HashPair hashPair = computeHash(key, len);
		for (size_t i = 0; i < HASH_TABLE_SIZE; ++i) {
			size_t idx = (hashPair.hash + i * hashPair.step) % HASH_TABLE_SIZE;
			const ConfigSlot& slot = buffer.m_slots[idx];
			if (!slot.m_occupied) {
				return -1;
			}
			if (std::strcmp(slot.m_key, key) == 0) {
				return static_cast<int>(idx);
			}
		}
		
		return -1;
	}
	
	int findSlotInBufferForWrite(const ConfigBuffer& buffer, const char* key, size_t len) const {
		HashPair hashPair = computeHash(key, len);
		for (size_t i = 0; i < HASH_TABLE_SIZE; ++i) {
			size_t idx = (hashPair.hash + i * hashPair.step) % HASH_TABLE_SIZE;
			const ConfigSlot& slot = buffer.m_slots[idx];
			
			if (!slot.m_occupied || std::strcmp(slot.m_key, key) == 0) {
				return static_cast<int>(idx);
			}
		}
		
		return -1;
	}

	bool initialize() {
		if (m_initialized) {
			return true;
		}

		try {
			m_sharedMemoryName = coral::testing::objectName(CONFIG_SHM_NAME);
			m_mutexName = coral::testing::objectName(CONFIG_MUTEX_NAME);
#if defined(_WIN32) || defined(_MSC_VER)
			if (hlp::shouldOnlyOpenSharedMemory()) {
				m_hMutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, std::wstring(m_mutexName.begin(), m_mutexName.end()).c_str());
			} else {
				m_hMutex = CreateMutexW(hlp::NtLPC_GetDefaultSecurity(), FALSE, std::wstring(m_mutexName.begin(), m_mutexName.end()).c_str());
			}
			if (!m_hMutex) {
				LOG_ERROR("Failed to create Windows or open mutex in UWP service mode: " + std::to_string(GetLastError()));
				return false;
			}
#endif
			ScopedLock createLock(m_hMutex);

			if (connectToSharedMemory()) {
				m_initialized = true;
				return true;
			}
			if (createSharedMemory()) {
				m_initialized = true;
				return true;
			}

			return false;
		}
		catch (const std::exception& e) {
			LOG_ERROR("SharedMemoryConfig initialize failed: " + std::string(e.what()));
			return false;
		}
	}

	bool ensureConnection() const {
		if (m_initialized && m_configData) {
			return true;
		}
		return const_cast<SharedMemoryConfig::Impl*>(this)->tryReconnect();
	}

	bool tryReconnect() {
		m_initialized = false;
		m_configData = nullptr;
#if defined(_WIN32) || defined(_MSC_VER)
		if (hlp::shouldOnlyOpenSharedMemory()) {
			if (m_hMutex) {
				CloseHandle(m_hMutex);
				m_hMutex = nullptr;
			}
			m_hMutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, std::wstring(m_mutexName.begin(), m_mutexName.end()).c_str());
			if (!m_hMutex) {
				return false;
			}
		}
#endif
		if (connectToSharedMemory()) {
			m_initialized = true;
			LOG_TRACE("Reconnected to config shared memory");
			return true;
		}
		LOG_TRACE("SharedMemoryConfig tryReconnect failed");
		return false;
	}

	bool setConfig(const std::string& key, const std::string& value) {
		if (!ensureConnection()) {
			return false;
		}
		
		if (key.length() >= MAX_KEY_LENGTH) {
			LOG_ERROR("SharedMemoryConfig set config failed: key too long: " + key);
			return false;
		}
		if (value.length() >= MAX_VALUE_LENGTH) {
			LOG_ERROR("SharedMemoryConfig set config failed: value too long for key: " + key);
			return false;
		}
		
		try {
			ScopedLock lock(m_hMutex);
			
			ShmFlagType seq = m_configData->m_seqNum.load(std::memory_order_acquire);
			m_configData->m_seqNum.store(seq + 1, std::memory_order_release);
			
			ShmFlagType currentIdx = m_configData->m_activeIndex.load(std::memory_order_acquire);
			ShmFlagType nextIdx = 1 - currentIdx;
			
			m_configData->m_buffers[nextIdx] = m_configData->m_buffers[currentIdx];
			int slotIdx = findSlotInBufferForWrite(m_configData->m_buffers[nextIdx], key.c_str(), key.length());
			if (slotIdx < 0) {
				m_configData->m_seqNum.store(seq + 2, std::memory_order_release);
				LOG_ERROR("SharedMemoryConfig set config failed: hash table full for key: " + key);
				return false;
			}
			
			ConfigSlot& slot = m_configData->m_buffers[nextIdx].m_slots[slotIdx];
			size_t key_length = key.copy(slot.m_key, MAX_KEY_LENGTH - 1);
			slot.m_key[key_length] = '\0';
			size_t valueLen = value.copy(slot.m_value, MAX_VALUE_LENGTH - 1);
			slot.m_value[valueLen] = '\0';
			slot.m_occupied = 1;
			
			m_configData->m_activeIndex.store(nextIdx, std::memory_order_release);
			m_configData->m_seqNum.store(seq + 2, std::memory_order_release);
			
			return true;
		}
		catch (const std::exception& e) {
			LOG_ERROR("SharedMemoryConfig set config failed: " + std::string(e.what()));
			return false;
		}
	}

std::string getConfig(const std::string& key) const {
	if (!ensureConnection()) {
		return std::string{};
	}
	
	if (key.length() >= MAX_KEY_LENGTH) {
		return std::string{};
	}
	
	try {
		ShmFlagType seq1 = 0, seq2 = 0;
		std::string result;
		bool found = false;
		int retries = 0;
		//这里进行循环的可能性非常低，几乎不可能，但是有概率，场景就是：一次读者周期比多次写者周期要长，导致写者周期结束时，读者周期还未结束，导致读者读到错误的数据
		do {
			seq1 = m_configData->m_seqNum.load(std::memory_order_acquire);
			
			ShmFlagType activeIdx = m_configData->m_activeIndex.load(std::memory_order_acquire);
			const ConfigBuffer& buffer = m_configData->m_buffers[activeIdx];
			
			int slotIdx = findSlotInBuffer(buffer, key.c_str(), key.length());
			if (slotIdx >= 0) {
				const ConfigSlot& slot = buffer.m_slots[slotIdx];
				if (slot.m_occupied) {
					result = slot.m_value;
					found = true;
				}
			}
			seq2 = m_configData->m_seqNum.load(std::memory_order_acquire);
			if (seq1 == seq2) {
				break;
			}
			++retries;
			if (retries >= MAX_SEQLOCK_RETRIES) {
				LOG_ERROR("SharedMemoryConfig get config failed: too many retries for key: " + key);
				return std::string{};
			}
		} while (true);
		
		return found ? result : std::string{};
	}
	catch (const std::exception& e) {
		LOG_ERROR("SharedMemoryConfig get config failed: " + std::string(e.what()));
		return std::string{};
	}
}

	bool getGeneration(std::uint64_t& generation) const {
		if (!ensureConnection()) {
			return false;
		}
		// seqlock 写者先 +1（奇数=写入中），完成后 +2（回到偶数稳定态）。
		// 门闸只做变化检测：读到奇数同样视为已变化，下一次读取自然拿到稳定值，
		// 因此这里无需 seqlock 重试循环。
		generation = static_cast<std::uint64_t>(
			m_configData->m_seqNum.load(std::memory_order_acquire));
		return true;
	}

	bool setConfigs(const std::vector<std::pair<std::string, std::string>>& configs) {
		if (!ensureConnection()) {
			return false;
		}
		
		try {
			ScopedLock lock(m_hMutex);
			return setConfigsInternal(configs);
		}
		catch (const std::exception& e) {
			LOG_ERROR("SharedMemoryConfig set configs failed: " + std::string(e.what()));
			return false;
		}
	}

	std::unordered_map<std::string, std::string> getAllConfigMap() const {
		std::unordered_map<std::string, std::string> result;
		if (!ensureConnection()) {
			return result;
		}
		//这里进行循环的可能性非常低，几乎不可能，但是有概率，场景就是：一次读者周期比多次写者周期要长，导致写者周期结束时，读者周期还未结束，导致读者读到错误的数据
		try {
			ShmFlagType seq1 = 0, seq2 = 0;
			int retries = 0;
			do {
				seq1 = m_configData->m_seqNum.load(std::memory_order_acquire);
				ShmFlagType activeIdx = m_configData->m_activeIndex.load(std::memory_order_acquire);
				const ConfigBuffer& buffer = m_configData->m_buffers[activeIdx];
				result.clear();
				for (size_t i = 0; i < HASH_TABLE_SIZE; ++i) {
					const ConfigSlot& slot = buffer.m_slots[i];
					if (slot.m_occupied) {
						result.emplace(slot.m_key, slot.m_value);
					}
				}
				seq2 = m_configData->m_seqNum.load(std::memory_order_acquire);
				if (seq1 == seq2) {
					break;
				}
				++retries;
				if (retries >= MAX_SEQLOCK_RETRIES) {
					LOG_ERROR("SharedMemoryConfig get all configs failed: too many retries");
					result.clear();
					break;
				}
			} while (true);
		}
		catch (const std::exception& e) {
			LOG_ERROR("SharedMemoryConfig get all configs failed: " + std::string(e.what()));
		}
		return result;
	}

	bool initializeAndSetConfigs(const std::vector<std::pair<std::string, std::string>>& configs) {
		if (m_initialized) {
			return setConfigs(configs);
		}

		try {
			m_sharedMemoryName = coral::testing::objectName(CONFIG_SHM_NAME);
			m_mutexName = coral::testing::objectName(CONFIG_MUTEX_NAME);
#if defined(_WIN32) || defined(_MSC_VER)
			if (hlp::shouldOnlyOpenSharedMemory()) {
				m_hMutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, std::wstring(m_mutexName.begin(), m_mutexName.end()).c_str());
			} else {
				m_hMutex = CreateMutexW(hlp::NtLPC_GetDefaultSecurity(), FALSE, std::wstring(m_mutexName.begin(), m_mutexName.end()).c_str());
			}
			if (!m_hMutex) {
				LOG_ERROR("Failed to create Windows or open mutex in UWP service mode: " + std::to_string(GetLastError()));
				return false;
			}
#endif
			ScopedLock createLock(m_hMutex);

			bool initSuccess = false;
			if (connectToSharedMemory()) {
				initSuccess = true;
			} else if (createSharedMemory()) {
				initSuccess = true;
			}

			if (!initSuccess) {
				return false;
			}

			m_initialized = true;
			return setConfigsInternal(configs);
		}
		catch (const std::exception& e) {
			LOG_ERROR("SharedMemoryConfig initializeAndSetConfigs failed: " + std::string(e.what()));
			return false;
		}
	}

private:
	//不加锁的内部实现，假设调用者已持有锁
	bool setConfigsInternal(const std::vector<std::pair<std::string, std::string>>& configs) {
		if (configs.empty()) {
			return true;
		}
		ShmFlagType seq = m_configData->m_seqNum.load(std::memory_order_acquire);
		m_configData->m_seqNum.store(seq + 1, std::memory_order_release);
		ShmFlagType currentIdx = m_configData->m_activeIndex.load(std::memory_order_acquire);
		ShmFlagType nextIdx = 1 - currentIdx;
		m_configData->m_buffers[nextIdx] = m_configData->m_buffers[currentIdx];
		for (const auto& config : configs) {
			const std::string& key = config.first;
			const std::string& value = config.second;
			if (key.length() >= MAX_KEY_LENGTH || value.length() >= MAX_VALUE_LENGTH) {
				LOG_ERROR("SharedMemoryConfig set configs failed: key or value too long: " + key);
				continue;
			}
			int slotIdx = findSlotInBufferForWrite(m_configData->m_buffers[nextIdx], key.c_str(), key.length());
			if (slotIdx < 0) {
				LOG_ERROR("SharedMemoryConfig set configs failed: hash table full for key: " + key);
				continue;
			}
			ConfigSlot& slot = m_configData->m_buffers[nextIdx].m_slots[slotIdx];
			size_t key_length = key.copy(slot.m_key, MAX_KEY_LENGTH - 1);
			slot.m_key[key_length] = '\0';
			size_t valueLen = value.copy(slot.m_value, MAX_VALUE_LENGTH - 1);
			slot.m_value[valueLen] = '\0';
			slot.m_occupied = 1;
		}
		m_configData->m_activeIndex.store(nextIdx, std::memory_order_release);
		m_configData->m_seqNum.store(seq + 2, std::memory_order_release);
		return true;
	}

	bool createSharedMemory() {
		try {
#if defined(_WIN32) || defined(_MSC_VER)
			if(hlp::isAppContainer()) {
				return false;
			}
#endif
			bip::shared_memory_object::remove(m_sharedMemoryName.c_str());
			boost::interprocess::permissions perm;
#if defined(_WIN32) || defined(_MSC_VER)
			perm.set_permissions(hlp::NtLPC_GetDefaultSecurity());
#else
			perm.set_unrestricted();
#endif
			m_segment = SimpleManagedSharedMemory(
				bip::create_only,
				m_sharedMemoryName.c_str(),
				CONFIG_SHM_SIZE,
				nullptr,
				perm
			);
			m_configData = m_segment.construct<SharedConfigData>(CONFIG_DATA_NAME)();
			if (!m_configData) {
				return false;
			}
			m_configData->m_initialized.store(1, std::memory_order_release);
			LOG_TRACE("Created new config shared memory: " + m_sharedMemoryName);
			return true;
		}
		catch (const std::exception& e) {
			LOG_ERROR("SharedMemoryConfig create shared memory failed: " + std::string(e.what()));
			return false;
		}
	}

	bool connectToSharedMemory() {
		if (!hlp::ShmExistsByName(m_sharedMemoryName)) {
			return false;
		}
		try {
			m_segment = SimpleManagedSharedMemory(bip::open_only, m_sharedMemoryName.c_str());
			auto result = m_segment.find<SharedConfigData>(CONFIG_DATA_NAME);
			m_configData = result.first;

			if (!m_configData || !m_configData->m_initialized.load(std::memory_order_acquire)) {
				return false;
			}
			LOG_TRACE("Connected to existing config shared memory");
			return true;
		}
		catch (const std::exception& e) {
			LOG_TRACE("SharedMemoryConfig connect to shared memory failed: " + std::string(e.what()) + " (If this is the first connection, the next step will create it.)");
			return false;
		}
	}

private:
	SimpleManagedSharedMemory m_segment;
	SharedConfigData* m_configData;
	std::string m_sharedMemoryName;
	std::string m_mutexName;
	bool m_initialized;
#if defined(_WIN32) || defined(_MSC_VER)
	HANDLE m_hMutex;
#else
	mutable std::mutex m_hMutex;
#endif
};

SharedMemoryConfig::SharedMemoryConfig() : m_pImpl(std::make_unique<Impl>()) {}
SharedMemoryConfig::~SharedMemoryConfig() = default;

bool SharedMemoryConfig::initialize() {
	return m_pImpl->initialize();
}

bool SharedMemoryConfig::initializeAndSetConfigs(const std::vector<std::pair<std::string, std::string>>& configs) {
	return m_pImpl->initializeAndSetConfigs(configs);
}

bool SharedMemoryConfig::setConfig(const std::string & key, const std::string & value) {
	return m_pImpl->setConfig(key, value);
}

std::unordered_map<std::string, std::string> SharedMemoryConfig::getAllConfigMap() const {
	return m_pImpl->getAllConfigMap();
}

std::string SharedMemoryConfig::getConfig(const std::string & key) const {
	return m_pImpl->getConfig(key);
}

bool SharedMemoryConfig::setConfigs(const std::vector<std::pair<std::string, std::string>>& configs) {
	return m_pImpl->setConfigs(configs);
}

bool SharedMemoryConfig::ensureConnection() const {
	return m_pImpl->ensureConnection();
}

bool SharedMemoryConfig::getGeneration(std::uint64_t& generation) const {
	return m_pImpl->getGeneration(generation);
}

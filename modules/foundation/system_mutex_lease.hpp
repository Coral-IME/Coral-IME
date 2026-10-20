#pragma once
#ifndef CORAL_MODULES_FOUNDATION_SYSTEM_MUTEX_LEASE_HPP_INCLUDED
#define CORAL_MODULES_FOUNDATION_SYSTEM_MUTEX_LEASE_HPP_INCLUDED

#include <string>
#if defined(_WIN32) || defined(_MSC_VER)
#include <Windows.h>

class ScopedLock {
public:
	explicit ScopedLock(const HANDLE hMutex, DWORD timeoutMs = INFINITE) : m_hMutex(hMutex) {
		if (m_hMutex == nullptr || m_hMutex == INVALID_HANDLE_VALUE) {
			LOG_ERROR("ScopedLock: Invalid mutex handle provided.");
			return;
		}
		const DWORD waitResult = WaitForSingleObject(m_hMutex, timeoutMs);
		if (WAIT_OBJECT_0 == waitResult || WAIT_ABANDONED == waitResult) {
			m_acquired = true;
		} else if (WAIT_TIMEOUT == waitResult) {
			LOG_ERROR(std::string("ScopedLock: Timeout after ") + std::to_string(timeoutMs) + " ms");
			m_acquired = false;
		} else {
			LOG_ERROR(std::string("ScopedLock: Failed to acquire mutex. Error: ") + std::to_string(GetLastError()));
		}
	}

	~ScopedLock() {
		if (m_acquired && m_hMutex && m_hMutex != INVALID_HANDLE_VALUE) {
			if (!ReleaseMutex(m_hMutex)) {
				LOG_ERROR(std::string("ScopedLock: Failed to release mutex. Error=") + std::to_string(GetLastError()));
			}
		}
	}

	bool isAcquired() const { return m_acquired; }

	ScopedLock(const ScopedLock&) = delete;
	ScopedLock& operator=(const ScopedLock&) = delete;
	ScopedLock(ScopedLock&&) = delete;
	ScopedLock& operator=(ScopedLock&&) = delete;

private:
	HANDLE	m_hMutex;
	bool	m_acquired = false;
};

#elif defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
#include <mutex>

class ScopedLock {
public:
	explicit ScopedLock(std::mutex& m, int timeoutMs = -1) : m_mutex(&m), m_acquired(false) {
		if (timeoutMs < 0) {
			// 永久等待
			m_mutex->lock();
			m_acquired = true;
		} else {
			// 注意：std::mutex 不支持超时，这里简单记录警告
			// 实际使用中如需超时功能，应使用 std::timed_mutex
			LOG_ERROR("ScopedLock: std::mutex does not support timeout on Unix-like systems. Using blocking lock.");
			m_mutex->lock();
			m_acquired = true;
		}
	}
	
	~ScopedLock() { 
		if (m_acquired && m_mutex) {
			m_mutex->unlock();
		}
	}

	bool isAcquired() const { return m_acquired; }

	ScopedLock(const ScopedLock&) = delete;
	ScopedLock& operator=(const ScopedLock&) = delete;
	ScopedLock(ScopedLock&&) = delete;
	ScopedLock& operator=(ScopedLock&&) = delete;

private:
	std::mutex* m_mutex;
	bool m_acquired;
};
#endif

#endif // SCOPEDLOCK_HPP（作用域锁头文件）

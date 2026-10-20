// -----------------------------------------------------------------------
// 创建人	:	黄智寿
// 创建时间:	2012:7:4 10:16:06
// 功能描述 :	管道实例管理器
// -----------------------------------------------------------------------
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_IPC_IPCSERVER_SERVER_PIPEMGR_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_IPC_IPCSERVER_SERVER_PIPEMGR_H_INCLUDED

#include <vector>
#include <new>
#include "autolock.h"
#include "securityattribute.h"

#define INSTANCES 16
#define BUFSIZE 4096

class CPipeMgr
{
public:
	typedef HANDLE PIPE_HANDLE;

public:
	BOOL Init(LPCWSTR szPipeName, INT nInstancesNum = INSTANCES)
	{
		if (nInstancesNum <= 0 || nInstancesNum >= PIPE_UNLIMITED_INSTANCES)
		{
			::SetLastError(ERROR_INVALID_PARAMETER);
			return FALSE;
		}
		try
		{
			m_allPipes.assign(nInstancesNum, INVALID_HANDLE_VALUE);
			m_available.assign(nInstancesNum, INVALID_HANDLE_VALUE);
		}
		catch (const std::bad_alloc&)
		{
			UnInit();
			::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return FALSE;
		}
		for (int i = 0; i < nInstancesNum; ++i)
		{
			PIPE_HANDLE hPipe = ::CreateNamedPipeW(
				szPipeName,
				PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
				    (i == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
				PIPE_READMODE_MESSAGE | PIPE_TYPE_MESSAGE | PIPE_WAIT,
				nInstancesNum,
				BUFSIZE, BUFSIZE,
				NMPWAIT_USE_DEFAULT_WAIT,
				m_sa);

			if (hPipe == INVALID_HANDLE_VALUE)
			{
				const DWORD error = ::GetLastError();
				UnInit();
				::SetLastError(error);
				return FALSE;
			}
			m_allPipes[i] = hPipe;
			m_available[i] = hPipe;
			++m_availableCount;
		}
		return TRUE;
	}

	void UnInit()
	{
		CAutoLock _lock(&m_cs);
		for (HANDLE pipe : m_allPipes)
			if (pipe != INVALID_HANDLE_VALUE) ::CloseHandle(pipe);
		m_allPipes.clear();
		m_available.clear();
		m_availableHead = m_availableCount = 0;
	}

	PIPE_HANDLE CreatePipe()
	{
		CAutoLock _lock(&m_cs);
		if (m_availableCount == 0)
			return INVALID_HANDLE_VALUE;

		PIPE_HANDLE hPipe = m_available[m_availableHead];
		m_availableHead = (m_availableHead + 1) % m_available.size();
		--m_availableCount;
		return hPipe;
	}

	void ClosePipe(PIPE_HANDLE hPipe)
	{
		CAutoLock _lock(&m_cs);
		if (hPipe != INVALID_HANDLE_VALUE)
		{
			::DisconnectNamedPipe(hPipe);
			// 固定容量 FIFO：归还实例绝不分配内存。
			m_available[(m_availableHead + m_availableCount) % m_available.size()] = hPipe;
			++m_availableCount;
		}
	}

private:
	std::vector<HANDLE> m_allPipes;
	std::vector<HANDLE> m_available;
	size_t m_availableHead = 0;
	size_t m_availableCount = 0;
	CCriticalSection m_cs;
	CSecurityAttribute m_sa;
};

#endif // PIPEMGR_H

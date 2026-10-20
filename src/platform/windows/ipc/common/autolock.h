// -----------------------------------------------------------------------
// 创建人	:	黄智寿
// 创建时间:	2011:7:16 16:51:03
// 功能描述 :	自动锁
// -----------------------------------------------------------------------
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_IPC_COMMON_AUTOLOCK_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_IPC_COMMON_AUTOLOCK_H_INCLUDED
#include <atlsync.h>
class CAutoLock
{
public:
	CAutoLock(CCriticalSection* cs)
		: m_cs(cs)
	{
		cs->Enter();
	}

	~CAutoLock()
	{
		m_cs->Leave();
	}

private:
	CCriticalSection* m_cs;
};

#endif // AUTOLOCK_H

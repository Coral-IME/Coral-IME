/*
* @author: huangzhishou
* @date: 2021-07-30 11:40:04
*/
#include "pch.h"
#include "recursive_lock.h"

CLock::CLock()
{
#ifdef OS_WINDOW
	::InitializeCriticalSection(&mLock);
#else
	pthread_mutex_init(&mLock, nullptr);
#endif
}

CLock::~CLock()
{
#ifdef OS_WINDOW
	::DeleteCriticalSection(&mLock);
#else
	pthread_mutex_destroy(&mLock);
#endif
}

void CLock::enter()
{
#ifdef OS_WINDOW
	::EnterCriticalSection(&mLock);
#else
	pthread_mutex_lock(&mLock);
#endif
}

bool CLock::tryEnter()
{
#ifdef OS_WINDOW
	return ::TryEnterCriticalSection(&mLock) ? true : false;
#else
	return pthread_mutex_trylock(&mLock) == 0 ? true : false;
#endif
}
void CLock::leave()
{
#ifdef OS_WINDOW
	::LeaveCriticalSection(&mLock);
#else
	pthread_mutex_unlock(&mLock);
#endif
}

CAutoLock::CAutoLock(CLock* lock)
{
	lock->enter();
	mLock = lock;
}
CAutoLock::~CAutoLock()
{
	mLock->leave();
}
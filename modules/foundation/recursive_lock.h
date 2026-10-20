/*
* @author: huangzhishou
* @date: 2021-07-30 11:40:04
*/
#pragma once
#ifndef CORAL_MODULES_FOUNDATION_RECURSIVE_LOCK_H_INCLUDED
#define CORAL_MODULES_FOUNDATION_RECURSIVE_LOCK_H_INCLUDED

#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

class CLock
{
public:
	CLock();
	~CLock();
	void enter();
	bool tryEnter();
	void leave();

private:
#ifdef OS_WINDOW
	CRITICAL_SECTION mLock{};
#else
	pthread_mutex_t mLock{};
#endif
};

class CAutoLock
{
public:
	explicit CAutoLock(CLock*);
	~CAutoLock();
private:
	CLock* mLock;
};

#endif // LOCK_H

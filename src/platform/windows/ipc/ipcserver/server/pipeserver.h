// -----------------------------------------------------------------------
// 创建人	:	黄智寿
// 创建时间:	2012:4:26 11:43:17
// 功能描述 :	
// -----------------------------------------------------------------------
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_IPC_IPCSERVER_SERVER_PIPESERVER_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_IPC_IPCSERVER_SERVER_PIPESERVER_H_INCLUDED
#include "pipemgr.h"
#include <mutex>
#include <queue>

typedef void (CALLBACK* PFN_CallBack)(LPVOID pContext, LPCWSTR szData, CString& strResult);
class CPipeServer
{
public:
	CPipeServer();
	~CPipeServer();
	HRESULT Start(LPCWSTR szPipeName, long nDealThreadNum, PFN_CallBack pfn, LPVOID pcbContext);
	void Stop();
	bool IsRunning() const { return m_bIsRunning != FALSE; }

private:
	static unsigned CALLBACK _ConnectThread(LPVOID lParam);
	static void CALLBACK _DealConnectPipe(LPVOID lParam, LPVOID lParam2);
	static void CALLBACK _WorkCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK);
	void _CloseResources();

private:
	PFN_CallBack m_pfnCallback;
	LPVOID m_CallbackContext;
	CString m_strPipeName;
	PTP_POOL m_pool = nullptr;
	PTP_WORK m_work = nullptr;
	TP_CALLBACK_ENVIRON m_environment{};
	std::queue<HANDLE> m_jobs;
	std::mutex m_jobsMutex;
	BOOL m_bIsRunning;

	HANDLE m_hThread;
	HANDLE m_stopEvent = nullptr;
	CPipeMgr m_PipeMgr;
};

#endif // PIPESERVER_H

// -----------------------------------------------------------------------
// 创建人	:	黄智寿
// 创建时间:	2012:4:26 18:45:36
// 功能描述 :	
// -----------------------------------------------------------------------
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_IPC_IPCCLIENT_CLIENT_PIPECLIENT_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_IPC_IPCCLIENT_CLIENT_PIPECLIENT_H_INCLUDED

class CPipeClient
{
public:
	CPipeClient();
	~CPipeClient();
	CPipeClient(const CPipeClient&) = delete;
	CPipeClient& operator=(const CPipeClient&) = delete;
	HRESULT ConnectServer(LPCWSTR szPipeName, int nRetry);
	HRESULT SendToServer(const void* buffer, DWORD dwToSend, PDWORD dwHasSend);
	HRESULT RecvFromServer(void* buffer, DWORD dwToRecv, PDWORD dwHasRecv);
	void DisconnectServer();

private:
	HRESULT _TryConnectServer(LPCWSTR szPipe);
private:
	HANDLE m_hPipe;
	ULONGLONG m_deadline;
};

#endif // PIPECLIENT_H

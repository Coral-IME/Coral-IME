// -----------------------------------------------------------------------
// 创建人	:	黄智寿
// 创建时间:	2011:7:16 11:05:54
// 功能描述 :	
// -----------------------------------------------------------------------
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_IPC_INCLUDE_IPCSERVER_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_IPC_INCLUDE_IPCSERVER_H_INCLUDED

#include <vector>
#include <map>
#include <mutex>
#include <atlpath.h>

class CIpcResponse
{
	friend class CIpcServer;
public:
	CIpcResponse() : m_hr(E_FAIL) {}
	void AddKeyValue(const CString& szKey, const CString& szValue);
	void AddKeyValue(const CString& szKey, long nValue);
	CString getResponse(bool percentEncoding = true);
protected:
	void SetResultCode(HRESULT hr);

private:
	std::map<CString, CString> m_mapKV;
	HRESULT m_hr;
};

typedef HRESULT (CALLBACK* PFN_CommonCall)(LPVOID pContext, const std::vector<CString>& params, CIpcResponse& result);
class CPipeServer;
class CIpcServer
{
	typedef std::map<CString, PFN_CommonCall> MapApis;
	typedef MapApis::iterator MapApis_It;
	typedef MapApis::const_iterator MapApis_Const_It;

public:
	explicit CIpcServer();
	~CIpcServer();
	HRESULT RegisterApi(LPCWSTR szFuncName, PFN_CommonCall pFuncAddress);
	HRESULT Start(LPCWSTR szPipeName, LPVOID pContext, int nThreadNum);
	HRESULT Stop();

private:
	static void CALLBACK _GetPipeData(LPVOID pContext, LPCWSTR szData, CString& strResult);

private:
	MapApis m_Apis;
	std::mutex m_apiMutex;
	LPVOID m_pContext;
	CPipeServer* m_PipeSvr;
};

///////////////////////////////////////////////////////////////////////////////
///

#define BEGIN_API_MAP(CLASS)\
	HRESULT RegisterApi()\
	{\
		HRESULT hr = S_OK;\

#define BIND_API(NAME, API)\
	hr = m_server.RegisterApi(NAME, API);\
	if (FAILED(hr))\
		return hr;

#define END_API_MAP()\
		return hr;\
	}

template<class T>
class CIpcServerImpl
{
public:
	static T& Instance()
	{
		static T inst;
		return inst;
	}

	virtual HRESULT RegisterApi() = 0;
	virtual HRESULT DoInit()
	{
		return S_OK;
	}
	virtual void DoUnInit()
	{

	}
	HRESULT Init(LPCWSTR szPipeName, int nThreadNum = -1)
	{
		if (m_initialized) return S_FALSE;
		HRESULT hr = DoInit();
		if (FAILED(hr))
			return hr;
		
		hr = ((T*)this)->RegisterApi();
		if (SUCCEEDED(hr))
			hr = m_server.Start(szPipeName, (T*)this, nThreadNum);
		if (FAILED(hr))
		{
			DoUnInit();
			return hr;
		}
		m_initialized = true;
		return hr;
	}
	void Uninit()
	{
		if (!m_initialized) return;
		m_server.Stop();
		DoUnInit();
		m_initialized = false;
	}

protected:
	CIpcServer m_server;
	bool m_initialized = false;
};

#endif // IPCSERVER_H

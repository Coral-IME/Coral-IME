// -----------------------------------------------------------------------
// 创建人	:	黄智寿
// 创建时间:	2011:7:16 17:40:25
// 功能描述 :	
// -----------------------------------------------------------------------
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_IPC_INCLUDE_IPCCLIENT_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_IPC_INCLUDE_IPCCLIENT_H_INCLUDED

#include <vector>
#include <map>

class CIpcClient
{
	typedef std::vector<CString> VecParams;
	typedef VecParams::iterator VecParams_It;

	typedef std::map<CString, CString> MapKv;
	typedef MapKv::iterator MapKv_It;
	typedef MapKv::const_iterator MapKv_Const_It;

public:
	CIpcClient(LPCWSTR szPipeName);
	void Add(LPCWSTR szParam);
	void Add(long nParam);
	HRESULT Call(LPCWSTR szMethod);
	CString Get(LPCWSTR szKey);
	void Reset();

private:
	static HRESULT _SendTo(LPCWSTR szPipeName, LPCWSTR szSend, CString& strReply, int nRetry);
	void _LoadResult(LPCWSTR szData);

private:
	VecParams m_vecParams;
	CString m_strPipeName;
	MapKv m_ResultKv;
};

#endif // IPCCLIENT_H

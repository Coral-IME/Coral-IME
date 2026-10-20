#include "pch.h"
#include "ipcserver.h"
#include "pipeserver.h"
#include "data.h"

void CIpcResponse::AddKeyValue(const CString& szKey, const CString& szValue)
{
	m_mapKV[szKey] = szValue;
}

void CIpcResponse::AddKeyValue(const CString& szKey, long nValue)
{
	CString szValue;
	szValue.Format(L"%ld", nValue);
	m_mapKV[szKey] = szValue;
}

void CIpcResponse::SetResultCode(HRESULT hr)
{
	m_hr = hr;
}
CString CIpcResponse::getResponse(bool percentEncoding)
{
	AddKeyValue(CDataParser::RESULT_CODE, m_hr);
	return CDataParser::ServerMerge(m_mapKV, percentEncoding);
}

//////////////////////////////////////////////////////////////////////////
//
CIpcServer::CIpcServer()
	:m_pContext(NULL)
	, m_PipeSvr(new CPipeServer)
{
}

CIpcServer::~CIpcServer()
{
	Stop();
	delete m_PipeSvr;
}

HRESULT CIpcServer::RegisterApi(LPCWSTR szFuncName, PFN_CommonCall pFuncAddress)
{
	if (ipc_hlp::IsStrEmpty(szFuncName) || pFuncAddress == NULL)
		return E_INVALIDARG;

	std::lock_guard<std::mutex> lock(m_apiMutex);
	MapApis_It it = m_Apis.find(szFuncName);
	if (it != m_Apis.end())
		it->second = pFuncAddress;
	else
		m_Apis.insert(MapApis::value_type(szFuncName, pFuncAddress));
	return S_OK;
}


HRESULT CIpcServer::Start(LPCWSTR szPipeName, LPVOID pContext, int nThreadNum)
{
	if (ipc_hlp::IsStrEmpty(szPipeName))
		return E_INVALIDARG;
	if (m_PipeSvr->IsRunning()) return S_FALSE;
	m_pContext = pContext;

	return m_PipeSvr->Start(szPipeName, nThreadNum <= 0 ? 2 : nThreadNum,
	                       _GetPipeData, this);
}

HRESULT CIpcServer::Stop()
{
	m_PipeSvr->Stop();
	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
//
void CALLBACK CIpcServer::_GetPipeData(LPVOID pContext, LPCWSTR szData, CString& strResult)
{
	CIpcServer* pThis = (CIpcServer*)pContext;
	ATLASSERT(pThis);
	CIpcResponse response;
	response.SetResultCode(E_FAIL);
	{
		if (ipc_hlp::IsStrEmpty(szData))
		{
			response.SetResultCode(E_INVALIDARG);
			goto Exit0;
		}

		std::vector<CString> vec;
		CDataParser::ServerSplit(szData, vec);
		if (vec.size() == 0)
		{
			response.SetResultCode(E_INVALIDARG);
			goto Exit0;
		}
		CString strMethodName = vec[0];
		vec.erase(vec.begin());

		PFN_CommonCall pFuntion = nullptr;
		{
			std::lock_guard<std::mutex> lock(pThis->m_apiMutex);
			MapApis_Const_It it = pThis->m_Apis.find(strMethodName);
			if (it != pThis->m_Apis.end()) pFuntion = it->second;
		}
		if (!pFuntion)
		{
			response.SetResultCode(E_NOINTERFACE);
			goto Exit0;
		}
		HRESULT hr = pFuntion(pThis->m_pContext, vec, response);
		response.SetResultCode(hr);
	}
Exit0:
	strResult = response.getResponse(CDataParser::IsPercentEncoded(szData));
}

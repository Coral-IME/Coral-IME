#include "pch.h"
#include "ipcclient.h"
#include "pipeclient.h"
#include "data.h"
#include "pipeio.h"
#include <memory>
#include <new>
#include <cerrno>
#include "../../../common/input_timing.h"


//////////////////////////////////////////////////////////////////////////
//
CIpcClient::CIpcClient(LPCWSTR szPipeName)
	: m_strPipeName(szPipeName)
{
}

void CIpcClient::Add(LPCWSTR szParam)
{
	m_vecParams.emplace_back(szParam);
}

void CIpcClient::Add(long nParam)
{
	CString strParam;
	strParam.Format(L"%ld", nParam);
	m_vecParams.push_back(strParam);
}


HRESULT CIpcClient::Call(LPCWSTR szMethod)
{
	coral::input_timing::Scope callTiming(L"legacy_ipc.call");
	m_ResultKv.clear();
	if (ipc_hlp::IsStrEmpty(szMethod)) return E_INVALIDARG;
	HRESULT hr = S_OK;
	CString strData = CDataParser::ClientMerge(szMethod, m_vecParams);

	CString strResult;
	hr = CIpcClient::_SendTo(m_strPipeName, strData, strResult, 3);
	if (FAILED(hr))
		return hr;

	if (ipc_hlp::IsStrEmpty(strResult))
		return E_FAIL;

	_LoadResult(strResult);
	CString strHR = Get(CDataParser::RESULT_CODE);
	if (strHR.IsEmpty())
		return E_FAIL;
	WCHAR* end = nullptr;
    errno = 0;
    const long result = wcstol(strHR, &end, 10);
    if (errno == ERANGE || end == strHR.GetString() || *end != 0) return E_FAIL;
    return static_cast<HRESULT>(result);
}

CString CIpcClient::Get(LPCWSTR szKey)
{
	if (ipc_hlp::IsStrEmpty(szKey))
		return L"";
	MapKv_Const_It it = m_ResultKv.find(szKey);
	return it == m_ResultKv.end() ? CString(L"") : it->second;
}

void CIpcClient::Reset()
{
	m_vecParams.clear();
	m_ResultKv.clear();
}

HRESULT CIpcClient::_SendTo(LPCWSTR name, LPCWSTR data, CString& reply, int retries)
{
    if (ipc_hlp::IsStrEmpty(name) || ipc_hlp::IsStrEmpty(data)) return E_INVALIDARG;
    const size_t chars = wcsnlen_s(data, static_cast<size_t>(coral_ipc::MaxMessageChars) + 1);
    if (chars == 0 || chars > coral_ipc::MaxMessageChars)
        return HRESULT_FROM_WIN32(ERROR_BAD_LENGTH);

    CPipeClient client;
    const auto connectStart = coral::input_timing::nowUs();
    HRESULT hr = client.ConnectServer(name, retries);
    coral::input_timing::mark(L"legacy_ipc.connect", connectStart, 0, static_cast<unsigned long>(hr));
    if (FAILED(hr)) return hr;

    const auto exchangeStart = coral::input_timing::nowUs();
    DWORD length = static_cast<DWORD>(chars), transferred = 0;
    hr = client.SendToServer(&length, sizeof(length), &transferred);
    if (FAILED(hr)) return hr;
    hr = client.SendToServer(data, length * sizeof(WCHAR), &transferred);
    if (FAILED(hr)) return hr;
    hr = client.RecvFromServer(&length, sizeof(length), &transferred);
    if (FAILED(hr)) return hr;
    if (!coral_ipc::ValidLength(length)) return HRESULT_FROM_WIN32(ERROR_BAD_LENGTH);

    std::unique_ptr<WCHAR[]> buffer(new (std::nothrow) WCHAR[static_cast<size_t>(length) + 1]);
    if (!buffer) return E_OUTOFMEMORY;
    hr = client.RecvFromServer(buffer.get(), length * sizeof(WCHAR), &transferred);
    if (FAILED(hr)) return hr;
    buffer[length] = 0;
    if (wmemchr(buffer.get(), 0, length)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    reply.SetString(buffer.get(), static_cast<int>(length));
    coral::input_timing::mark(L"legacy_ipc.exchange", exchangeStart, 0, static_cast<unsigned long>(hr));
    return S_OK;
}
//////////////////////////////////////////////////////////////////////////
//
void CIpcClient::_LoadResult(LPCWSTR szData)
{
	if (ipc_hlp::IsStrEmpty(szData))
		return;

	CDataParser::ClientSplit(szData, m_ResultKv);
}

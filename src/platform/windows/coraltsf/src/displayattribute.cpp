#include "pch.h"
#include "displayattribute.h"

// {B2E2C831-8209-4E6F-A43A-1E6B84E8D1C6}
const GUID GUID_CoralPinyin_DisplayAttr =
{ 0xb2e2c831, 0x8209, 0x4e6f, { 0xa4, 0x3a, 0x1e, 0x6b, 0x84, 0xe8, 0xd1, 0xc6 } };

// ---------------------------------------------------------------------------
// CDisplayAttributeInfo —— 拼音组串的点状下划线
// ---------------------------------------------------------------------------

STDMETHODIMP CDisplayAttributeInfo::GetGUID(GUID* pguid)
{
	if (!pguid) return E_INVALIDARG;
	*pguid = GUID_CoralPinyin_DisplayAttr;
	return S_OK;
}

STDMETHODIMP CDisplayAttributeInfo::GetDescription(BSTR* pbstrDesc)
{
	if (!pbstrDesc) return E_INVALIDARG;
	*pbstrDesc = ::SysAllocString(L"Coral Pinyin composition underline");
	return (*pbstrDesc) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CDisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda)
{
	if (!pda) return E_INVALIDARG;

	// 文字颜色 —— 继承应用程序（不覆盖）。
	pda->crText.type = TF_CT_NONE;

	// 背景 —— 不改变背景色。
	pda->crBk.type = TF_CT_NONE;

	// 线型：点状下划线，与微软/微信输入法一致。
	pda->lsStyle = TF_LS_DOT;
	pda->fBoldLine = FALSE;

	// 线条颜色 —— 继承应用程序（不覆盖）。
	pda->crLine.type = TF_CT_NONE;

	// 把该范围标记为“输入”（未上屏的预编辑文本）。
	pda->bAttr = TF_ATTR_INPUT;

	return S_OK;
}

STDMETHODIMP CDisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda)
{
	// 只读 —— 忽略调用方的值；保持点状下划线不变。
	(void)pda;
	return S_OK;
}

STDMETHODIMP CDisplayAttributeInfo::Reset()
{
	// 没有可重置的可变状态 —— 属性是常量。
	return S_OK;
}

// ---------------------------------------------------------------------------
// CEnumDisplayAttributeInfo —— 单项枚举器
// ---------------------------------------------------------------------------

HRESULT CEnumDisplayAttributeInfo::Init(ITfDisplayAttributeInfo* pInfo)
{
	if (!pInfo) return E_INVALIDARG;
	m_pInfo = pInfo;
	m_ulFetched = 0;
	return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum)
{
	if (!ppEnum) return E_INVALIDARG;
	*ppEnum = nullptr;

	CComObject<CEnumDisplayAttributeInfo>* pClone = nullptr;
	HRESULT hr = CComObject<CEnumDisplayAttributeInfo>::CreateInstance(&pClone);
	if (FAILED(hr)) return hr;
	pClone->AddRef();
	if (m_pInfo)
		pClone->Init(m_pInfo);
	pClone->m_ulFetched = m_ulFetched;
	*ppEnum = pClone;
	return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Next(
	ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched)
{
	if (pcFetched) *pcFetched = 0;
	if (!rgInfo) return E_INVALIDARG;

	ULONG nReturned = 0;
	if (m_ulFetched == 0 && ulCount > 0 && m_pInfo)
	{
		rgInfo[0] = m_pInfo;
		rgInfo[0]->AddRef();
		nReturned = 1;
		m_ulFetched = 1;
	}
	if (pcFetched) *pcFetched = nReturned;
	return (nReturned == ulCount) ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Reset()
{
	m_ulFetched = 0;
	return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Skip(ULONG ulCount)
{
	if (m_ulFetched == 0 && ulCount > 0)
	{
		m_ulFetched = 1;
		ulCount--;
	}
	return (ulCount == 0) ? S_OK : S_FALSE;
}

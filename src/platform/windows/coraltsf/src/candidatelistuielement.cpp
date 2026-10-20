#include "pch.h"
#include "candidatelistuielement.h"
#include "textservice.h"
#include "contextstore.h"

namespace
{
constexpr DWORD kAllCandidateUiFlags =
	TF_CLUIE_DOCUMENTMGR |
	TF_CLUIE_COUNT |
	TF_CLUIE_SELECTION |
	TF_CLUIE_STRING |
	TF_CLUIE_PAGEINDEX |
	TF_CLUIE_CURRENTPAGE;
}

CCandidateListUIElement::CCandidateListUIElement(CTextService* pTextService)
	: m_pTextService(pTextService)
	, m_dwUIElementID(TF_INVALID_COOKIE)
{

}

CCandidateListUIElement::~CCandidateListUIElement()
{
	unInit();
}

STDAPI CCandidateListUIElement::QueryInterface(REFIID riid, void** ppvObject)
{
	if (!ppvObject)
		return E_INVALIDARG;

	*ppvObject = nullptr; 

	if (IsEqualIID(riid, IID_IUnknown))
	{
		*ppvObject = static_cast<IUnknown*>(static_cast<ITfCandidateListUIElement*>(this)); // 可以指向任何一个基类接口
	}
	else if (IsEqualIID(riid, IID_ITfUIElement))
	{
		*ppvObject = static_cast<ITfUIElement*>(static_cast<ITfCandidateListUIElement*>(this));
	}
	else if (IsEqualIID(riid, IID_ITfCandidateListUIElement))
	{
		*ppvObject = static_cast<ITfCandidateListUIElement*>(this);
	}
	else if (IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior))
	{
		*ppvObject = static_cast<ITfCandidateListUIElementBehavior*>(this);
	}
	else if (IsEqualIID(riid, IID_ITfIntegratableCandidateListUIElement))
	{
		*ppvObject = static_cast<ITfIntegratableCandidateListUIElement*>(this);
	}
	else
	{
		return E_NOINTERFACE;
	}

	AddRef();
	return S_OK;
}

STDAPI_(ULONG) CCandidateListUIElement::AddRef()
{
	return InterlockedIncrement(&m_cRef);
}

STDAPI_(ULONG) CCandidateListUIElement::Release()
{
	ULONG cRef = InterlockedDecrement(&m_cRef);
	if (cRef == 0)
	{
		delete this; 
	}
	return cRef;
}

STDAPI CCandidateListUIElement::GetDescription(BSTR* pbstr)
{
	if (!pbstr)
		return E_INVALIDARG;

	*pbstr = SysAllocString(L"CORAL pinyin Candidate List");
	return S_OK;
}

STDAPI CCandidateListUIElement::GetGUID(GUID* pguid)
{
	if (!pguid)
		return E_INVALIDARG;

	*pguid = GUID_CANDIDATELIST_UI;
	return S_OK;
}

STDAPI CCandidateListUIElement::Show(BOOL bShow)
{
	const bool changed = !!m_bIsShown != !!bShow;
	m_bIsShown = bShow;
	if (m_lifecycle == Lifecycle::Begun)
	{
		notifyLegacyCandidate(!!bShow);
		if (changed && m_pTextService)
			m_pTextService->onCandidateUIElementVisibilityChanged(this, !!bShow);
	}
	return S_OK;
}

void CCandidateListUIElement::notifyLegacyCandidate(bool open)
{
	if (m_legacyCandidateOpen == open)
		return;
	m_legacyCandidateOpen = open;
	// 旧应用通过这个隔间观察候选的打开/关闭。
	if (m_pContext && m_pTextService)
	{
		CComQIPtr<ITfCompartmentMgr> manager(m_pContext);
		if (manager)
		{
			static const GUID kCuaCandidateMessage =
				{ 0xb7a578d2, 0x9332, 0x438a, { 0xa4, 0x03, 0x40, 0x57, 0xd0, 0x5c, 0x39, 0x58 } };
			CComPtr<ITfCompartment> compartment;
			if (SUCCEEDED(manager->GetCompartment(kCuaCandidateMessage, &compartment)))
			{
				CComVariant value(static_cast<LONG>(open ? TRUE : FALSE));
				compartment->SetValue(m_pTextService->getClientId(), &value);
			}
		}
	}
}

STDAPI CCandidateListUIElement::IsShown(BOOL* pbVisible)
{
	if (!pbVisible)
		return E_INVALIDARG;

	*pbVisible = m_bIsShown;
	return S_OK;
}

STDAPI CCandidateListUIElement::GetUpdatedFlags(DWORD* pdwFlags)
{
	if (!pdwFlags)
		return E_INVALIDARG;

	*pdwFlags = m_updatedFlags;
	m_updatedFlags = 0;
	return S_OK;
}

STDAPI CCandidateListUIElement::GetDocumentMgr(ITfDocumentMgr** ppdim)
{
	if (!ppdim)
		return E_INVALIDARG;

	*ppdim = nullptr;
	if (!m_pDocumentMgr)
		return E_FAIL;

	return m_pDocumentMgr.CopyTo(ppdim);
}

STDAPI CCandidateListUIElement::GetCount(UINT* pcCount)
{
	if (!pcCount)
		return E_INVALIDARG;

	*pcCount = static_cast<UINT>(m_candidates.size());
	return S_OK;
}

STDAPI CCandidateListUIElement::GetSelection(UINT* puIndex)
{
	if (!puIndex)
		return E_INVALIDARG;

	*puIndex = m_uSelectedCandidateIndex;
	return S_OK;
}

STDAPI CCandidateListUIElement::GetString(UINT uIndex, BSTR* pstr)
{
	if (!pstr)
		return E_INVALIDARG;

	if (m_candidates.size() == 0)
	{
		*pstr = SysAllocString(L"");
		return S_OK;
	}

	if (uIndex >= m_candidates.size())
		return E_INVALIDARG; // 索引超出范围

	*pstr = SysAllocString(m_candidates[uIndex].GetString());
	if (!*pstr)
		return E_OUTOFMEMORY; // 内存分配失败

	return S_OK;
}

STDAPI CCandidateListUIElement::GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt)
{
	if (!puPageCnt)
		return E_INVALIDARG;

	if (m_candidates.size() == 0)
	{
		*puPageCnt = 0; // 总页数为0
		if (pIndex && uSize > 0)
		{
			pIndex[0] = 0;
		}
		return S_OK;
	}

	// 计算总页数
	ULONG totalCandidates = static_cast<ULONG>(m_candidates.size());
	*puPageCnt = (totalCandidates + m_uPageSize - 1) / m_uPageSize; // 向上取整

	// 宿主可能先查询页数，再提供精确大小的缓冲。
	if (!pIndex)
		return S_OK;
	if (uSize < *puPageCnt)
		return E_NOT_SUFFICIENT_BUFFER;
	for (UINT i = 0; i < *puPageCnt; ++i)
		pIndex[i] = i * m_uPageSize;
	return S_OK;
}

STDAPI CCandidateListUIElement::SetPageIndex(UINT* pIndex, UINT uPageCnt)
{
	return E_NOTIMPL;
}

STDAPI CCandidateListUIElement::GetCurrentPage(UINT* puPage)
{
	if (!puPage)
		return E_INVALIDARG;

	*puPage = m_uCurrentPageIndex;
	return S_OK;
}

STDAPI CCandidateListUIElement::SetSelection(UINT index)
{
	if (m_lifecycle != Lifecycle::Begun || !m_pTextService ||
		!m_pContext || m_pTextService->getContext() != m_pContext.p)
		return E_FAIL;
	if (index >= m_candidates.size())
		return E_INVALIDARG;
	return m_pTextService->commitCandidateByIndex(static_cast<int>(index));
}

STDAPI CCandidateListUIElement::Finalize()
{
	if (m_lifecycle != Lifecycle::Begun || !m_pTextService ||
		!m_pContext || m_pTextService->getContext() != m_pContext.p)
		return E_FAIL;
	return m_pTextService->commitCurrentCandidate();
}

STDAPI CCandidateListUIElement::Abort()
{
	return Finalize();
}

STDAPI CCandidateListUIElement::SetIntegrationStyle(GUID)
{
	return S_OK;
}

STDAPI CCandidateListUIElement::GetSelectionStyle(
	TfIntegratableCandidateListSelectionStyle* style)
{
	if (!style) return E_INVALIDARG;
	*style = STYLE_ACTIVE_SELECTION;
	return S_OK;
}

STDAPI CCandidateListUIElement::OnKeyDown(WPARAM, LPARAM, BOOL* eaten)
{
	if (!eaten) return E_INVALIDARG;
	// 常规 TSF 按键接收器负责导航与选词。
	*eaten = FALSE;
	return S_OK;
}

STDAPI CCandidateListUIElement::ShowCandidateNumbers(BOOL* show)
{
	if (!show) return E_INVALIDARG;
	*show = TRUE;
	return S_OK;
}

STDAPI CCandidateListUIElement::FinalizeExactCompositionString()
{
	return Finalize();
}

void CCandidateListUIElement::syncFrom(const coral::tsf::CandidateSnapshot& snapshot)
{
	bool stringsChanged = m_candidates.size() != snapshot.candidates.size();
	if (!stringsChanged)
		for (size_t i = 0; i < m_candidates.size(); ++i)
			if (m_candidates[i] != snapshot.candidates[i]) { stringsChanged = true; break; }
	if (stringsChanged)
	{
		m_candidates = snapshot.candidates;
		markUpdated(TF_CLUIE_COUNT | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX);
	}
	const ULONG pageSize = (std::max)(1u, snapshot.pageSize);
	const UINT selection = m_candidates.empty() ? 0
		: (std::min)(snapshot.selection, static_cast<UINT>(m_candidates.size() - 1));
	const UINT page = selection / pageSize;
	if (pageSize != m_uPageSize)
		markUpdated(TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE);
	if (selection != m_uSelectedCandidateIndex)
		markUpdated(TF_CLUIE_SELECTION);
	if (page != m_uCurrentPageIndex)
		markUpdated(TF_CLUIE_CURRENTPAGE);
	m_uPageSize = pageSize;
	m_uSelectedCandidateIndex = selection;
	m_uCurrentPageIndex = page;
}

const std::vector<CString>& CCandidateListUIElement::getCandidates() const
{
	return m_candidates;
}

STDAPI CCandidateListUIElement::GetWnd(HWND* phwnd)
{
	if (!phwnd) return E_INVALIDARG;
	*phwnd = NULL; // 关键：在 UILess 模式下返回 NULL
	return S_OK;
}

BOOL CCandidateListUIElement::init()
{
	if (m_lifecycle != Lifecycle::Uninitialized)
		return TRUE;

	ITfThreadMgr* threadMgr = m_pTextService ? m_pTextService->getThreadMgr() : nullptr;
	if (!threadMgr)
		return FALSE;

	const HRESULT hr = threadMgr->QueryInterface(IID_ITfUIElementMgr, (void**)&m_pUIElementMgr);
	if (FAILED(hr))
	{
		LOG_ERROR_W(L"[CCandidateListUIElement::init] QueryInterface(ITfUIElementMgr) failed.");
		return FALSE;
	}

	m_dwUIElementID = TF_INVALID_COOKIE;
	m_lifecycle = Lifecycle::Idle;
	m_updatedFlags = kAllCandidateUiFlags;
	return TRUE;
}

void CCandidateListUIElement::unInit()
{
	endUI();
	m_pDocumentMgr.Release();
	m_pContext.Release();
	m_pUIElementMgr.Release();
	m_lifecycle = Lifecycle::Uninitialized;
	m_updatedFlags = 0;
}

BOOL CCandidateListUIElement::compareUIElementCookie(DWORD dwUIElementId)
{
	return m_lifecycle == Lifecycle::Begun && dwUIElementId == m_dwUIElementID;
}

DWORD CCandidateListUIElement::getCandidateListCookie()
{
	return m_dwUIElementID;
}

void CCandidateListUIElement::beginUI(BOOL& isAllowShow)
{
	isAllowShow = TRUE;
	if (!m_pUIElementMgr || m_lifecycle == Lifecycle::Uninitialized)
		return;

	if (m_lifecycle == Lifecycle::Begun)
	{
		isAllowShow = m_allowServerWindow ? TRUE : FALSE;
		updateUI();
		return;
	}

	m_pDocumentMgr.Release();
	ITfContext* context = m_pTextService ? m_pTextService->getContext() : nullptr;
	m_pContext = context;
	if (context)
		context->GetDocumentMgr(&m_pDocumentMgr);

	DWORD elementId = TF_INVALID_COOKIE;
	const HRESULT hr = m_pUIElementMgr->BeginUIElement(
		static_cast<ITfUIElement*>(this), &isAllowShow, &elementId);
	if (FAILED(hr) || elementId == TF_INVALID_COOKIE)
	{
		LOG_ERROR_W(L"[tsf_ui] event=begin_failed hr=" +
			std::to_wstring(static_cast<unsigned long>(hr)));
		m_pDocumentMgr.Release();
		m_pContext.Release();
		m_dwUIElementID = TF_INVALID_COOKIE;
		return;
	}

	m_dwUIElementID = elementId;
	m_lifecycle = Lifecycle::Begun;
	m_allowServerWindow = isAllowShow != FALSE;
	m_updatedFlags = kAllCandidateUiFlags;
	LOG_INFO_W(L"[tsf_ui] event=begin service_id=" +
		std::to_wstring(m_pTextService->getServiceInstanceId()) + L" ui_id=" +
		std::to_wstring(elementId) + L" ime_draws=" +
		std::to_wstring(isAllowShow != FALSE));
	// 自行渲染候选的宿主从 BeginUIElement 返回 FALSE。
	Show(isAllowShow);
	if (!isAllowShow)
		LOG_INFO_W(L"[CCandidateListUIElement::beginUI] CORAL IME is not rendering UI");
}

void CCandidateListUIElement::updateUI()
{
	if (m_pUIElementMgr && m_lifecycle == Lifecycle::Begun && m_dwUIElementID != TF_INVALID_COOKIE)
	{
		m_pUIElementMgr->UpdateUIElement(m_dwUIElementID);
	}
}

void CCandidateListUIElement::endUI()
{
	if (m_lifecycle == Lifecycle::Begun)
	{
		const DWORD elementId = m_dwUIElementID;
		m_dwUIElementID = TF_INVALID_COOKIE;
		m_lifecycle = Lifecycle::Idle;
		m_allowServerWindow = true;
		Show(FALSE);
		notifyLegacyCandidate(false);
		m_pDocumentMgr.Release();
		m_pContext.Release();
		LOG_INFO_W(L"[tsf_ui] event=end service_id=" +
			std::to_wstring(m_pTextService->getServiceInstanceId()) + L" ui_id=" +
			std::to_wstring(elementId));
		if (m_pUIElementMgr && elementId != TF_INVALID_COOKIE)
			m_pUIElementMgr->EndUIElement(elementId);
	}

	m_candidates.clear();
	m_uSelectedCandidateIndex = 0;
	m_uCurrentPageIndex = 0;
	m_updatedFlags = kAllCandidateUiFlags;
}

void CCandidateListUIElement::markUpdated(DWORD flags)
{
	m_updatedFlags |= flags;
}

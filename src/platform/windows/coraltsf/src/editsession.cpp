#include "pch.h"
#include "editsession.h"
#include "preference_registry.h"
#include "displayattribute.h"
#include <windows.h>
#include <shellscalingapi.h>

#define PLACEHOLDER L"\u200b"
#define PLACEHOLDER_SPECIAL L" "

namespace
{
std::atomic<std::uint64_t> g_nextEditOperationId{ 1 };

HRESULT ApplyCompositionDisplayAttribute(ITfContext* context, TfEditCookie ec, ITfRange* range,
    const std::function<bool()>& valid)
{
	if (!context || !range)
		return E_INVALIDARG;

	CComPtr<ITfCategoryMgr> categoryMgr;
	HRESULT hr = CoCreateInstance(
		CLSID_TF_CategoryMgr,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_ITfCategoryMgr,
		reinterpret_cast<void**>(&categoryMgr));
	if (FAILED(hr))
		return hr;
    if (!categoryMgr || !valid()) return E_ABORT;

	TfGuidAtom atom = TF_INVALID_GUIDATOM;
	hr = categoryMgr->RegisterGUID(GUID_CoralPinyin_DisplayAttr, &atom);
	if (FAILED(hr))
		return hr;
    if (!valid()) return E_ABORT;

	CComPtr<ITfProperty> property;
	hr = context->GetProperty(GUID_PROP_ATTRIBUTE, &property);
	if (FAILED(hr))
		return hr;
    if (!property || !valid()) return E_ABORT;

	VARIANT value;
	VariantInit(&value);
	value.vt = VT_I4;
	value.lVal = static_cast<LONG>(atom);
	hr = property->SetValue(ec, range, &value);
	VariantClear(&value);
	return hr;
}

HRESULT ClearCompositionDisplayAttribute(ITfContext* context, TfEditCookie ec, ITfRange* range,
    const std::function<bool()>& valid)
{
	if (!context || !range)
		return E_INVALIDARG;
	CComPtr<ITfProperty> property;
    const HRESULT status = context->GetProperty(GUID_PROP_ATTRIBUTE, &property);
    if (FAILED(status)) return status;
    if (!property || !valid()) return E_ABORT;
    return property->Clear(ec, range);
}

HRESULT RequestEditSessionChecked(
	ITfContext* context,
	TfClientId clientId,
	CEditSession* editSession,
	DWORD flags)
{
	if (!context || !editSession || clientId == TF_CLIENTID_NULL)
	{
        if (editSession) editSession->complete_request(E_INVALIDARG);
		return E_INVALIDARG;
    }

	HRESULT sessionHr = E_PENDING;
	const HRESULT requestHr = context->RequestEditSession(clientId, editSession, flags, &sessionHr);
	if (FAILED(requestHr))
	{
		LOG_ERROR_W(L"[tsf_edit] event=request_failed operation_id=" +
			std::to_wstring(editSession->operationId()) + L" type=" +
			std::wstring(editSession->operationName()) + L" hr=" +
			std::to_wstring(static_cast<unsigned long>(requestHr)));
        editSession->complete_request(requestHr);
		return requestHr;
	}
	if (requestHr == TF_S_ASYNC || sessionHr == TF_S_ASYNC)
		return TF_S_ASYNC;
    editSession->complete_request(sessionHr);
	return sessionHr;
}
}

CEditSession::CEditSession(
	CEditMgr* pEditMgr,
	const wchar_t* operationName,
	const CString& text,
	TfAnchor anchor,
	int cursorPos,
	CRect* textBoundOut)
	: m_cRef(1)
	, m_operationId(g_nextEditOperationId.fetch_add(1, std::memory_order_relaxed))
	, m_operationName(operationName)
	, m_editMgrGuard(pEditMgr ? pEditMgr->shared_from_this() : nullptr)
	, m_pEditMgr(pEditMgr)
	, m_text(text)
	, m_anchor(anchor)
	, m_cursorPos(cursorPos)
	, m_textBoundOut(textBoundOut) {
	if (m_pEditMgr)
	{
		m_lifetimeGuard = m_pEditMgr->getCompositionSink();
		m_context = m_pEditMgr->getContext();
		m_composition = m_pEditMgr->getComposition();
        edit_epoch_ = m_pEditMgr->edit_epoch();
        focus_epoch_ = m_pEditMgr->focus_epoch();
	}
}

CEditSession::~CEditSession() {
    complete_request(E_ABORT);
}

STDAPI CEditSession::DoEditSession(TfEditCookie ec)
{
    if (completed_) return result_.status;
    const bool cleanup = wcscmp(m_operationName, L"end") == 0 || unfocused_cleanup_;
	const HRESULT result = current(!cleanup) && (!preflight_ || preflight_()) && current(!cleanup)
        ? execute(ec) : E_ABORT;
	if (FAILED(result))
	{
		LOG_ERROR_W(L"[tsf_edit] event=execute_failed operation_id=" +
			std::to_wstring(m_operationId) + L" type=" +
			std::wstring(m_operationName) + L" hr=" +
			std::to_wstring(static_cast<unsigned long>(result)));
	}
    complete_request(result);
	return result;
}

void CEditSession::set_completion(EditCompletion callback, std::function<bool()> preflight)
{
    completion_ = std::move(callback);
    preflight_ = std::move(preflight);
}

void CEditSession::complete_request(HRESULT status)
{
    if (completed_) return;
    completed_ = true;
    result_.status = status;
    if (wcscmp(m_operationName, L"commit") == 0) {
        LOG_INFO_W(L"[tsf_edit] event=commit_completed operation_id=" + std::to_wstring(m_operationId) +
            L" hr=" + std::to_wstring(static_cast<unsigned long>(status)) +
            L" text_written=" + std::to_wstring(result_.text_written) +
            L" composition_ended=" + std::to_wstring(result_.composition_ended));
    }
    auto completion = std::move(completion_);
    if (completion) completion(result_);
}

bool CEditSession::current(bool require_focus) const
{
    return m_pEditMgr && m_pEditMgr->edit_current(edit_epoch_, focus_epoch_, require_focus) &&
        m_context == m_pEditMgr->getContext() &&
        (!m_composition || m_composition == m_pEditMgr->getComposition());
}

CStartCompositionEditSession::CStartCompositionEditSession(CEditMgr* manager)
	: CEditSession(manager, L"start") {}
HRESULT CStartCompositionEditSession::execute(TfEditCookie ec) { return doStart(ec); }

CEndCompositionEditSession::CEndCompositionEditSession(CEditMgr* manager)
	: CEditSession(manager, L"end") {}
HRESULT CEndCompositionEditSession::execute(TfEditCookie ec) { return doEnd(ec); }

CTextBoundEditSession::CTextBoundEditSession(CEditMgr* manager, CRect* output)
	: CEditSession(manager, L"text_bound", CString(), TF_ANCHOR_START, 0, output) {}
HRESULT CTextBoundEditSession::execute(TfEditCookie ec) { return doTextBound(ec); }

CInsertTextEditSession::CInsertTextEditSession(
	CEditMgr* manager, const CString& text, TfAnchor anchor)
	: CEditSession(manager, L"insert_text", text, anchor) {}
HRESULT CInsertTextEditSession::execute(TfEditCookie ec) { return doInput(ec); }

CUpdateCompositionEditSession::CUpdateCompositionEditSession(
	CEditMgr* manager, const CString& text)
	: CEditSession(manager, L"update_composition", text) {}
HRESULT CUpdateCompositionEditSession::execute(TfEditCookie ec) { return doComposition(ec); }

CSetCursorEditSession::CSetCursorEditSession(CEditMgr* manager, int position)
	: CEditSession(manager, L"set_cursor", CString(), TF_ANCHOR_START, position) {}
HRESULT CSetCursorEditSession::execute(TfEditCookie ec) { return doSetCursorPos(ec); }

CUpdatePlaceholderEditSession::CUpdatePlaceholderEditSession(
	CEditMgr* manager, const CString& text, TfAnchor anchor)
	: CEditSession(manager, L"update_placeholder", text, anchor) {}
HRESULT CUpdatePlaceholderEditSession::execute(TfEditCookie ec) { return doUpdatePlaceholder(ec); }

CCommitCompositionEditSession::CCommitCompositionEditSession(
	CEditMgr* manager, const CString& text)
	: CEditSession(manager, L"commit", text) {}
HRESULT CCommitCompositionEditSession::execute(TfEditCookie ec) { return doCommit(ec); }

CCancelCompositionEditSession::CCancelCompositionEditSession(CEditMgr* manager)
	: CEditSession(manager, L"cancel") {}
HRESULT CCancelCompositionEditSession::execute(TfEditCookie ec) { return doCancel(ec); }


HRESULT CEditSession::doStart(TfEditCookie ec)
{
	if (!m_pEditMgr) {
		return E_FAIL;
	}
	if (m_pEditMgr->exsitComposition()) {
		return S_OK;
	}
	ITfContext* pContext = m_context;
	HRESULT hr = E_FAIL;
	if (pContext && m_pEditMgr->getContext() == pContext)
	{
		CComPtr<ITfInsertAtSelection> pInsertAtSelection;
		hr = pContext->QueryInterface(&pInsertAtSelection);
		if (hr != S_OK)
			return hr;
		CComPtr<ITfRange> pRange;
		hr = pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, NULL, 0, &pRange);
		if (hr != S_OK)
			return hr;
		CComPtr<ITfContextComposition> pContextComposition;
		hr = pContext->QueryInterface(&pContextComposition);
		if (hr != S_OK)
			return hr;
		ITfCompositionSink* pCompositionSink = m_pEditMgr->getCompositionSink();
		CComPtr<ITfComposition> pComposition = nullptr;
        if (!pRange || !pContextComposition || !current(true)) return E_ABORT;
		hr = pContextComposition->StartComposition(ec, pRange, pCompositionSink, &pComposition);
		if (hr != S_OK || !pComposition) {
			return FAILED(hr) ? hr : E_FAIL;
		}
        if (!current(true)) {
            pComposition->EndComposition(ec);
            return E_ABORT;
        }
		m_pEditMgr->setComposition(pComposition);

		// Start 由按键接收器异步请求。等到本编辑会话运行时，
		// 按键处理可能已经解析了更多字母；在此写入最新的预编辑文本，
		// 使第一次组串永不为空。
		const CString text = m_pEditMgr->getAccumulatedPinyin();
		if (!text.IsEmpty())
		{
			hr = pRange->SetText(ec, TF_ST_CORRECTION, text, text.GetLength());
			if (FAILED(hr))
				return hr;

            if (!current(true)) return E_ABORT;
			hr = ApplyCompositionDisplayAttribute(pContext, ec, pRange, [this] { return current(true); });
			if (FAILED(hr))
				return hr;

			// 在某些宿主中 SetText 可能把原始范围留在旧锚点。
			// 移动宿主插入符之前重新获取组串范围。
			pRange.Release();
            if (!current(true)) return E_ABORT;
			hr = pComposition->GetRange(&pRange);
			if (FAILED(hr))
				return hr;
            if (!pRange || !current(true)) return E_ABORT;
			hr = pRange->Collapse(ec, TF_ANCHOR_END);
			if (FAILED(hr))
				return hr;
			TF_SELECTION selection = { 0 };
			selection.range = pRange;
			selection.style.ase = TF_AE_NONE;
			selection.style.fInterimChar = FALSE;
            if (!current(true)) return E_ABORT;
			hr = pContext->SetSelection(ec, 1, &selection);
			if (FAILED(hr))
				return hr;
		}
	}
	return hr;
}

HRESULT CEditSession::doEnd(TfEditCookie ec)
{
	ITfComposition* pComposition = m_composition;
	if (pComposition)
	{
        m_pEditMgr->internal_end_ = true;
		const HRESULT hr = pComposition->EndComposition(ec);
        m_pEditMgr->internal_end_ = false;
		if (hr != S_OK)
			return FAILED(hr) ? hr : E_FAIL;
		m_pEditMgr->clearTerminatedComposition(pComposition);
        result_.composition_ended = true;
		return S_OK;
	}
	return S_FALSE;
}

HRESULT CEditSession::doTextBound(TfEditCookie ec)
{
	ITfContext* pContext = m_context;
	ITfComposition* pComposition = m_composition;
	if (!pContext || !pComposition) {
		return S_FALSE;
	}
	TF_SELECTION selection = {};
	ULONG nSelection = 0;
	HRESULT hr = pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &nSelection);
	CComPtr<ITfRange> selection_range;
    selection_range.Attach(selection.range);
    if (FAILED(hr)) return hr;
    if (nSelection != 1 || !selection_range) return E_FAIL;
	CComPtr<ITfContextView> pContextView;
	hr = pContext->GetActiveView(&pContextView);
	if (!SUCCEEDED(hr))
		return hr;
    if (!pContextView || !current(true)) return E_ABORT;
	BOOL fClipped = FALSE;
	CRect rc(0, 0, 0, 0);
	hr = pContextView->GetTextExt(ec, selection.range, &rc, &fClipped);
	//做一个fallback判断,如果tsf获取屏幕坐标失败(不在窗口内)，则从系统获取
	HWND hwndView = NULL;
	pContextView->GetWnd(&hwndView);
	if (hwndView) {
		CRect wrc;
		if (::GetWindowRect(hwndView, &wrc) &&
			(FAILED(hr) || !wrc.PtInRect(rc.TopLeft()) || !wrc.PtInRect(rc.BottomRight())))
		{
			// 从系统插入符号获取屏幕坐标
			DWORD tid = ::GetWindowThreadProcessId(hwndView, NULL);
			GUITHREADINFO gi = { sizeof(GUITHREADINFO) };
			if (::GetGUIThreadInfo(tid, &gi) && ::IsWindow(gi.hwndCaret)) {
				CRect rcCaret = gi.rcCaret;
				::MapWindowPoints(gi.hwndCaret, NULL, (LPPOINT)&rcCaret, 2);
				rc = rcCaret;
                hr = S_OK;
			}
		}
	}
	if (!SUCCEEDED(hr))
		return hr;
	if (!m_textBoundOut)
		return E_POINTER;
    if (!current(true)) return E_ABORT;
	*m_textBoundOut = rc;
	return S_OK;
}

HRESULT CEditSession::doInput(TfEditCookie ec)
{
    if (m_pEditMgr->has_committed_text() || m_pEditMgr->commit_pending()) return E_ABORT;
	ITfContext* pContext = m_context;
	ITfComposition* pComposition = m_composition;
	if (!pContext || !pComposition) {
		return E_FAIL;
	}
	CComPtr<ITfRange> pRange;
	HRESULT hr = pComposition->GetRange(&pRange);
	if (hr != S_OK)
	{
		return E_FAIL;
	}
    if (!pRange || !current(true)) return E_ABORT;
	hr = pRange->SetText(ec, TF_ST_CORRECTION, m_text, m_text.GetLength());
	if (FAILED(hr)) return hr;
    if (!current(true)) return E_ABORT;
	hr = pRange->Collapse(ec, m_anchor);
	if (FAILED(hr)) return hr;
	TF_SELECTION tfSelection = { 0 };
	tfSelection.range = pRange;
	tfSelection.style.ase = TF_AE_NONE;
	tfSelection.style.fInterimChar = FALSE;
    if (!current(true)) return E_ABORT;
	return pContext->SetSelection(ec, 1, &tfSelection);
}

HRESULT CEditSession::doComposition(TfEditCookie ec)
{
    if (m_pEditMgr->has_committed_text() || m_pEditMgr->commit_pending()) return E_ABORT;
	ITfContext* pContext = m_context;
	ITfComposition* pComposition = m_composition;
	if (!pContext || !pComposition)
		return S_FALSE;

	CComPtr<ITfRange> pRange;
	HRESULT hr = pComposition->GetRange(&pRange);
	if (hr != S_OK) return hr;
    if (!pRange || !current(true)) return E_ABORT;
	hr = pRange->SetText(ec, TF_ST_CORRECTION, m_text, m_text.GetLength());
	if (hr != S_OK) return hr;
    if (!current(true)) return E_ABORT;
	hr = ApplyCompositionDisplayAttribute(pContext, ec, pRange, [this] { return current(true); });
	if (FAILED(hr)) return hr;

	// 替换后重新获取范围，使选区落在当前预编辑文本之后，
	// 包括保留静态替换范围的编辑器。
    if (!current(true)) return E_ABORT;
    pRange.Release();
	hr = pComposition->GetRange(&pRange);
	if (FAILED(hr)) return hr;
    if (!pRange || !current(true)) return E_ABORT;
	hr = pRange->Collapse(ec, TF_ANCHOR_END);
	if (hr != S_OK) return hr;

	TF_SELECTION tfSelection = { 0 };
	tfSelection.range = pRange;
	tfSelection.style.ase = TF_AE_NONE;
	tfSelection.style.fInterimChar = FALSE;

    if (!current(true)) return E_ABORT;
	hr = pContext->SetSelection(ec, 1, &tfSelection);
	if (hr != S_OK) return hr;

	return S_OK;
}

HRESULT CEditSession::doCommit(TfEditCookie ec)
{
	ITfContext* pContext = m_context;
    CComPtr<ITfComposition> pComposition = m_pEditMgr->getComposition();
	if (!pContext || !pComposition)
		return E_FAIL;
    if (m_pEditMgr->has_committed_text()) return E_ABORT;

	CComPtr<ITfRange> pRange;
	HRESULT hr = pComposition->GetRange(&pRange);
	if (FAILED(hr))
		return hr;
    if (!pRange || !current(true)) return E_ABORT;
    hr = pRange->SetText(ec, TF_ST_CORRECTION, m_text, m_text.GetLength());
    if (hr != S_OK) return FAILED(hr) ? hr : E_FAIL;
    // 写入是不可重放的边界；后续定位或结束失败仍须确认文字已经写入。
    result_.text_written = true;
    if (m_pEditMgr->getComposition() == pComposition) m_pEditMgr->committed_text_ = true;
    m_pEditMgr->record_written_text(m_text);
    if (!current(true)) return E_ABORT;
    ClearCompositionDisplayAttribute(pContext, ec, pRange, [this] { return current(true); });
    if (!current(true)) return E_ABORT;
    pRange.Release();
    hr = pComposition->GetRange(&pRange);
    if (SUCCEEDED(hr) && !pRange) hr = E_FAIL;
    if (SUCCEEDED(hr) && current(true)) hr = pRange->Collapse(ec, TF_ANCHOR_END);
	TF_SELECTION selection = { 0 };
	selection.range = pRange;
	selection.style.ase = TF_AE_NONE;
	selection.style.fInterimChar = FALSE;
    if (SUCCEEDED(hr) && current(true)) hr = pContext->SetSelection(ec, 1, &selection);
    if (!current(false)) return E_ABORT;
    m_pEditMgr->internal_end_ = true;
    HRESULT end_status = pComposition->EndComposition(ec);
    m_pEditMgr->internal_end_ = false;
	if (end_status == S_OK)
    {
		m_pEditMgr->clearTerminatedComposition(pComposition);
        result_.composition_ended = true;
    }
    else if (SUCCEEDED(end_status)) end_status = E_FAIL;
	return FAILED(hr) ? hr : end_status;
}

HRESULT CEditSession::doCancel(TfEditCookie ec)
{
    // 已写入的文字只能结束组串，不能再作为预编辑清空。
    if (m_pEditMgr->has_committed_text()) return doEnd(ec);
	CComPtr<ITfComposition> pComposition = m_composition;
	if (!pComposition)
		return S_FALSE;

	CComPtr<ITfRange> pRange;
	HRESULT hr = pComposition->GetRange(&pRange);
	if (FAILED(hr))
		return hr;
	if (!pRange || !current(!unfocused_cleanup_)) return E_ABORT;
	if (FAILED(hr = pRange->SetText(ec, TF_ST_CORRECTION, L"", 0)))
		return hr;
    if (m_pEditMgr->getComposition() == pComposition) m_pEditMgr->committed_text_ = true;
    if (!current(false)) return E_ABORT;
    m_pEditMgr->internal_end_ = true;
	hr = pComposition->EndComposition(ec);
    m_pEditMgr->internal_end_ = false;
	if (hr == S_OK)
    {
		m_pEditMgr->clearTerminatedComposition(pComposition);
        result_.composition_ended = true;
    }
    else if (SUCCEEDED(hr)) hr = E_FAIL;
	return hr;
}

HRESULT CEditSession::doUpdatePlaceholder(TfEditCookie ec)
{
    if (m_pEditMgr->has_committed_text() || m_pEditMgr->commit_pending()) return E_ABORT;
	ITfContext* pContext = m_context;
	ITfComposition* pComposition = m_composition;
	if (!pContext || !pComposition)
		return S_FALSE;

	CComPtr<ITfRange> pRange;
	HRESULT hr = pComposition->GetRange(&pRange);
	if (hr != S_OK)
		return hr;

	if (m_text.IsEmpty()) {
		return S_OK;
	}
    if (!pRange || !current(true)) return E_ABORT;
	hr = pRange->SetText(ec, TF_ST_CORRECTION, m_text, m_text.GetLength());
	if (hr != S_OK)
		return hr;

	// 只有PINYINHOLDER模式才把光标放到后面
    if (!current(true)) return E_ABORT;
	hr = pRange->Collapse(ec, m_anchor);
	if (hr != S_OK)
		return hr;

	TF_SELECTION tfSelection = { 0 };
	tfSelection.range = pRange;
	tfSelection.style.ase = TF_AE_NONE;
	tfSelection.style.fInterimChar = FALSE;
    if (!current(true)) return E_ABORT;
	hr = pContext->SetSelection(ec, 1, &tfSelection);
	if (hr != S_OK)
		return hr;

	return S_OK;
}

HRESULT CEditSession::doSetCursorPos(TfEditCookie ec)
{
    if (m_cursorPos < 0 || m_pEditMgr->has_committed_text()) return E_INVALIDARG;
	ITfContext* pContext = m_context;
	ITfComposition* pComposition = m_composition;
	if (!pContext || !pComposition)
		return S_FALSE;

	CComPtr<ITfRange> pRange;
	CComPtr<ITfRange> composition_range;
    HRESULT hr = pComposition->GetRange(&composition_range);
	if (hr != S_OK) return hr;
    if (!composition_range) return E_FAIL;
    hr = composition_range->Clone(&pRange);
    if (FAILED(hr)) return hr;
    if (!pRange || !current(true)) return E_ABORT;

	LONG cchActualMoved = 0; // 用于接收实际移动的字符数
    const TF_HALTCOND halt = { composition_range, TF_ANCHOR_END, 0 };
	hr = pRange->ShiftStart(ec, m_cursorPos, &cchActualMoved, &halt);
	if (hr != S_OK) return hr;
    if (cchActualMoved != m_cursorPos || !current(true)) return E_FAIL;

	hr = pRange->Collapse(ec, TF_ANCHOR_START);
	if (hr != S_OK) return hr;
    if (!current(true)) return E_ABORT;

	TF_SELECTION tfSelection = { 0 };
	tfSelection.range = pRange;
	tfSelection.style.ase = TF_AE_NONE;
	tfSelection.style.fInterimChar = FALSE;

	hr = pContext->SetSelection(ec, 1, &tfSelection);
	if (hr != S_OK) return hr;

	return S_OK;
}

CEditMgr::CEditMgr(CTextService* pTextService) :
	m_pTextService(pTextService) {

}

CEditMgr::~CEditMgr() {

}

void CEditMgr::setAboutToShow(bool bAboutToShow)
{
	m_bAboutToShow = bAboutToShow;
}

bool CEditMgr::getAboutToShow() const
{
	return m_bAboutToShow;
}

void CEditMgr::setPositionEnhanced(bool isPositionEnhanced)
{
	m_bPositionEnhanced = isPositionEnhanced;
}

bool CEditMgr::getPositionEnhanced() const
{
	return m_bPositionEnhanced;
}

int CEditMgr::getScheme() const
{
	return m_scheme;
}

void CEditMgr::setKeepPlaceholder(bool bKeepPlaceholder)
{
	m_bKeepPlaceholder = bKeepPlaceholder;
}

bool CEditMgr::getKeepPlaceholder() const
{
	return m_bKeepPlaceholder;
}

void CEditMgr::clearAccumulatedPinyin()
{
	m_accumulatedPinyin.Empty();
}

void CEditMgr::updateAccumulatedPinyin(const CString& unfinshedPinyin, TfClientId clientId)
{
	m_accumulatedPinyin = unfinshedPinyin;
	m_accumulatedPinyin.MakeLower();
	if (exsitComposition())
		updateComposition(clientId, m_accumulatedPinyin);
}

CString& CEditMgr::getAllInput()
{
	return m_allInput;
}

const CString& CEditMgr::getLastTwoChars() const
{
	return m_lastTwoChars;
}

void CEditMgr::setLastTwoChars(const CString& value)
{
	m_lastTwoChars = value;
}

std::vector<CString>& CEditMgr::getCandInput()
{
	return m_candInput;
}

int CEditMgr::isNeedFilter(const CString& text) const
{
	bool needFilter = false;
	if (text.GetLength() == 1)
	{
		WCHAR ch = text[0];

		// 检查是否为半角数字 (0-9)
		bool isHalfNum = (ch >= L'0' && ch <= L'9');
		// 检查是否为半角字母 (a-z, A-Z)
		bool isHalfLetter = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
		// 检查是否为全角数字 (０-９)
		bool isFullNum = (ch >= L'０' && ch <= L'９');
		// 检查是否为全角字母 (ａ-ｚ, Ａ-Ｚ)
		bool isFullLetter = (ch >= L'ａ' && ch <= L'ｚ') || (ch >= L'Ａ' && ch <= L'Ｚ');

		needFilter = isHalfNum || isHalfLetter || isFullNum || isFullLetter;
		if (!isHalfNum
			&& !isHalfLetter
			&& !isFullNum
			&& !isFullLetter)
		{
			return NO_LETTER_NUM;
		}
	}
	return needFilter ? NEED_FILTER : VAILD_STR;
}

void CEditMgr::setProcessName(const CString& process_name, int placeholder_scheme)
{
	m_processName = process_name;
    m_scheme = placeholder_scheme;
}

void CEditMgr::processbAboutToShow(WPARAM wParam)
{
	if (wParam == VK_NUMPAD0 || (wParam >= 'A' && wParam <= 'Z'))
		setAboutToShow(true);
	else
		setAboutToShow(false);
}

HRESULT CEditMgr::start(TfClientId clientId, ITfContext* pContext, ITfCompositionSink* pCompositionSink)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
    if (!pContext || retired_) return E_INVALIDARG;
    if (committed_text_) return end(clientId);
    if (start_pending_) return TF_S_ASYNC;
	if (exsitComposition())
	{
		return S_OK;
	}
	m_pContext = pContext;
	m_pCompositionSink = pCompositionSink;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CStartCompositionEditSession(this));
    start_pending_ = true;
    const auto epoch = edit_epoch_;
    pEditSession->set_completion([manager = shared_from_this(), epoch](const EditResult&) {
        if (manager->edit_epoch_ == epoch) manager->start_pending_ = false;
    });
	return RequestEditSessionChecked(
		pContext, clientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

HRESULT CEditMgr::end(TfClientId client_id, EditCompletion completion)
{
	ASSERT(client_id != TF_CLIENTID_NULL);
    if (end_pending_) return TF_S_ASYNC;
	setAboutToShow(false);
	setPositionEnhanced(false);
	if (!m_pContext)
    {
        if (completion) completion(EditResult{ E_FAIL, false, false });
        return E_FAIL;
    }
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CEndCompositionEditSession(this));
    end_pending_ = true;
    const auto sequence = request_sequence_;
    pEditSession->set_completion([manager = shared_from_this(), sequence, completion = std::move(completion)]
        (const EditResult& result) {
        if (sequence == manager->request_sequence_) manager->end_pending_ = false;
        if (completion) completion(result);
    });
	return RequestEditSessionChecked(
		m_pContext, client_id, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

HRESULT CEditMgr::commit(TfClientId clientId, const CString& text)
{
    return insertText(clientId, text, {}, {});
}

HRESULT CEditMgr::cancel(TfClientId clientId)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
    if (start_pending_ && !exsitComposition()) {
        invalidate_edits();
        clearAccumulatedPinyin();
        return S_OK;
    }
    invalidate_edits();
	if (!m_pContext || !exsitComposition())
		return S_FALSE;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CCancelCompositionEditSession(this));
	return RequestEditSessionChecked(
		m_pContext, clientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

HRESULT CEditMgr::updateComposition(TfClientId clientId, const CString& text)
{
	return doUpdateComposition(clientId, text);
}

HRESULT CEditMgr::setCursorPos(TfClientId clientId, int pos)
{
	return doSetCursorPos(clientId, pos);
}

HRESULT CEditMgr::insertText(TfClientId clientId, const CString& text)
{
    return insertText(clientId, text, {}, {});
}

HRESULT CEditMgr::insertText(TfClientId client_id, const CString& text,
    EditCompletion completion, std::function<bool()> preflight)
{
    if (retired_ || commit_pending_ || committed_text_ || !m_pContext ||
        (!exsitComposition() && !start_pending_))
    {
        if (completion) completion(EditResult{ E_ABORT, false, false });
        return E_ABORT;
    }
    commit_pending_ = true;
    CComPtr<CEditSession> session;
    session.Attach(new CCommitCompositionEditSession(this, text));
    const auto sequence = request_sequence_;
    session->set_completion([manager = shared_from_this(), sequence, completion = std::move(completion)]
        (const EditResult& result) {
        if (sequence == manager->request_sequence_) manager->commit_pending_ = false;
        if (completion) completion(result);
    }, std::move(preflight));
    return RequestEditSessionChecked(m_pContext, client_id, session,
        TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

void CEditMgr::record_written_text(const CString& text)
{
	// 不需要过滤的情况才添加到m_allInput
	int res = isNeedFilter(text);
	if (res == VAILD_STR)
	{
		m_allInput += text;
	}
	else if (res == NEED_FILTER)
	{
		//将不需要删除的改为一个占位符
		m_allInput += L"\u200B";
	}
	
	if (text.GetLength() > 0)
	{
		CString& lastTwoChars = m_lastTwoChars;
		CString combined = lastTwoChars + text;
		// 只保留最后2个字符
		if (combined.GetLength() >= 2)
		{
			lastTwoChars = combined.Right(2);
		}
		else
		{
			lastTwoChars = combined;
		}
	}
	
}

void CEditMgr::set_edit_focus(bool focused)
{
    if (focused_ == focused) return;
    focused_ = focused;
    ++focus_epoch_;
    if (!focused) invalidate_edits();
}

void CEditMgr::invalidate_edits()
{
    ++edit_epoch_;
    ++request_sequence_;
    start_pending_ = false;
    commit_pending_ = false;
    end_pending_ = false;
}

void CEditMgr::retire_edits()
{
    retired_ = true;
    invalidate_edits();
    ++focus_epoch_;
    start_pending_ = false;
    focus_validator_ = {};
}

bool CEditMgr::edit_current(std::uint64_t epoch, std::uint64_t focus, bool require_focus) const
{
    if (owner_thread_ != ::GetCurrentThreadId() || retired_ || epoch != edit_epoch_) return false;
    if (!require_focus) return true;
    if (!focused_ || focus != focus_epoch_) return false;
    if (focus_validator_ && !focus_validator_(m_pContext)) return false;
    return !retired_ && epoch == edit_epoch_ && focused_ && focus == focus_epoch_;
}

HRESULT CEditMgr::insertPlaceholder(TfClientId clientId)
{
	return doInsertText(clientId, PLACEHOLDER, TF_ANCHOR_START);
}

HRESULT CEditMgr::updatePlaceholder(TfClientId clientId)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
	if (!m_pContext)
		return E_FAIL;
	int scheme = getScheme();
	if (scheme != PINYINHOLDER) {
		return S_OK; // 不是PINYINHOLDER模式，直接返回
	}
	const WCHAR* placeholder = getCurrentPlaceholder();
	if (!placeholder)
		return S_OK;
	const CString placeholderText(placeholder);
	const TfAnchor anchor = scheme == PINYINHOLDER ? TF_ANCHOR_END : TF_ANCHOR_START;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CUpdatePlaceholderEditSession(this, placeholderText, anchor));
	return RequestEditSessionChecked(
		m_pContext, clientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

const WCHAR* CEditMgr::getCurrentPlaceholder() const
{
	int scheme = getScheme();
	switch (scheme)
	{
	case U200BHOLDER:
		return PLACEHOLDER;
	case PINYINHOLDER:
		return static_cast<const WCHAR*>(m_accumulatedPinyin.GetString());
	case SPACEHOLDER:
		return PLACEHOLDER_SPECIAL;
	case NOHOLDER:
		return nullptr;
	default:
		//todo 默认方案，之后再看用什么最好
		return PLACEHOLDER;
	}
}

void CEditMgr::delPlaceholder(TfEditCookie ec)
{
	if (!m_pComposition)
		return;
	CComPtr<ITfRange> pRange;
	if (m_pComposition->GetRange(&pRange) == S_OK)
	{
		WCHAR buf[MAX_PATH + 1] = { 0 };
		ULONG pcch = 0;
		if (S_OK == pRange->GetText(ec, TF_TF_MOVESTART, buf, MAX_PATH, &pcch))
		{
			LONG movePcch = 0;
			pRange->ShiftStart(ec, 0 - pcch, &movePcch, NULL);
			const WCHAR* currentPlaceholder = getCurrentPlaceholder();
			if (currentPlaceholder != nullptr && _wcsicmp(buf, currentPlaceholder) == 0)
			{
				// 如果是PINYINHOLDER模式，需要额外检查
				if (getScheme() == PINYINHOLDER)
				{
					if (!m_bKeepPlaceholder)
					{
						pRange->SetText(ec, 0, L"", 0);
					}
				}
				else
				{
					// 其他模式正常清空占位符
					pRange->SetText(ec, 0, L"", 0);
				}
			}
		}
	}
}

HRESULT CEditMgr::getTextBound(TfClientId clientId, CRect& rc)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
	if (!m_pContext)
		return E_FAIL;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CTextBoundEditSession(this, &rc));
	return RequestEditSessionChecked(m_pContext, clientId, pEditSession, TF_ES_SYNC | TF_ES_READ);
}

void CEditMgr::dpiAdapt(CRect& rc)
{
	HWND hwnd = GetActiveWindow();
	if (hwnd)
	{
		UINT baseDpi = 96;
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (user32)
		{
			using GetDpiForSystemFn = UINT(WINAPI*)();
			auto getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(
				GetProcAddress(user32, "GetDpiForSystem"));
			if (getDpiForSystem)
				baseDpi = getDpiForSystem();
		}
		const UINT dpi = g_fGetDpiForWindow
			? std::max<UINT>(96, g_fGetDpiForWindow(hwnd)) : baseDpi;
		const float scale = static_cast<float>(baseDpi) / dpi;
		rc.left = LONG(rc.left * scale);
		rc.top = LONG(rc.top * scale);
		rc.right = LONG(rc.right * scale);
		rc.bottom = LONG(rc.bottom * scale);
	}
}

HRESULT CEditMgr::doUpdateComposition(TfClientId clientId, const CString& text)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
	if (!m_pContext)
		return E_FAIL;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CUpdateCompositionEditSession(this, text));
	return RequestEditSessionChecked(
		m_pContext, clientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

HRESULT CEditMgr::doSetCursorPos(TfClientId clientId, int pos)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
	if (!m_pContext)
		return E_FAIL;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CSetCursorEditSession(this, pos));
	return RequestEditSessionChecked(
		m_pContext, clientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

HRESULT CEditMgr::doInsertText(TfClientId clientId, const CString& text, TfAnchor tfType)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
	if (!m_pContext)
		return E_FAIL;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CInsertTextEditSession(this, text, tfType));
	return RequestEditSessionChecked(
		m_pContext, clientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

BOOL CEditMgr::exsitComposition() const
{
	return m_pComposition != NULL;
}

ITfComposition* CEditMgr::getComposition() const
{
	return m_pComposition;
}

ITfContext* CEditMgr::getContext() const
{
	return m_pContext;
}

void CEditMgr::setContext(CComPtr<ITfContext> pContext)
{
    if (m_pContext != pContext) invalidate_edits();
	m_pContext = std::move(pContext);
}

void CEditMgr::setComposition(CComPtr<ITfComposition> pComposition) {
    if (m_pComposition && m_pComposition != pComposition) invalidate_edits();
	m_pComposition = std::move(pComposition);
}

bool CEditMgr::clearTerminatedComposition(ITfComposition* pComposition)
{
	if (!pComposition || m_pComposition != pComposition)
		return false;
    invalidate_edits();
    committed_text_ = false;
	m_pComposition.Release();
	m_pContext.Release();
	m_bKeepPlaceholder = false;
	clearAccumulatedPinyin();
	return true;
}

ITfCompositionSink* CEditMgr::getCompositionSink() const {
	return m_pCompositionSink;
}

const CString& CEditMgr::getAccumulatedPinyin() const {
	return m_accumulatedPinyin;
}

HRESULT CEditMgr::cancelSynchronously(TfClientId clientId)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
    // 关闭前先使排队的写入失效，再请求同步清理现有组串。
    invalidate_edits();
	if (!m_pContext || !exsitComposition())
		return S_FALSE;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CCancelCompositionEditSession(this));
    pEditSession->allow_unfocused_cleanup();
	return RequestEditSessionChecked(
		m_pContext, clientId, pEditSession, TF_ES_SYNC | TF_ES_READWRITE);
}

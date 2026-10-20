#include "pch.h"
#include "editsession.h"
#include "preference_registry.h"
#include "textservice.h"
#include "displayattribute.h"
#include <windows.h>
#include <shellscalingapi.h>

#define PLACEHOLDER L"\u200b"
#define PLACEHOLDER_SPECIAL L" "

namespace
{
std::atomic<std::uint64_t> g_nextEditOperationId{ 1 };

HRESULT ApplyCompositionDisplayAttribute(ITfContext* context, TfEditCookie ec, ITfRange* range)
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

	TfGuidAtom atom = TF_INVALID_GUIDATOM;
	hr = categoryMgr->RegisterGUID(GUID_CoralPinyin_DisplayAttr, &atom);
	if (FAILED(hr))
		return hr;

	CComPtr<ITfProperty> property;
	hr = context->GetProperty(GUID_PROP_ATTRIBUTE, &property);
	if (FAILED(hr))
		return hr;

	VARIANT value;
	VariantInit(&value);
	value.vt = VT_I4;
	value.lVal = static_cast<LONG>(atom);
	hr = property->SetValue(ec, range, &value);
	VariantClear(&value);
	return hr;
}

void ClearCompositionDisplayAttribute(ITfContext* context, TfEditCookie ec, ITfRange* range)
{
	if (!context || !range)
		return;
	CComPtr<ITfProperty> property;
	if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property)) && property)
		property->Clear(ec, range);
}

HRESULT RequestEditSessionChecked(
	ITfContext* context,
	TfClientId clientId,
	CEditSession* editSession,
	DWORD flags)
{
	if (!context || !editSession || clientId == TF_CLIENTID_NULL)
		return E_INVALIDARG;

	HRESULT sessionHr = E_PENDING;
	const HRESULT requestHr = context->RequestEditSession(clientId, editSession, flags, &sessionHr);
	if (FAILED(requestHr))
	{
		LOG_ERROR_W(L"[tsf_edit] event=request_failed operation_id=" +
			std::to_wstring(editSession->operationId()) + L" type=" +
			std::wstring(editSession->operationName()) + L" hr=" +
			std::to_wstring(static_cast<unsigned long>(requestHr)));
		return requestHr;
	}
	if (requestHr == TF_S_ASYNC || sessionHr == TF_S_ASYNC)
		return S_OK;
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
	}
}

CEditSession::~CEditSession() {

}

STDAPI CEditSession::DoEditSession(TfEditCookie ec)
{
	const HRESULT result = execute(ec);
	if (FAILED(result))
	{
		LOG_ERROR_W(L"[tsf_edit] event=execute_failed operation_id=" +
			std::to_wstring(m_operationId) + L" type=" +
			std::wstring(m_operationName) + L" hr=" +
			std::to_wstring(static_cast<unsigned long>(result)));
	}
	return result;
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
		hr = pContextComposition->StartComposition(ec, pRange, pCompositionSink, &pComposition);
		if (hr != S_OK || !pComposition) {
			return hr;
		}
		m_pEditMgr->setComposition(pComposition);

		// Start 由按键接收器异步请求。等到本编辑会话运行时，
		// 按键处理可能已经解析了更多字母；在此写入最新的预编辑文本，
		// 使第一次组串永不为空。
		const CString& text = m_pEditMgr->getAccumulatedPinyin();
		if (!text.IsEmpty())
		{
			hr = pRange->SetText(ec, TF_ST_CORRECTION, text, text.GetLength());
			if (FAILED(hr))
				return hr;

			hr = ApplyCompositionDisplayAttribute(pContext, ec, pRange);
			if (FAILED(hr))
				return hr;

			// 在某些宿主中 SetText 可能把原始范围留在旧锚点。
			// 移动宿主插入符之前重新获取组串范围。
			hr = pComposition->GetRange(&pRange);
			if (FAILED(hr))
				return hr;
			hr = pRange->Collapse(ec, TF_ANCHOR_END);
			if (FAILED(hr))
				return hr;
			TF_SELECTION selection = { 0 };
			selection.range = pRange;
			selection.style.ase = TF_AE_NONE;
			selection.style.fInterimChar = FALSE;
			hr = pContext->SetSelection(ec, 1, &selection);
			if (FAILED(hr))
				return hr;
		}
	}
	return S_OK;
}

HRESULT CEditSession::doEnd(TfEditCookie ec)
{
	ITfComposition* pComposition = m_composition;
	if (pComposition)
	{
		const HRESULT hr = pComposition->EndComposition(ec);
		if (FAILED(hr))
			return hr;
		m_pEditMgr->clearTerminatedComposition(pComposition);
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
	TF_SELECTION selection;
	ULONG nSelection = 0;
	HRESULT hr = pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &nSelection);
	if (!SUCCEEDED(hr))
		return hr;
	CComPtr<ITfContextView> pContextView;
	hr = pContext->GetActiveView(&pContextView);
	if (!SUCCEEDED(hr))
		return hr;
	BOOL fClipped = FALSE;
	CRect rc;
	hr = pContextView->GetTextExt(ec, selection.range, &rc, &fClipped);
	if (selection.range) {
		selection.range->Release();
	}
	//做一个fallback判断,如果tsf获取屏幕坐标失败(不在窗口内)，则从系统获取
	HWND hwndView = NULL;
	pContextView->GetWnd(&hwndView);
	if (hwndView) {
		CRect wrc;
		if (::GetWindowRect(hwndView, &wrc) &&
			(!wrc.PtInRect(rc.TopLeft()) || !wrc.PtInRect(rc.BottomRight())))
		{
			// 从系统插入符号获取屏幕坐标
			DWORD tid = ::GetWindowThreadProcessId(hwndView, NULL);
			GUITHREADINFO gi = { sizeof(GUITHREADINFO) };
			if (::GetGUIThreadInfo(tid, &gi) && ::IsWindow(gi.hwndCaret)) {
				CRect rcCaret = gi.rcCaret;
				::MapWindowPoints(gi.hwndCaret, NULL, (LPPOINT)&rcCaret, 2);
				rc = rcCaret;
			}
		}
	}
	if (!SUCCEEDED(hr))
		return hr;
	if (!m_textBoundOut)
		return E_POINTER;
	*m_textBoundOut = rc;
	return S_OK;
}

HRESULT CEditSession::doInput(TfEditCookie ec)
{
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
	hr = pRange->SetText(ec, TF_ST_CORRECTION, m_text, m_text.GetLength());
	if (FAILED(hr)) return hr;
	hr = pRange->Collapse(ec, m_anchor);
	if (FAILED(hr)) return hr;
	TF_SELECTION tfSelection = { 0 };
	tfSelection.range = pRange;
	tfSelection.style.ase = TF_AE_NONE;
	tfSelection.style.fInterimChar = FALSE;
	return pContext->SetSelection(ec, 1, &tfSelection);
}

HRESULT CEditSession::doComposition(TfEditCookie ec)
{
	ITfContext* pContext = m_context;
	ITfComposition* pComposition = m_composition;
	if (!pContext || !pComposition)
		return S_FALSE;

	CComPtr<ITfRange> pRange;
	HRESULT hr = pComposition->GetRange(&pRange);
	if (hr != S_OK) return hr;

	hr = pRange->SetText(ec, TF_ST_CORRECTION, m_text, m_text.GetLength());
	if (hr != S_OK) return hr;

	hr = ApplyCompositionDisplayAttribute(pContext, ec, pRange);
	if (FAILED(hr)) return hr;

	// 替换后重新获取范围，使选区落在当前预编辑文本之后，
	// 包括保留静态替换范围的编辑器。
	hr = pComposition->GetRange(&pRange);
	if (FAILED(hr)) return hr;
	hr = pRange->Collapse(ec, TF_ANCHOR_END);
	if (hr != S_OK) return hr;

	TF_SELECTION tfSelection = { 0 };
	tfSelection.range = pRange;
	tfSelection.style.ase = TF_AE_NONE;
	tfSelection.style.fInterimChar = FALSE;

	hr = pContext->SetSelection(ec, 1, &tfSelection);
	if (hr != S_OK) return hr;

	return S_OK;
}

HRESULT CEditSession::doCommit(TfEditCookie ec)
{
	ITfContext* pContext = m_context;
	CComPtr<ITfComposition> pComposition = m_composition;
	if (!pContext || !pComposition)
		return S_FALSE;

	CComPtr<ITfRange> pRange;
	HRESULT hr = pComposition->GetRange(&pRange);
	if (FAILED(hr))
		return hr;
	if (FAILED(hr = pRange->SetText(ec, TF_ST_CORRECTION, m_text, m_text.GetLength())))
		return hr;
	ClearCompositionDisplayAttribute(pContext, ec, pRange);
	if (FAILED(hr = pComposition->GetRange(&pRange)))
		return hr;
	if (FAILED(hr = pRange->Collapse(ec, TF_ANCHOR_END)))
		return hr;
	TF_SELECTION selection = { 0 };
	selection.range = pRange;
	selection.style.ase = TF_AE_NONE;
	selection.style.fInterimChar = FALSE;
	if (FAILED(hr = pContext->SetSelection(ec, 1, &selection)))
		return hr;
	hr = pComposition->EndComposition(ec);
	if (SUCCEEDED(hr))
		m_pEditMgr->clearTerminatedComposition(pComposition);
	return hr;
}

HRESULT CEditSession::doCancel(TfEditCookie ec)
{
	CComPtr<ITfComposition> pComposition = m_composition;
	if (!pComposition)
		return S_FALSE;

	CComPtr<ITfRange> pRange;
	HRESULT hr = pComposition->GetRange(&pRange);
	if (FAILED(hr))
		return hr;
	if (FAILED(hr = pRange->SetText(ec, TF_ST_CORRECTION, L"", 0)))
		return hr;
	hr = pComposition->EndComposition(ec);
	if (SUCCEEDED(hr))
		m_pEditMgr->clearTerminatedComposition(pComposition);
	return hr;
}

HRESULT CEditSession::doUpdatePlaceholder(TfEditCookie ec)
{
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

	hr = pRange->SetText(ec, TF_ST_CORRECTION, m_text, m_text.GetLength());
	if (hr != S_OK)
		return hr;

	// 只有PINYINHOLDER模式才把光标放到后面
	hr = pRange->Collapse(ec, m_anchor);
	if (hr != S_OK)
		return hr;

	TF_SELECTION tfSelection = { 0 };
	tfSelection.range = pRange;
	tfSelection.style.ase = TF_AE_NONE;
	tfSelection.style.fInterimChar = FALSE;

	hr = pContext->SetSelection(ec, 1, &tfSelection);
	if (hr != S_OK)
		return hr;

	return S_OK;
}

HRESULT CEditSession::doSetCursorPos(TfEditCookie ec)
{
	ITfContext* pContext = m_context;
	ITfComposition* pComposition = m_composition;
	if (!pContext || !pComposition)
		return S_FALSE;

	CComPtr<ITfRange> pRange;
	HRESULT hr = pComposition->GetRange(&pRange);
	if (hr != S_OK) return hr;

	LONG cchActualMoved = 0; // 用于接收实际移动的字符数
	hr = pRange->ShiftStart(ec, m_cursorPos, &cchActualMoved, NULL);
	if (hr != S_OK) return hr;

	hr = pRange->Collapse(ec, TF_ANCHOR_START);
	if (hr != S_OK) return hr;

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

void CEditMgr::setProcessName(const CString& process_name)
{
	m_processName = process_name;
	std::string processBaseName(CT2A(m_processName, CP_UTF8));
	size_t dotPos = processBaseName.find_last_of('.');
	if (dotPos != std::string::npos) {
		processBaseName = processBaseName.substr(0, dotPos);
	}
	m_scheme = sConfig->getPlaceHolderValue(processBaseName);
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
	if (exsitComposition())
	{
		return S_OK;
	}
	m_pContext = pContext;
	CComPtr<ITfContextView> pContextView;  
	if (pContext->GetActiveView(&pContextView) != S_OK)
	{
		return S_FALSE;
	}
	m_pCompositionSink = pCompositionSink;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CStartCompositionEditSession(this));
	return RequestEditSessionChecked(
		pContext, clientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

HRESULT CEditMgr::end(TfClientId clientId)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
	setAboutToShow(false);
	setPositionEnhanced(false);
	if (!m_pContext)
		return E_FAIL;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CEndCompositionEditSession(this));
	return RequestEditSessionChecked(
		m_pContext, clientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

HRESULT CEditMgr::commit(TfClientId clientId, const CString& text)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
	if (!m_pContext || !exsitComposition())
		return S_FALSE;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CCommitCompositionEditSession(this, text));
	return RequestEditSessionChecked(
		m_pContext, clientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE);
}

HRESULT CEditMgr::cancel(TfClientId clientId)
{
	ASSERT(clientId != TF_CLIENTID_NULL);
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
	
	// 此路径只用于最终输出。把替换与 EndComposition 放进同一个
	// 写会话，可避免已上屏词
	// 在目标应用中仍处于组串范围。
	return commit(clientId, text);
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
	m_pContext = std::move(pContext);
}

void CEditMgr::setComposition(CComPtr<ITfComposition> pComposition) {
	m_pComposition = std::move(pComposition);
}

bool CEditMgr::clearTerminatedComposition(ITfComposition* pComposition)
{
	if (!pComposition || m_pComposition != pComposition)
		return false;
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
	if (!m_pContext || !exsitComposition())
		return S_FALSE;
	CComPtr<CEditSession> pEditSession;
	pEditSession.Attach(new CCancelCompositionEditSession(this));
	return RequestEditSessionChecked(
		m_pContext, clientId, pEditSession, TF_ES_SYNC | TF_ES_READWRITE);
}

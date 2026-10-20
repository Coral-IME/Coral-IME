#include "pch.h"
#include "texteditsink.h"
#include "textservice.h"
#include "preference_registry.h"

BOOL isRangeCovered(TfEditCookie ec, const CComPtr<ITfRange>& pRangeTest, const CComPtr<ITfRange>& pRangeCover)
{
	LONG lResult = 0;
	if (pRangeCover->CompareStart(ec, pRangeTest, TF_ANCHOR_START, &lResult) != S_OK || lResult > 0)
	{
		return FALSE;
	}
	if (pRangeCover->CompareEnd(ec, pRangeTest, TF_ANCHOR_END, &lResult) != S_OK || lResult < 0)
	{
		return FALSE;
	}
	return TRUE;
}

STDAPI CTextEditSink::OnEndEdit(ITfContext* pic, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord)
{
	if (!m_pTextService)
		return S_OK;
	coral::tsf::ContextToken previousToken;
	if (const auto* previous = m_pTextService->activeContextState())
		previousToken = previous->token();
	if (FAILED(m_pTextService->selectContextController(pic)))
		return S_OK;
	BOOL bChanged = FALSE;

	if (pEditRecord->GetSelectionStatus(&bChanged) == S_OK && bChanged)
	{
		if (m_pTextService && m_pTextService->exsitComposition())
		{
			TF_SELECTION tfSelection = { 0 };
			ULONG cFetched = { 0 };
			if (pic->GetSelection(ecReadOnly, TF_DEFAULT_SELECTION, 1, &tfSelection, &cFetched) == S_OK && cFetched == 1)
			{
				CComPtr<ITfRange> pRange;
				if (m_pTextService->getComposition()->GetRange(&pRange) == S_OK)
				{
					if (!isRangeCovered(ecReadOnly, tfSelection.range, pRange))
					{
						m_pTextService->endEditSession();
					}
				}
				if (tfSelection.range) {
					tfSelection.range->Release();
				}
			}
		}
	}
	if (const auto* previous =
		m_pTextService->m_tsfThreadContext.contexts().find(previousToken))
	{
		if (previous->context() != pic)
			m_pTextService->selectContextController(previous->context());
	}
	else
		m_pTextService->clearActiveContext();
	return S_OK;
}

CTextEditSink::CTextEditSink(CTextService* pTextService)
	: m_pTextService(pTextService)
{}

CTextEditSink::~CTextEditSink()
{}

BOOL CTextEditSink::init()
{
	return internalInit(NULL);
}

BOOL CTextEditSink::reInit(const CComPtr<ITfDocumentMgr>& pOtherDocMgr)
{
	unInit();
	return internalInit(pOtherDocMgr);
}

BOOL CTextEditSink::internalInit(const CComPtr<ITfDocumentMgr>& pOtherDocMgr)
{
	CComPtr<ITfDocumentMgr> pDocMgr;

	if (pOtherDocMgr)
	{
		pDocMgr = pOtherDocMgr;
	}
	else
	{
		if (m_pTextService && m_pTextService->getThreadMgr())
			m_pTextService->getThreadMgr()->GetFocus(&pDocMgr);
	}
	if (!pDocMgr)
		return TRUE;
	CComPtr<ITfContext> context;
	if (FAILED(pDocMgr->GetTop(&context)) || !context)
		return TRUE;
	return SUCCEEDED(m_pTextService->m_tsfThreadContext.contexts().ensure(context));
}

void CTextEditSink::unInit()
{
	// ContextState 持有每上下文的订阅。当 Context 弹出或服务
	// 停用时，其析构函数会撤销两个接收器的通告。
}


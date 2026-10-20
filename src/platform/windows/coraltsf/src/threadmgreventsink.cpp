#include "pch.h"
#include "threadmgreventsink.h"
#include "textservice.h"
#include "candidatelistuielement.h"

STDAPI CThreadMgrEventSink::OnInitDocumentMgr(ITfDocumentMgr* pdim)
{
	if (!pdim)
		return E_INVALIDARG;
	CComPtr<ITfContext> context;
	if (SUCCEEDED(pdim->GetTop(&context)) && context)
		return m_pTextService->m_tsfThreadContext.contexts().ensure(context);
	return S_OK;
}

STDAPI CThreadMgrEventSink::OnUninitDocumentMgr(ITfDocumentMgr* pdim)
{
	if (const auto* active = m_pTextService->activeContextState())
	{
		CComPtr<ITfDocumentMgr> owner;
		if (SUCCEEDED(active->context()->GetDocumentMgr(&owner)) && owner == pdim)
			m_pTextService->clearActiveContext();
	}
	for (const auto handle : m_pTextService->m_tsfThreadContext.contexts().engineSessionsForDocument(pdim))
		m_pTextService->session_bridge_->destroy_session(handle.value);
	m_pTextService->m_tsfThreadContext.contexts().closeDocumentManager(pdim);
	return S_OK;
}

STDAPI CThreadMgrEventSink::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus)
{
	coral::tsf::ContextState* previousState = m_pTextService->activeContextState();
	CComPtr<ITfContext> focusedContext;
	if (pdimFocus)
		pdimFocus->GetTop(&focusedContext);
	if (previousState && previousState->context() != focusedContext)
		previousState->candidateUIElement().endUI();
    const bool focus_changed = pdimFocus != pdimPrevFocus || !previousState || previousState->context() != focusedContext;
    if (focus_changed) m_pTextService->resetConditionWord();
	const HRESULT focusHr = m_pTextService->m_tsfThreadContext.contexts().setFocused(focusedContext);
	if (FAILED(focusHr))
		return focusHr;
	if (focusedContext)
	{
		const HRESULT selectHr = m_pTextService->selectContextController(focusedContext);
		if (FAILED(selectHr))
			return selectHr;
		if (focus_changed) m_pTextService->resetConditionWord();
		m_pTextService->publishCandidateSnapshot();
		m_pTextService->publishToolbarSnapshot(m_pTextService->m_remoteToolbarVisible);
		if (const auto* active = m_pTextService->activeContextState();
			active && active->candidates().visible)
		{
			m_pTextService->beginCandidateUIElement();
		}
	}
	else
	{
		if (const auto* previous = m_pTextService->activeContextState())
			m_pTextService->m_rendererCoordinator.hideContext(
				*previous, previous->candidates().version + 1);
		m_pTextService->clearActiveContext();
		m_pTextService->publishToolbarSnapshot(m_pTextService->m_remoteToolbarVisible);
	}
	return m_pTextService->m_pTextEditSink->reInit(pdimFocus) ? S_OK : E_FAIL;
}

STDAPI CThreadMgrEventSink::OnPushContext(ITfContext* pic)
{
	return m_pTextService->m_tsfThreadContext.contexts().ensure(pic);
}

STDAPI CThreadMgrEventSink::OnPopContext(ITfContext* pic)
{
	if (m_pTextService && m_pTextService->m_pKeyEventSink)
		m_pTextService->m_pKeyEventSink->clearContext(pic);
	coral::tsf::ContextState* state = m_pTextService->m_tsfThreadContext.contexts().find(pic);
	if (state && state->engineSession().valid())
		m_pTextService->session_bridge_->destroy_session(state->engineSession().value);
	if (state && m_pTextService->m_activeContextToken.contextId == state->token().contextId)
		m_pTextService->clearActiveContext();
	const HRESULT hr = m_pTextService->m_tsfThreadContext.contexts().close(pic);
	return hr == S_FALSE ? S_OK : hr;
}

CThreadMgrEventSink::CThreadMgrEventSink(CTextService* pTextService)
	: m_pTextService(pTextService)
{
}

CThreadMgrEventSink::~CThreadMgrEventSink()
{

}

BOOL CThreadMgrEventSink::init()
{
	return SUCCEEDED(m_subscription.advise(
		m_pTextService->getThreadMgr(), IID_ITfThreadMgrEventSink,
		static_cast<ITfThreadMgrEventSink*>(m_pTextService)));
}

void CThreadMgrEventSink::unInit()
{
	m_subscription.reset();
}

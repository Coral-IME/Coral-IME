#include "pch.h"
#include "threadfocussink.h"
#include "textservice.h"

STDAPI CThreadFocussink::OnSetThreadFocus(void)
{
	m_pTextService->showLanguageBar();
	m_pTextService->showFloatingToolbar(TRUE);
	// 失焦期间发布的任何隐藏（包括瞬时的文档焦点丢失）
	// 只能通过重新发布聚焦上下文来撤销。
	m_pTextService->republishFocusedCandidate();
	LOG_TRACE_W(L"[CThreadFocussink::OnSetThreadFocus]");
	return S_OK;
}

STDAPI CThreadFocussink::OnKillThreadFocus(void)
{
	m_pTextService->resetConditionWord();
	m_pTextService->showFloatingToolbar(FALSE);
	LOG_TRACE_W(L"[CThreadFocussink::OnKillThreadFocus]");
	m_pTextService->hideCandidateDlg(TRUE);
	return S_OK;
}

CThreadFocussink::CThreadFocussink(CTextService* pTextService)
	: m_pTextService(pTextService)
{

}

CThreadFocussink::~CThreadFocussink()
{}

BOOL CThreadFocussink::init()
{
	return SUCCEEDED(m_subscription.advise(
		m_pTextService->getThreadMgr(), IID_ITfThreadFocusSink,
		static_cast<ITfThreadFocusSink*>(m_pTextService)));
}

void CThreadFocussink::unInit()
{
	m_subscription.reset();
}

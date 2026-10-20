#include "pch.h"
#include "textlayoutsink.h"
#include "textservice.h"
#include "editsession.h"

STDAPI CTextLayoutSink::OnLayoutChange(ITfContext* pic, TfLayoutCode lcode, ITfContextView* pView)
{
	auto* state = m_pTextService->m_tsfThreadContext.contexts().find(pic);
	if (lcode == TF_LC_CHANGE && state && state->focused() &&
		SUCCEEDED(m_pTextService->selectContextController(pic)))
	{
		CRect rc;
		if (S_OK == m_pTextService->layoutChangeGetTextBound(rc))
		{
			m_pTextService->adjustCandidateDlgPos(rc);
		}
	}
	return S_OK;
}

CTextLayoutSink::CTextLayoutSink(CTextService* pTextService)
	: m_pTextService(pTextService)
{}

CTextLayoutSink::~CTextLayoutSink()
{}

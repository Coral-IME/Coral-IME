#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_THREADMGREVENTSINK_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_THREADMGREVENTSINK_H_INCLUDED

#include "sinksubscription.h"

class CTextService;
class CThreadMgrEventSink
{
public:
	CThreadMgrEventSink(CTextService* pTextService);
	~CThreadMgrEventSink();

public:
	STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim);
	STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim);
	STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus);
	STDMETHODIMP OnPushContext(ITfContext* pic);
	STDMETHODIMP OnPopContext(ITfContext* pic);

public:
	BOOL init();
	void unInit();

private:
	coral::tsf::SinkSubscription m_subscription;
	CTextService* m_pTextService = nullptr;
};

#endif // THREADMGREVENTSINK_H

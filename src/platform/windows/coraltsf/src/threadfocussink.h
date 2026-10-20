#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_THREADFOCUSSINK_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_THREADFOCUSSINK_H_INCLUDED

#include "sinksubscription.h"

class CTextService;
class CThreadFocussink
{
public:
	CThreadFocussink(CTextService* pTextService);
	~CThreadFocussink();

public:
	STDMETHODIMP OnSetThreadFocus(void);
	STDMETHODIMP OnKillThreadFocus(void);

public:
	BOOL init();
	void unInit();

private:
	coral::tsf::SinkSubscription m_subscription;
	CTextService* m_pTextService = nullptr;
};

#endif // THREADFOCUSSINK_H

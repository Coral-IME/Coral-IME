#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TEXTLAYOUTSINK_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TEXTLAYOUTSINK_H_INCLUDED

class CTextService;
class CTextLayoutSink
{
public:
	CTextLayoutSink(CTextService* pTextService);
	~CTextLayoutSink();

public:
	STDMETHODIMP OnLayoutChange(ITfContext* pic, TfLayoutCode lcode, ITfContextView* pView);

private:
	CTextService* m_pTextService = nullptr;
};

#endif // TEXTLAYOUTSINK_H

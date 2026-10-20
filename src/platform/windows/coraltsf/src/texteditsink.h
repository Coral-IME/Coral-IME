#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TEXTEDITSINK_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TEXTEDITSINK_H_INCLUDED

class CTextService;
class CTextEditSink
{
public:
	CTextEditSink(CTextService* pTextService);
	~CTextEditSink();

public:
	STDMETHODIMP OnEndEdit(ITfContext* pContext, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord);

public:
	BOOL init();
	BOOL reInit(const CComPtr<ITfDocumentMgr>& pOtherDocMgr);
	void unInit();

private:
	BOOL internalInit(const CComPtr<ITfDocumentMgr>& pOtherDocMgr);
private:
	CTextService* m_pTextService = nullptr;
};

#endif // TEXTEDITSINK_H

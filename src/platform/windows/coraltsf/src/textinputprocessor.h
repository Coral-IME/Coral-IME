#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TEXTINPUTPROCESSOR_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TEXTINPUTPROCESSOR_H_INCLUDED

class CTextService;
class CTextInputProcessor : public ITfTextInputProcessorEx
{
public:
    CTextInputProcessor(const CTextService* pTextService);
    ~CTextInputProcessor();
public:
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid);
    STDMETHODIMP Deactivate(void);
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags);

private:
    CTextService* m_pTextService = nullptr;
};

#endif // TEXTINPUTPROCESSOR_H

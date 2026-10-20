#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_LANGUAGEPROFILE_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_LANGUAGEPROFILE_H_INCLUDED

class CTextService;
class CLangBarItemButton
    : public ITfLangBarItemButton
	, public ITfSource
{
public:
    ~CLangBarItemButton();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
        if (ppvObject == NULL)
            return E_INVALIDARG;

        *ppvObject = NULL;

        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
            IsEqualIID(riid, IID_ITfLangBarItemButton))
            *ppvObject = (ITfLangBarItemButton*)this;
        else if(IsEqualIID(riid, IID_ITfSource))
            *ppvObject = (ITfSource*)this;

        if (*ppvObject) {
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() { return ++m_cRef; }

    STDMETHODIMP_(ULONG) Release() {
        LONG cr = --m_cRef;
        ASSERT(m_cRef >= 0);
        if (m_cRef == 0)
            delete this;
        return cr;
    }
public:
    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo);
    STDMETHODIMP GetStatus(DWORD* pdwStatus);
    STDMETHODIMP Show(BOOL fShow);
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip);


    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea);
    STDMETHODIMP InitMenu(ITfMenu* pMenu);
    STDMETHODIMP OnMenuSelect(UINT wID);
    STDMETHODIMP GetIcon(HICON* phIcon);
    STDMETHODIMP GetText(BSTR* pbstrText);

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie);
    STDMETHODIMP UnadviseSink(DWORD dwCookie);
    void updateAsciiMode();
    void updateIcon();
    bool getAsciiMode();
    void setTextService(CTextService* textService);

public:
    void setLangBarStatus(DWORD dwStatus, BOOL bSet);

private:
    LONG m_cRef = 0; /*  COM 引用计数 */
    DWORD m_status = 0;
    bool m_asciiMode = false;
    CTextService* m_pTextService = nullptr;
    CComPtr<ITfLangBarItemSink> m_pLangBarItemSink;
};
////////////////////////////////////////////////////////////////////////////////
///
class CTextService;
class CLanguageProfile
{
public:
	CLanguageProfile(CTextService* pTextService);
	~CLanguageProfile();

public:
	STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated);

public:
	BOOL init();
	void unInit();
	void showLanguageBar(BOOL show);
	void enableLanguageBar(BOOL enable);
    void updateAsciiMode();
    bool getAsciiMode();
    void updateIcon();

private:
	CTextService* m_pTextService = nullptr;
	CComPtr<CLangBarItemButton> m_pLangBarItemButton;
	bool m_asciiMode = false;
};

#endif // LANGUAGEPROFILE_H

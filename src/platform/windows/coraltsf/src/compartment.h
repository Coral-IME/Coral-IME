#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_COMPARTMENT_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_COMPARTMENT_H_INCLUDED

using Callback = std::function<HRESULT(REFGUID guidCompartment)>;

class CTextService;
class CCompartmentEventSink
{
public:
	CCompartmentEventSink(CTextService* pTextService);
	~CCompartmentEventSink();

public:
	STDMETHODIMP OnChange(REFGUID guid);

public:
	BOOL init();
	void unInit();
	BOOL isKeyboardDisable();
	BOOL isKeyboardOpen();
	HRESULT setKeyboardOpen(BOOL isOpen);

	HRESULT updateCompartmentString(const GUID& rguidCompartment, const WCHAR* pszNewString);
	HRESULT updateFullShapeMode(BOOL bFullShape);
	HRESULT updateChineseMode(BOOL bChineseMode);
	HRESULT updateChinesePunctuationMode(BOOL bChinesePunctuationMode);

private:
	HRESULT advise(const CComPtr<IUnknown>& punk, REFGUID guidCompartment);
	HRESULT unAdvise();
	HRESULT handleCompartment(REFGUID guidCompartment);

private:
	CTextService* m_pTextService = nullptr;
	CComPtr<ITfCompartment> m_compartment;
	DWORD m_cookie = 0;
	Callback m_callback;
};

#endif // COMPARTMENT_H

#include "pch.h"
#include "languageprofile.h"
#include "textservice.h"

static const GUID GUID_LBIB_INPUTMODE = { 0x2C77A81E, 0x41CC, 0x4178, { 0xA3, 0xA7, 0x5F, 0x8A, 0x98, 0x75, 0x68, 0xE6 } };
#define TEXTSERVICE_LANGBARITEMSINK_COOKIE 0x0eab0fac

CLangBarItemButton::~CLangBarItemButton() = default;
STDAPI CLangBarItemButton::GetInfo(TF_LANGBARITEMINFO* pInfo)
{
    pInfo->clsidService = CLSID_TextService;
    pInfo->guidItem = GUID_LBIB_INPUTMODE;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU | TF_LBI_STYLE_SHOWNINTRAY;
    pInfo->ulSort = 1;
	wcsncpy_s(
		pInfo->szDescription,                
		_countof(pInfo->szDescription),      
		(LPCWSTR)hlp::_tr(IDS_BUTTON_DEC),   
		_TRUNCATE                           
	);
    return S_OK;
}

STDAPI CLangBarItemButton::GetStatus(DWORD* pdwStatus)
{
    *pdwStatus = m_status;
    return S_OK;
}

STDAPI CLangBarItemButton::Show(BOOL fShow)
{
    setLangBarStatus(TF_LBI_STATUS_HIDDEN, fShow ? FALSE : TRUE);
    return S_OK;
}
STDAPI CLangBarItemButton::GetTooltipString(BSTR* pbstrToolTip)
{
    *pbstrToolTip = SysAllocString(hlp::_tr(IDS_TOOL_TIP_DEC));
    return (*pbstrToolTip == NULL) ? E_OUTOFMEMORY : S_OK;
}

STDAPI CLangBarItemButton::OnClick(TfLBIClick click, POINT pt, const RECT* prcArea)
{
    if (click == TF_LBI_CLK_LEFT)
    {
        LOG_TRACE_W(L"CLangBarItemButton: Left-click on the language bar to switch between Chinese and English");
        if ((GetKeyState(VK_CAPITAL) & 0x0001) != 0)
        {
            m_pTextService->changeCapsLock();
        }
        else
        {
            m_pTextService->handleFloatingToolbarClick(Ascii);
        }
    }
    else if (click == TF_LBI_CLK_RIGHT)
    {
        LOG_TRACE_W(L"CLangBarItemButton: Right-click opens the quick settings menu");
        if (m_pTextService) m_pTextService->openSettingsMenuFromUi(prcArea);
    }
    return S_OK;
}

STDAPI CLangBarItemButton::InitMenu(ITfMenu* pMenu)
{
    return S_OK;
}

STDAPI CLangBarItemButton::OnMenuSelect(UINT wID)
{
    return S_OK;
}

STDAPI CLangBarItemButton::GetIcon(HICON* phIcon)
{
    INT nIconID = 0; // 用于存放最终选择的图标资源ID
    if ((GetKeyState(VK_CAPITAL) & 0x0001) != 0)
    {
        // 如果 CapsLock 开启，无论当前是中/英模式，都显示大写状态图标 'A'
        nIconID = IDI_A;
    }
    else
    {
        // 如果 CapsLock 关闭，根据 m_asciiMode 判断显示“中”或“英”
        nIconID = m_asciiMode ? IDI_EN : IDI_ZH;
    }

    // 使用最终确定的图标ID来加载图标
    *phIcon = (HICON)LoadImageW(
        g_hInst,
        MAKEINTRESOURCEW(nIconID),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_SHARED);

    return (*phIcon != NULL) ? S_OK : E_FAIL;
}

STDAPI CLangBarItemButton::GetText(BSTR* pbstrText)
{
    *pbstrText = SysAllocString(hlp::_tr(IDS_BUTTON_DEC));

    return (*pbstrText == NULL) ? E_OUTOFMEMORY : S_OK;
}

STDAPI CLangBarItemButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie)
{
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
    {
        return CONNECT_E_CANNOTCONNECT;
    }
    if (m_pLangBarItemSink != NULL)
    {
        return CONNECT_E_ADVISELIMIT;
    }
    if (punk->QueryInterface(&m_pLangBarItemSink) != S_OK)
    {
        m_pLangBarItemSink = NULL;
        return E_NOINTERFACE;
    }
    *pdwCookie = TEXTSERVICE_LANGBARITEMSINK_COOKIE;
    return S_OK;
}

STDAPI CLangBarItemButton::UnadviseSink(DWORD dwCookie)
{
    if (dwCookie != TEXTSERVICE_LANGBARITEMSINK_COOKIE || m_pLangBarItemSink == NULL)
    {
        return CONNECT_E_NOCONNECTION;
    }
    m_pLangBarItemSink = NULL;

    return S_OK;
}

void CLangBarItemButton::setLangBarStatus(DWORD dwStatus, BOOL bSet)
{
    BOOL isChange = FALSE;

    if (bSet)
    {
        if (!(m_status & dwStatus))
        {
            m_status |= dwStatus;
            isChange = TRUE;
        }
    }
    else
    {
        if (m_status & dwStatus)
        {
            m_status &= ~dwStatus;
            isChange = TRUE;
        }
    }

    if (isChange && m_pLangBarItemSink)
    {
        m_pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
    }
}

void CLangBarItemButton::updateAsciiMode()
{
    m_asciiMode = !m_asciiMode;
}

bool CLangBarItemButton::getAsciiMode()
{
    return m_asciiMode;
}

void CLangBarItemButton::setTextService(CTextService* textService)
{
    m_pTextService = textService;
}

void CLangBarItemButton::updateIcon()
{
    if (m_pLangBarItemSink)
    {
        m_pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////
STDAPI CLanguageProfile::OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated)
{
    if (!::IsEqualCLSID(clsid, CLSID_TextService))
    {
        return S_OK;
    }
    showLanguageBar(fActivated);
    return S_OK;
}

CLanguageProfile::CLanguageProfile(CTextService* pTextService)
    :m_pTextService(pTextService)
{}

CLanguageProfile::~CLanguageProfile()
{}

BOOL IsRunningWindows7()
{
	static BOOL s_isWindows7 = IsWindows7OrGreater() && !IsWindows8OrGreater();
	return s_isWindows7;
}

BOOL CLanguageProfile::init()
{
	CComPtr<ITfLangBarItemMgr> pLangBarItemMgr;
	if (!m_pTextService->getThreadMgr() ||
		FAILED(m_pTextService->getThreadMgr()->QueryInterface(&pLangBarItemMgr)) ||
		!pLangBarItemMgr)
	{
		LOG_INFO_W(L"[CLanguageProfile::init] Language bar is unavailable; input mode remains active.");
		return TRUE;
	}
	m_pLangBarItemButton = new CLangBarItemButton();
	m_pLangBarItemButton->setTextService(m_pTextService);
	if (m_asciiMode) m_pLangBarItemButton->updateAsciiMode();
	if (FAILED(pLangBarItemMgr->AddItem(m_pLangBarItemButton)))
	{
		LOG_INFO_W(L"[CLanguageProfile::init] Language bar item was declined; input mode remains active.");
		m_pLangBarItemButton.Release();
		return TRUE;
	}
	if (IsRunningWindows7())
		showLanguageBar(FALSE);
	return TRUE;
}

void CLanguageProfile::unInit()
{
    CComPtr<ITfLangBarItemMgr> pLangBarItemMgr;
    if (m_pTextService->getThreadMgr()->QueryInterface(&pLangBarItemMgr) == S_OK)
    {
        if (m_pLangBarItemButton)
        {
            pLangBarItemMgr->RemoveItem(m_pLangBarItemButton);
            m_pLangBarItemButton = NULL;
        }
    }
}

void CLanguageProfile::showLanguageBar(BOOL show)
{
    if (m_pLangBarItemButton)
    {
        m_pLangBarItemButton->Show(show);
    }
}

void CLanguageProfile::enableLanguageBar(BOOL enable)
{
    if (m_pLangBarItemButton)
    {
        m_pLangBarItemButton->setLangBarStatus(TF_LBI_STATUS_DISABLED, !enable);
    }
}

void CLanguageProfile::updateAsciiMode()
{
	m_asciiMode = !m_asciiMode;
    if (m_pLangBarItemButton)
    {
        m_pLangBarItemButton->updateAsciiMode();
        m_pLangBarItemButton->updateIcon();
    }
	m_pTextService->initFloatingToolbarIcon();
}

bool CLanguageProfile::getAsciiMode()
{
	return m_asciiMode;
}

void CLanguageProfile::updateIcon()
{
    if (m_pLangBarItemButton)
    {
        m_pLangBarItemButton->updateIcon();
        m_pTextService->initFloatingToolbarIcon();
    }
}

#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_CANDIDATELISTUIELEMENT_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_CANDIDATELISTUIELEMENT_H_INCLUDED

#include <ctffunc.h>

class CTextService;
namespace coral::tsf { struct CandidateSnapshot; }
class CCandidateListUIElement
	: public ITfCandidateListUIElementBehavior
	, public ITfIntegratableCandidateListUIElement
{

public:
	CCandidateListUIElement(CTextService* pTextService);
	virtual ~CCandidateListUIElement();   // 无论通过基类指针还是派生类指针删除对象，都会调用正确的析构函数

	// IUnkonw
	 STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
	 ULONG STDMETHODCALLTYPE AddRef() override;
	 ULONG STDMETHODCALLTYPE Release() override;

	// ITfUIElement
	STDMETHODIMP GetDescription(BSTR* pbstr);
	STDMETHODIMP GetGUID(GUID* pguid);
	STDMETHODIMP Show(BOOL bShow);
	STDMETHODIMP IsShown(BOOL* pbShow);

	// ITfCandidateListUIElement
	STDMETHODIMP GetUpdatedFlags(DWORD* pdwFlags);
	STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** ppdim);
	STDMETHODIMP GetCount(UINT* pcCount);
	STDMETHODIMP GetSelection(UINT* puIndex);
	STDMETHODIMP GetString(UINT uIndex, BSTR* pstr);
	STDMETHODIMP GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt);
	STDMETHODIMP SetPageIndex(UINT* pIndex, UINT uPageCnt);
	STDMETHODIMP GetCurrentPage(UINT* puPage);
	STDMETHODIMP SetSelection(UINT index);
	STDMETHODIMP Finalize();
	STDMETHODIMP Abort();

	// ITfIntegratableCandidateListUIElement（Windows 搜索候选 UI）
	STDMETHODIMP SetIntegrationStyle(GUID guidIntegrationStyle);
	STDMETHODIMP GetSelectionStyle(TfIntegratableCandidateListSelectionStyle* style);
	STDMETHODIMP OnKeyDown(WPARAM wParam, LPARAM lParam, BOOL* eaten);
	STDMETHODIMP ShowCandidateNumbers(BOOL* show);
	STDMETHODIMP FinalizeExactCompositionString();

	STDMETHODIMP GetWnd(HWND* phwnd);


public:
	BOOL init();
	void unInit();
	BOOL compareUIElementCookie(DWORD dwUIElementId);
	DWORD getCandidateListCookie();
	bool allowsServerWindow() const { return m_allowServerWindow; }
	bool isBegun() const { return m_lifecycle == Lifecycle::Begun; }
	bool isShown() const { return m_bIsShown != FALSE; }
	void beginUI(BOOL& isAllowShow);
	void updateUI();
	void endUI();

	// 管理候选数据的自定义方法
	void syncFrom(const coral::tsf::CandidateSnapshot& snapshot);
	const std::vector<CString>& getCandidates() const;

private:
	enum class Lifecycle
	{
		Uninitialized,
		Idle,
		Begun,
	};

	void markUpdated(DWORD flags);
	void notifyLegacyCandidate(bool open);

	CComPtr<ITfUIElementMgr> m_pUIElementMgr;
	CComPtr<ITfDocumentMgr> m_pDocumentMgr;
	CComPtr<ITfContext> m_pContext;
	CTextService* m_pTextService = nullptr;
	std::vector<CString> m_candidates;
	DWORD m_dwUIElementID = TF_INVALID_COOKIE;
	DWORD m_updatedFlags = 0;
	Lifecycle m_lifecycle = Lifecycle::Uninitialized;
	long m_cRef = 1;
	UINT m_uSelectedCandidateIndex = 0;      // 当前选中索引
	BOOL m_bIsShown = FALSE;                 // UI 元素当前的可见状态
	bool m_allowServerWindow = true;
	bool m_legacyCandidateOpen = false;
	ULONG m_uPageSize = 7;                   // 每页候选数量
	UINT m_uCurrentPageIndex = 0;            // 当前页索引
};

#endif // CANDIDATELISTUIELEMENT_H

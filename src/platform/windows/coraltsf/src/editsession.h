#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_EDITSESSION_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_EDITSESSION_H_INCLUDED

#define MAX_BUFFER_SIZE 2048
#include <functional>

struct EditResult {
    HRESULT status = E_PENDING;
    bool text_written = false;
    bool composition_ended = false;
};
using EditCompletion = std::function<void(const EditResult&)>;

class CEditMgr;
class CTextService;

class CEditSession : public ITfEditSession {
public:
	virtual ~CEditSession();

protected:
	CEditSession(
		CEditMgr* pEditMgr,
		const wchar_t* operationName,
		const CString& text = CString(),
		TfAnchor anchor = TF_ANCHOR_START,
		int cursorPos = 0,
		CRect* textBoundOut = nullptr);
	virtual HRESULT execute(TfEditCookie ec) = 0;

	public:
	/* IUnknown */
	STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
		if (ppvObject == NULL)
			return E_INVALIDARG;

		*ppvObject = NULL;

		if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
			*ppvObject = (ITfEditSession*)this;

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

	/* ITfEditSession */
	STDMETHODIMP DoEditSession(TfEditCookie ec) final;
	std::uint64_t operationId() const { return m_operationId; }
	const wchar_t* operationName() const { return m_operationName; }
    void set_completion(EditCompletion callback, std::function<bool()> preflight = {});
    void complete_request(HRESULT status);
    void allow_unfocused_cleanup() { unfocused_cleanup_ = true; }

public:

	HRESULT FinalConstruct()
	{
		return S_OK;
	}
	void FinalRelease()
	{
	}
protected:
	HRESULT doStart(TfEditCookie ec);
	HRESULT doEnd(TfEditCookie ec);
	HRESULT doTextBound(TfEditCookie ec);
	HRESULT doInput(TfEditCookie ec);
	HRESULT doComposition(TfEditCookie ec);
	HRESULT doSetCursorPos(TfEditCookie ec);
	HRESULT doUpdatePlaceholder(TfEditCookie ec);
	HRESULT doCommit(TfEditCookie ec);
	HRESULT doCancel(TfEditCookie ec);
private:
	LONG m_cRef = 0; /*  COM 引用计数 */
	std::uint64_t m_operationId = 0;
	const wchar_t* m_operationName = L"unknown";
    EditCompletion completion_;
    std::function<bool()> preflight_;
    EditResult result_;
    bool completed_ = false;
    bool unfocused_cleanup_ = false;
    std::uint64_t edit_epoch_ = 0, focus_epoch_ = 0;
    bool current(bool require_focus) const;

protected:
	// TSF 可能在 ContextState 关闭后仍执行异步编辑会话。
	// 在 TSF 释放本编辑会话之前保持控制器存活。
	std::shared_ptr<CEditMgr> m_editMgrGuard;
	CEditMgr* m_pEditMgr = nullptr;
	CComPtr<ITfCompositionSink> m_lifetimeGuard;
	CComPtr<ITfContext> m_context;
	CComPtr<ITfComposition> m_composition;
	CString m_text;
	TfAnchor m_anchor = TF_ANCHOR_START;
	int m_cursorPos = 0;
	CRect* m_textBoundOut = nullptr; // 只用于 TF_ES_SYNC 的位置查询
};

class CStartCompositionEditSession final : public CEditSession {
public: explicit CStartCompositionEditSession(CEditMgr* manager);
private: HRESULT execute(TfEditCookie ec) override;
};
class CEndCompositionEditSession final : public CEditSession {
public: explicit CEndCompositionEditSession(CEditMgr* manager);
private: HRESULT execute(TfEditCookie ec) override;
};
class CTextBoundEditSession final : public CEditSession {
public: CTextBoundEditSession(CEditMgr* manager, CRect* output);
private: HRESULT execute(TfEditCookie ec) override;
};
class CInsertTextEditSession final : public CEditSession {
public: CInsertTextEditSession(CEditMgr* manager, const CString& text, TfAnchor anchor);
private: HRESULT execute(TfEditCookie ec) override;
};
class CUpdateCompositionEditSession final : public CEditSession {
public: CUpdateCompositionEditSession(CEditMgr* manager, const CString& text);
private: HRESULT execute(TfEditCookie ec) override;
};
class CSetCursorEditSession final : public CEditSession {
public: CSetCursorEditSession(CEditMgr* manager, int position);
private: HRESULT execute(TfEditCookie ec) override;
};
class CUpdatePlaceholderEditSession final : public CEditSession {
public: CUpdatePlaceholderEditSession(CEditMgr* manager, const CString& text, TfAnchor anchor);
private: HRESULT execute(TfEditCookie ec) override;
};
class CCommitCompositionEditSession final : public CEditSession {
public: CCommitCompositionEditSession(CEditMgr* manager, const CString& text);
private: HRESULT execute(TfEditCookie ec) override;
};
class CCancelCompositionEditSession final : public CEditSession {
public: explicit CCancelCompositionEditSession(CEditMgr* manager);
private: HRESULT execute(TfEditCookie ec) override;
};


class CEditMgr : public std::enable_shared_from_this<CEditMgr> {
public:
	CEditMgr(CTextService* pTextService);

	virtual ~CEditMgr();

	enum
	{
		NO_LETTER_NUM = 0,
		NEED_FILTER,
		VAILD_STR
	};
public:

public:
	HRESULT start(TfClientId clientId, ITfContext* pContext, ITfCompositionSink* pTextService);
	HRESULT end(TfClientId client_id, EditCompletion completion = {});
	HRESULT commit(TfClientId clientId, const CString& text);
	HRESULT cancel(TfClientId clientId);
	HRESULT cancelSynchronously(TfClientId clientId);
	HRESULT updateComposition(TfClientId clientId, const CString& text);
	HRESULT setCursorPos(TfClientId clientId, int pos);
	HRESULT insertText(TfClientId clientId, const CString& text);
    HRESULT insertText(TfClientId client_id, const CString& text, EditCompletion completion,
        std::function<bool()> preflight);
    void set_edit_focus(bool focused);
    void invalidate_edits();
    void retire_edits();
    void set_focus_validator(std::function<bool(ITfContext*)> validator) { focus_validator_ = std::move(validator); }
    bool edit_current(std::uint64_t epoch, std::uint64_t focus, bool require_focus) const;
    std::uint64_t edit_epoch() const { return edit_epoch_; }
    std::uint64_t focus_epoch() const { return focus_epoch_; }
    bool start_pending() const { return start_pending_; }
    bool commit_pending() const { return commit_pending_; }
    bool has_committed_text() const { return committed_text_; }
    bool internal_end() const { return internal_end_; }
	HRESULT insertPlaceholder(TfClientId clientId);
	HRESULT updatePlaceholder(TfClientId clientId);
	HRESULT getTextBound(TfClientId clientId, CRect& rc);

	void delPlaceholder(TfEditCookie ec);
	BOOL exsitComposition() const;
	void setComposition(CComPtr<ITfComposition> pComposition);
	bool clearTerminatedComposition(ITfComposition* pComposition);
	ITfComposition* getComposition() const;
	void setContext(CComPtr<ITfContext> pContext);
	ITfContext* getContext() const;
	void processbAboutToShow(WPARAM wParam);
	void setAboutToShow(bool bAboutToShow);
	bool getAboutToShow() const;
	void setPositionEnhanced(bool isPositionEnhanced);
	bool getPositionEnhanced() const;
	CString& getAllInput();
	std::vector<CString>& getCandInput();
	int isNeedFilter(const CString& text) const;
	void setProcessName(const CString& process_name, int placeholder_scheme);
	int getScheme() const;
	void setKeepPlaceholder(bool bKeepPlaceholder);
	const CString& getLastTwoChars() const;
	void setLastTwoChars(const CString& value);
	bool getKeepPlaceholder() const;
	const WCHAR* getCurrentPlaceholder() const;
	void clearAccumulatedPinyin();
	void updateAccumulatedPinyin(const CString& unfinshedPinyin, TfClientId clientId);
	ITfCompositionSink* getCompositionSink() const;
	const CString& getAccumulatedPinyin() const;
private:
	HRESULT doInsertText(TfClientId clientId, const CString& text, TfAnchor tfType);
	HRESULT doSetCursorPos(TfClientId clientId, int pos);
	HRESULT doUpdateComposition(TfClientId clientId, const CString& text);
	void dpiAdapt(CRect& rc);

private:
    friend class CEditSession;
    void record_written_text(const CString& text);
    DWORD owner_thread_ = ::GetCurrentThreadId();
    std::uint64_t edit_epoch_ = 1, focus_epoch_ = 1;
    std::uint64_t request_sequence_ = 1;
    bool focused_ = true, retired_ = false, start_pending_ = false;
    bool commit_pending_ = false, end_pending_ = false, committed_text_ = false, internal_end_ = false;
    std::function<bool(ITfContext*)> focus_validator_;
	int m_scheme = 0;
	ITfCompositionSink* m_pCompositionSink = nullptr;
	WCHAR m_choosenText[MAX_BUFFER_SIZE + 1] = { 0 };

	CTextService* m_pTextService = nullptr;
	CComPtr<ITfComposition> m_pComposition = nullptr;
	CComPtr<ITfContext> m_pContext = nullptr;
	bool m_bAboutToShow = false; // 是否即将显示界面
	bool m_bPositionEnhanced = false; // 是否启用形如coral的进一步位置计算(不能普遍启用，否则没有问题的应用程序会出现位置错误)
	bool m_bKeepPlaceholder = false;

	//利用候选框传入的东西
	std::vector<CString> m_candInput;
	//用输入法完全输入的东西
	CString m_allInput;
	CString m_processName;
	// 累积的拼音字符串，用于PINYINHOLDER模式
	CString m_accumulatedPinyin;
	// 数字间符号转换：记录最近输入的两个字符
	CString m_lastTwoChars;
};

#endif // EDITSESSION_H

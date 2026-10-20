#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_CONTEXTSTORE_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_CONTEXTSTORE_H_INCLUDED

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "keydecision.h"
#include "sinksubscription.h"

class CEditMgr;
class CTextService;
class CandidateController;
class CCandidateListUIElement;

namespace coral::tsf
{

struct EngineSessionHandle
{
	std::uint64_t value = 0;

	bool valid() const { return value != 0; }
	void reset() { value = 0; }
};

struct ContextToken
{
	std::uint64_t serviceInstanceId = 0;
	std::uint64_t contextId = 0;
	std::uint64_t generation = 0;
};

struct RequestToken
{
	ContextToken context;
	std::uint64_t request_id = 0;
};

struct CandidateSnapshot
{
	std::uint64_t version = 0;
	std::vector<CString> candidates;
	std::vector<std::uint64_t> candidateIds;
	UINT selection = 0;
	UINT pageSize = 1;
	bool expanded = false;
	bool hasMore = false;
	RECT anchor{};
	bool visible = false;
};

struct ContextKeyState
{
	struct PendingDecision
	{
		bool valid = false;
		std::uint32_t virtualKey = 0;
		bool keyDown = false;
		KeyDecision shortcut;
		KeyHandlingDecision handling;
	};

	std::uint8_t modifierMask = 0;
	bool preInput = false;
	bool preQuote = false;
	bool shiftOrCtrlDown = false;
	bool enterDown = false;
	bool handledMultiKey = false;
	bool ctrlSpaceHeld = false;
	PendingDecision pendingDecision;
};

struct ContextInputState
{
	bool needSendLeftKey = false;
	bool fullWidthInChineseMode = false;
	bool preQuoteSingle = false;
	bool preQuoteDouble = false;
	bool compositionEnded = false;
	bool layoutChanged = false;
	bool englishPunctuationMode = false;
	bool candidatePresentationRequested = false;
	// 0 跟随配置，1 强制英文标点，2 强制中文标点。
	int punctuationLanguageMode = 0;
	int layoutChangeCount = 0;
	int layoutChangeIndex = 0;
};

class ContextSnapshot final
{
	public:
	ContextSnapshot(
		ContextToken token,
		EngineSessionHandle engineSession,
		std::uint64_t candidateVersion,
		bool focused,
		bool composing)
		: m_token(token)
		, m_engineSession(engineSession)
		, m_candidateVersion(candidateVersion)
		, m_focused(focused)
		, m_composing(composing)
	{
	}

	const ContextToken& token() const { return m_token; }
	EngineSessionHandle engineSession() const { return m_engineSession; }
	std::uint64_t candidateVersion() const { return m_candidateVersion; }
	bool focused() const { return m_focused; }
	bool composing() const { return m_composing; }

	private:
	const ContextToken m_token;
	const EngineSessionHandle m_engineSession;
	const std::uint64_t m_candidateVersion;
	const bool m_focused;
	const bool m_composing;
};

enum class ContextLifecycle
{
	Active,
	Closing,
};

class ContextState final
{
public:
	~ContextState();
	ContextState(const ContextState&) = delete;
	ContextState& operator=(const ContextState&) = delete;

	const ContextToken& token() const { return m_token; }
	ITfContext* context() const { return m_context; }
	ContextLifecycle lifecycle() const { return m_lifecycle; }
	bool focused() const { return m_focused; }
	EngineSessionHandle engineSession() const { return m_engineSession; }
	void setEngineSession(EngineSessionHandle handle) { m_engineSession = handle; }
	const CandidateSnapshot& candidates() const { return m_candidates; }
	void setCandidates(CandidateSnapshot snapshot) { m_candidates = std::move(snapshot); }
	CEditMgr& compositionController() const { return *m_compositionController; }
	CandidateController& candidateController() const { return *m_candidateController; }
	CCandidateListUIElement& candidateUIElement() const { return *m_candidateUIElement; }
	ContextKeyState& keyState() { return m_keyState; }
	const ContextKeyState& keyState() const { return m_keyState; }
	ContextInputState& inputState() { return m_inputState; }
	const ContextInputState& inputState() const { return m_inputState; }
	ContextSnapshot snapshot() const;

private:
	friend class ContextStore;
	ContextState(
		CComPtr<IUnknown> identity,
		ITfContext* context,
		ContextToken token,
		std::shared_ptr<CEditMgr> compositionController,
		std::unique_ptr<CandidateController> candidateController,
		std::unique_ptr<CCandidateListUIElement> candidateUIElement);
	HRESULT adviseSinks(CTextService* owner);

	CComPtr<IUnknown> m_identity;
	CComPtr<ITfContext> m_context;
	ContextToken m_token;
	EngineSessionHandle m_engineSession;
	CandidateSnapshot m_candidates;
	std::shared_ptr<CEditMgr> m_compositionController;
	std::unique_ptr<CandidateController> m_candidateController;
	std::unique_ptr<CCandidateListUIElement> m_candidateUIElement;
	ContextKeyState m_keyState;
	ContextInputState m_inputState;
	SinkSubscription m_editSubscription;
	SinkSubscription m_layoutSubscription;
	ContextLifecycle m_lifecycle = ContextLifecycle::Active;
	bool m_focused = false;
};

// 线程亲和的存储。COM 指针绝不离开 TSF 所有者线程；工作线程与
// IPC 回调使用 ContextToken/RequestToken 并在应用数据前重新校验。
class ContextStore final
{
public:
	explicit ContextStore(CTextService* owner);
	~ContextStore();
	ContextStore(const ContextStore&) = delete;
	ContextStore& operator=(const ContextStore&) = delete;

	std::uint64_t serviceInstanceId() const { return m_serviceInstanceId; }
	HRESULT ensure(ITfContext* context, ContextState** state = nullptr);
	ContextState* find(ITfContext* context) const;
	ContextState* find(const ContextToken& token) const;
	ContextState* findByComposition(ITfComposition* composition) const;
	ContextState* focused() const;
	HRESULT setFocused(ITfContext* context);
	HRESULT close(ITfContext* context);
	HRESULT closeDocumentManager(ITfDocumentMgr* documentManager);
	std::vector<EngineSessionHandle> engineSessions() const;
	std::vector<EngineSessionHandle> engineSessionsForDocument(ITfDocumentMgr* documentManager) const;
	void clear();

	RequestToken nextRequest(ITfContext* context);
	bool isCurrent(const ContextToken& token) const;
	bool isCurrent(const RequestToken& token) const;

private:
	bool isOwnerThread() const;
	HRESULT identityOf(ITfContext* context, CComPtr<IUnknown>& identity) const;

	DWORD m_ownerThreadId = 0;
	CTextService* m_owner = nullptr;
	std::uint64_t m_serviceInstanceId = 0;
	std::uint64_t m_nextContextId = 1;
	std::uint64_t m_nextRequestId = 1;
	std::unordered_map<IUnknown*, std::unique_ptr<ContextState>> m_states;
};

} // 命名空间 coral::tsf 结束

#endif // CONTEXTSTORE_H

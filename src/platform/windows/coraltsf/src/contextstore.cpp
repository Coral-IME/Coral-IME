#include "pch.h"
#include "contextstore.h"
#include "candidatecontroller.h"
#include "candidatelistuielement.h"
#include "editsession.h"
#include "textservice.h"

#include <atomic>

namespace coral::tsf
{
namespace
{
std::atomic<std::uint64_t> g_nextServiceInstanceId{ 1 };

void LogContextEvent(const wchar_t* event, const ContextToken& token)
{
	LOG_INFO_W(L"[tsf_context] event=" + std::wstring(event) +
		L" service_id=" + std::to_wstring(token.serviceInstanceId) +
		L" context_id=" + std::to_wstring(token.contextId) +
		L" generation=" + std::to_wstring(token.generation));
}
}

ContextState::ContextState(
	CComPtr<IUnknown> identity,
	ITfContext* context,
	ContextToken token,
	std::shared_ptr<CEditMgr> compositionController,
	std::unique_ptr<CandidateController> candidateController,
	std::unique_ptr<CCandidateListUIElement> candidateUIElement)
	: m_identity(std::move(identity))
	, m_context(context)
	, m_token(token)
	, m_compositionController(std::move(compositionController))
	, m_candidateController(std::move(candidateController))
	, m_candidateUIElement(std::move(candidateUIElement))
{
}

ContextState::~ContextState() = default;

HRESULT ContextState::adviseSinks(CTextService* owner)
{
	if (!owner)
		return E_INVALIDARG;
	HRESULT hr = m_editSubscription.advise(
		m_context, IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(owner));
	if (FAILED(hr))
		return hr;
	hr = m_layoutSubscription.advise(
		m_context, IID_ITfTextLayoutSink, static_cast<ITfTextLayoutSink*>(owner));
	if (FAILED(hr))
		m_editSubscription.reset();
	return hr;
}

ContextSnapshot ContextState::snapshot() const
{
	return ContextSnapshot(
		m_token,
		m_engineSession,
		m_candidates.version,
		m_focused,
		m_compositionController && m_compositionController->exsitComposition());
}

ContextStore::ContextStore(CTextService* owner)
	: m_ownerThreadId(::GetCurrentThreadId())
	, m_owner(owner)
	, m_serviceInstanceId(g_nextServiceInstanceId.fetch_add(1, std::memory_order_relaxed))
{
}

ContextStore::~ContextStore()
{
	clear();
}

bool ContextStore::isOwnerThread() const
{
	return m_ownerThreadId == ::GetCurrentThreadId();
}

HRESULT ContextStore::identityOf(ITfContext* context, CComPtr<IUnknown>& identity) const
{
	identity.Release();
	if (!context)
		return E_INVALIDARG;
	return context->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity));
}

HRESULT ContextStore::ensure(ITfContext* context, ContextState** state)
{
	if (state)
		*state = nullptr;
	if (!isOwnerThread())
		return RPC_E_WRONG_THREAD;

	CComPtr<IUnknown> identity;
	HRESULT hr = identityOf(context, identity);
	if (FAILED(hr))
		return hr;

	auto existing = m_states.find(identity.p);
	if (existing != m_states.end())
	{
		if (state)
			*state = existing->second.get();
		return S_FALSE;
	}

	ContextToken token{ m_serviceInstanceId, m_nextContextId++, 1 };
	IUnknown* key = identity.p;
	auto controller = std::make_shared<CEditMgr>(m_owner);
	auto candidateController = std::make_unique<CandidateController>();
	auto candidateUIElement = std::make_unique<CCandidateListUIElement>(m_owner);
	candidateUIElement->init();
	auto value = std::unique_ptr<ContextState>(new ContextState(
		std::move(identity), context, token, std::move(controller),
		std::move(candidateController), std::move(candidateUIElement)));
	ContextState* raw = value.get();
	const HRESULT adviseHr = raw->adviseSinks(m_owner);
	if (FAILED(adviseHr))
		return adviseHr;
	m_states.emplace(key, std::move(value));
	LogContextEvent(L"created", token);
	if (state)
		*state = raw;
	return S_OK;
}

ContextState* ContextStore::find(ITfContext* context) const
{
	if (!isOwnerThread())
		return nullptr;
	CComPtr<IUnknown> identity;
	if (FAILED(identityOf(context, identity)))
		return nullptr;
	auto it = m_states.find(identity.p);
	return it == m_states.end() ? nullptr : it->second.get();
}

ContextState* ContextStore::find(const ContextToken& token) const
{
	if (!isOwnerThread() || token.serviceInstanceId != m_serviceInstanceId ||
		token.contextId == 0)
	{
		return nullptr;
	}
	for (const auto& entry : m_states)
	{
		const ContextToken& current = entry.second->token();
		if (current.contextId == token.contextId && current.generation == token.generation &&
			entry.second->lifecycle() == ContextLifecycle::Active)
		{
			return entry.second.get();
		}
	}
	return nullptr;
}

ContextState* ContextStore::findByComposition(ITfComposition* composition) const
{
	if (!isOwnerThread() || !composition)
		return nullptr;
	for (const auto& entry : m_states)
	{
		if (entry.second->compositionController().getComposition() == composition)
			return entry.second.get();
	}
	return nullptr;
}

ContextState* ContextStore::focused() const
{
	if (!isOwnerThread())
		return nullptr;
	for (const auto& entry : m_states)
	{
		if (entry.second->focused())
			return entry.second.get();
	}
	return nullptr;
}

HRESULT ContextStore::setFocused(ITfContext* context)
{
	if (!isOwnerThread())
		return RPC_E_WRONG_THREAD;
	for (auto& entry : m_states)
		entry.second->m_focused = false;
	if (!context)
		return S_OK;

	ContextState* state = nullptr;
	const HRESULT hr = ensure(context, &state);
	if (FAILED(hr))
		return hr;
	state->m_focused = true;
	return S_OK;
}

HRESULT ContextStore::close(ITfContext* context)
{
	if (!isOwnerThread())
		return RPC_E_WRONG_THREAD;
	CComPtr<IUnknown> identity;
	HRESULT hr = identityOf(context, identity);
	if (FAILED(hr))
		return hr;
	auto it = m_states.find(identity.p);
	if (it == m_states.end())
		return S_FALSE;

	if (m_owner)
		m_owner->prepareContextClose(*it->second);
	it->second->m_lifecycle = ContextLifecycle::Closing;
	++it->second->m_token.generation;
	LogContextEvent(L"closed", it->second->m_token);
	it->second->m_engineSession.reset();
	m_states.erase(it);
	return S_OK;
}

HRESULT ContextStore::closeDocumentManager(ITfDocumentMgr* documentManager)
{
	if (!documentManager)
		return E_INVALIDARG;
	if (!isOwnerThread())
		return RPC_E_WRONG_THREAD;

	for (auto it = m_states.begin(); it != m_states.end();)
	{
		CComPtr<ITfDocumentMgr> owner;
		if (SUCCEEDED(it->second->m_context->GetDocumentMgr(&owner)) && owner == documentManager)
		{
			if (m_owner)
				m_owner->prepareContextClose(*it->second);
			it->second->m_lifecycle = ContextLifecycle::Closing;
			++it->second->m_token.generation;
			LogContextEvent(L"document_closed", it->second->m_token);
			it->second->m_engineSession.reset();
			it = m_states.erase(it);
		}
		else
		{
			++it;
		}
	}
	return S_OK;
}

void ContextStore::clear()
{
	for (auto& entry : m_states)
	{
		if (m_owner)
			m_owner->prepareContextClose(*entry.second);
		entry.second->m_lifecycle = ContextLifecycle::Closing;
		++entry.second->m_token.generation;
		LogContextEvent(L"service_cleared", entry.second->m_token);
		entry.second->m_engineSession.reset();
	}
	m_states.clear();
}

std::vector<EngineSessionHandle> ContextStore::engineSessions() const
{
	std::vector<EngineSessionHandle> result;
	result.reserve(m_states.size());
	for (const auto& entry : m_states)
	{
		const EngineSessionHandle handle = entry.second->engineSession();
		if (handle.valid())
			result.push_back(handle);
	}
	return result;
}

std::vector<EngineSessionHandle> ContextStore::engineSessionsForDocument(
	ITfDocumentMgr* documentManager) const
{
	std::vector<EngineSessionHandle> result;
	if (!documentManager || !isOwnerThread())
		return result;
	for (const auto& entry : m_states)
	{
		CComPtr<ITfDocumentMgr> owner;
		if (SUCCEEDED(entry.second->context()->GetDocumentMgr(&owner)) && owner == documentManager)
		{
			const EngineSessionHandle handle = entry.second->engineSession();
			if (handle.valid())
				result.push_back(handle);
		}
	}
	return result;
}

RequestToken ContextStore::nextRequest(ITfContext* context)
{
	RequestToken result;
	if (!isOwnerThread())
		return result;
	ContextState* state = nullptr;
	if (FAILED(ensure(context, &state)) || !state)
		return result;
	result.context = state->token();
	result.request_id = m_nextRequestId++;
	return result;
}

bool ContextStore::isCurrent(const ContextToken& token) const
{
	if (!isOwnerThread() || token.serviceInstanceId != m_serviceInstanceId)
		return false;
	for (const auto& entry : m_states)
	{
		const ContextToken& current = entry.second->token();
		if (current.contextId == token.contextId)
			return current.generation == token.generation &&
				entry.second->lifecycle() == ContextLifecycle::Active;
	}
	return false;
}

bool ContextStore::isCurrent(const RequestToken& token) const
{
	return token.request_id != 0 && isCurrent(token.context);
}

} // 命名空间 coral::tsf 结束

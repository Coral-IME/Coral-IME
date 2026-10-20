#include "pch.h"
#include "tsfthreadcontext.h"

namespace coral::tsf
{

TsfThreadContext::TsfThreadContext(CTextService* owner)
	: m_ownerThreadId(::GetCurrentThreadId())
	, m_contexts(owner)
{
}

TsfThreadContext::~TsfThreadContext()
{
	deactivate();
}

HRESULT TsfThreadContext::activate(HINSTANCE instance)
{
	if (!isOwnerThread())
		return RPC_E_WRONG_THREAD;
	return m_dispatcher.initialize(&m_contexts, instance);
}

void TsfThreadContext::deactivate()
{
	if (!isOwnerThread())
		return;
	m_dispatcher.shutdown();
	m_contexts.clear();
}

} // 命名空间 coral::tsf 结束

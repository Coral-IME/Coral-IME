#pragma once

#include "contextstore.h"
#include "ownerthreaddispatcher.h"

class CTextService;

namespace coral::tsf
{

class TsfThreadContext final
{
public:
	explicit TsfThreadContext(CTextService* owner);
	~TsfThreadContext();
	TsfThreadContext(const TsfThreadContext&) = delete;
	TsfThreadContext& operator=(const TsfThreadContext&) = delete;

	HRESULT activate(HINSTANCE instance);
	void deactivate();
	bool isOwnerThread() const { return m_ownerThreadId == ::GetCurrentThreadId(); }
	ContextStore& contexts() { return m_contexts; }
	const ContextStore& contexts() const { return m_contexts; }
	OwnerThreadDispatcher& dispatcher() { return m_dispatcher; }

private:
	DWORD m_ownerThreadId = 0;
	ContextStore m_contexts;
	OwnerThreadDispatcher m_dispatcher;
};

} // 命名空间 coral::tsf 结束

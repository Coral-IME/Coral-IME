#include <atlbase.h>
#include <msctf.h>

#include "sinksubscription.h"

#include <atomic>
#include <iostream>

namespace
{
class FakeSink final : public IUnknown
{
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
	{
		if (!object)
			return E_POINTER;
		*object = nullptr;
		if (iid == IID_IUnknown)
		{
			*object = static_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG refs = --m_refs;
		if (!refs)
			delete this;
		return refs;
	}
private:
	std::atomic<ULONG> m_refs{ 1 };
};

class FakeSource final : public ITfSource
{
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
	{
		if (!object)
			return E_POINTER;
		*object = nullptr;
		if (iid == IID_IUnknown || iid == IID_ITfSource)
		{
			*object = static_cast<ITfSource*>(this);
			AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG refs = --m_refs;
		if (!refs)
			delete this;
		return refs;
	}
	HRESULT STDMETHODCALLTYPE AdviseSink(REFIID, IUnknown* sink, DWORD* cookie) override
	{
		if (!sink || !cookie || m_sink)
			return E_INVALIDARG;
		m_sink = sink;
		*cookie = 17;
		++adviseCount;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD cookie) override
	{
		if (cookie != 17 || !m_sink)
			return CONNECT_E_NOCONNECTION;
		m_sink.Release();
		++unadviseCount;
		return S_OK;
	}

	int adviseCount = 0;
	int unadviseCount = 0;

private:
	std::atomic<ULONG> m_refs{ 1 };
	CComPtr<IUnknown> m_sink;
};
}

int main()
{
	auto* source = new FakeSource();
	auto* sink = new FakeSink();
	{
		coral::tsf::SinkSubscription subscription;
		if (FAILED(subscription.advise(source, IID_ITfThreadFocusSink, sink)) ||
			!subscription.active() || source->adviseCount != 1)
		{
			std::cerr << "advise contract failed\n";
			return 1;
		}
		subscription.reset();
		subscription.reset();
		if (subscription.active() || source->unadviseCount != 1)
		{
			std::cerr << "idempotent unadvise contract failed\n";
			return 1;
		}
	}
	sink->Release();
	source->Release();
	return 0;
}

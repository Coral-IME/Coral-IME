#pragma once

namespace coral::tsf
{

// 持有一个 ITfSource 订阅。reset() 是幂等的，析构函数
// 也会调用它，因此 Activate 部分失败不会泄漏已通告的接收器。
class SinkSubscription final
{
public:
	SinkSubscription() = default;
	~SinkSubscription() { reset(); }
	SinkSubscription(const SinkSubscription&) = delete;
	SinkSubscription& operator=(const SinkSubscription&) = delete;

	HRESULT advise(IUnknown* sourceObject, REFIID sinkInterface, IUnknown* sink)
	{
		reset();
		if (!sourceObject || !sink)
			return E_INVALIDARG;
		CComPtr<ITfSource> source;
		HRESULT hr = sourceObject->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source));
		if (FAILED(hr))
			return hr;
		DWORD cookie = TF_INVALID_COOKIE;
		hr = source->AdviseSink(sinkInterface, sink, &cookie);
		if (FAILED(hr))
			return hr;
		m_source = std::move(source);
		m_cookie = cookie;
		return S_OK;
	}

	void reset()
	{
		CComPtr<ITfSource> source;
		source.Attach(m_source.Detach());
		const DWORD cookie = m_cookie;
		m_cookie = TF_INVALID_COOKIE;
		if (source && cookie != TF_INVALID_COOKIE)
			source->UnadviseSink(cookie);
	}

	bool active() const { return m_source && m_cookie != TF_INVALID_COOKIE; }

private:
	CComPtr<ITfSource> m_source;
	DWORD m_cookie = TF_INVALID_COOKIE;
};

} // 命名空间 coral::tsf 结束

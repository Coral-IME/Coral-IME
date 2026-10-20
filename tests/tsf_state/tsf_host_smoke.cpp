#include <atlbase.h>
#include <msctf.h>
#include <objbase.h>

int wmain()
{
	HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hr))
		return 1;

	CComPtr<ITfThreadMgr> threadManager;
	hr = ::CoCreateInstance(
		CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
		IID_ITfThreadMgr, reinterpret_cast<void**>(&threadManager));
	TfClientId clientId = TF_CLIENTID_NULL;
	if (SUCCEEDED(hr))
		hr = threadManager->Activate(&clientId);
	CComPtr<ITfDocumentMgr> documentManager;
	if (SUCCEEDED(hr))
		hr = threadManager->CreateDocumentMgr(&documentManager);
	if (SUCCEEDED(hr))
		hr = threadManager->SetFocus(documentManager);
	if (threadManager && clientId != TF_CLIENTID_NULL)
	{
		threadManager->SetFocus(nullptr);
		threadManager->Deactivate();
	}
	threadManager.Release();
	::CoUninitialize();
	return SUCCEEDED(hr) ? 0 : 2;
}

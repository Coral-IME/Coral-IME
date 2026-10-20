#include "pch.h"
#include "compartment.h"
#include "textservice.h"

STDAPI CCompartmentEventSink::OnChange(REFGUID guidCompartment)
{
	return m_callback(guidCompartment);
}

CCompartmentEventSink::CCompartmentEventSink(CTextService* pTextService)
	: m_pTextService(pTextService)
	, m_cookie(TF_INVALID_COOKIE)
	, m_callback(NULL)
{
}

CCompartmentEventSink::~CCompartmentEventSink()
{
}

BOOL CCompartmentEventSink::init()
{
	m_callback = std::bind(&CCompartmentEventSink::handleCompartment, this, std::placeholders::_1);
	return SUCCEEDED(advise((IUnknown*)m_pTextService->getThreadMgr(), GUID_COMPARTMENT_KEYBOARD_OPENCLOSE));
}

void CCompartmentEventSink::unInit()
{
	unAdvise();
}

BOOL CCompartmentEventSink::isKeyboardDisable()
{
	BOOL isDisabled = FALSE;
	CComPtr<ITfDocumentMgr> pDocMgr;
	CComPtr<ITfContext> pContext;
	CComPtr<ITfCompartmentMgr> pComMgr;
	LOG_ERROR_MSG_W(m_pTextService->getThreadMgr()->GetFocus(&pDocMgr) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::isKeyboardDisable 1");
	LOG_ERROR_MSG_W(pDocMgr->GetTop(&pContext) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::isKeyboardDisable 2");
	LOG_ERROR_MSG_W(pContext->QueryInterface(&pComMgr) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::isKeyboardDisable 3");
	{
		CComPtr<ITfCompartment> pComDisable;
		LOG_ERROR_MSG_W(pComMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, &pComDisable) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::isKeyboardDisable 4");
		VARIANT var;
		if (pComDisable->GetValue(&var) == S_OK && var.vt == VT_I4)
		{
			isDisabled = (BOOL)var.lVal;
		}
		if (!isDisabled)
		{
			CComPtr<ITfCompartment> pComEmpty;
			LOG_ERROR_MSG_W(pComMgr->GetCompartment(GUID_COMPARTMENT_EMPTYCONTEXT, &pComEmpty) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::isKeyboardDisable 5");
			if (pComEmpty->GetValue(&var) == S_OK && var.vt == VT_I4)
			{
				isDisabled = (BOOL)var.lVal;
			}
		}
	}
Exit0:
	return isDisabled;
}

BOOL CCompartmentEventSink::isKeyboardOpen()
{
	BOOL isOpen = FALSE;
	CComPtr<ITfCompartmentMgr> pComMgr;
	LOG_ERROR_MSG_W(m_pTextService->getThreadMgr()->QueryInterface(&pComMgr) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::isKeyboardOpen 1");
	{
		CComPtr<ITfCompartment> pComOpen;
		LOG_ERROR_MSG_W(pComMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &pComOpen) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::isKeyboardOpen 2");
		VARIANT var;
		if (pComOpen->GetValue(&var) != S_OK || var.vt != VT_I4)
			return FALSE;
		isOpen = (BOOL)var.lVal;
	}
Exit0:
	return isOpen;
}

HRESULT CCompartmentEventSink::setKeyboardOpen(BOOL isOpen)
{
	HRESULT hr = E_FAIL;
	CComPtr<ITfCompartmentMgr> pComMgr;
	LOG_ERROR_MSG_W(m_pTextService->getThreadMgr()->QueryInterface(&pComMgr) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::setKeyboardOpen 1");
	{
		CComPtr<ITfCompartment> pComOpen;
		LOG_ERROR_MSG_W(pComMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &pComOpen) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::setKeyboardOpen 2");
		VARIANT var;
		var.vt = VT_I4;
		var.lVal = isOpen;
		LOG_ERROR_MSG_W(pComOpen->SetValue(m_pTextService->getClientId(), &var) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::setKeyboardOpen 3");
	}
	hr = S_OK;
Exit0:
	return hr;
}

// 预留接口，用于存储和管理全局或上下文特定状态数据的机制
// 当隔间的值通过 ITfCompartment::SetValue 方法被修改时，TSF 管理器就会调用所有已注册的 ITfCompartmentEventSink 实例的 OnChange 方法。
HRESULT CCompartmentEventSink::updateCompartmentString(const GUID& rguidCompartment, const WCHAR* pszNewString)
{
	if (!pszNewString)
		return E_INVALIDARG;

	HRESULT hr = E_FAIL;
	CComPtr<ITfCompartmentMgr> pComMgr;
	CComPtr<ITfRange> pRange;
	CHECK_ERROR_MSG(m_pTextService->getThreadMgr()->QueryInterface(&pComMgr) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateCompartmentString 1");
	{
		CComPtr<ITfCompartment> pCompartment;
		CHECK_ERROR_MSG(pComMgr->GetCompartment(rguidCompartment, &pCompartment) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateCompartmentString 2");

		VARIANT var;
		VariantInit(&var);

		var.vt = VT_BSTR;
		var.bstrVal = SysAllocString(pszNewString); // 将新的字符串分配为 BSTR
		if (!var.bstrVal)
			return E_OUTOFMEMORY; // 内存分配失败

		CHECK_ERROR_MSG(pCompartment->SetValue(m_pTextService->getClientId(), &var) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateCompartmentString 3");
		VariantClear(&var); // 清理 VARIANT，释放 BSTR 内存
	}
	hr = S_OK;
Exit0:
	return hr;
}

HRESULT CCompartmentEventSink::updateFullShapeMode(BOOL bFullShape)
{
	HRESULT hr = E_FAIL;
	CComPtr<ITfCompartmentMgr> pComMgr;
	CHECK_ERROR_MSG(m_pTextService->getThreadMgr()->QueryInterface(&pComMgr) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::UpdateFullShapeMode 1");
	{
		CComPtr<ITfCompartment> pCompartment;
		CHECK_ERROR_MSG(pComMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &pCompartment) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::UpdateFullShapeMode 2");

		VARIANT var;
		VariantInit(&var);
		hr = pCompartment->GetValue(&var);
		CHECK_ERROR_MSG(hr == S_OK || hr == S_FALSE, m_pTextService->getProcessName(), L"CCompartmentEventSink::UpdateFullShapeMode 3");

		var.vt = VT_I4; // 设置为 DWORD 类型
		if (bFullShape)
		{
			var.lVal |= TF_CONVERSIONMODE_FULLSHAPE; // 全角模式
		}
		else
		{
			var.lVal &= ~TF_CONVERSIONMODE_FULLSHAPE; // 其他非全角模式
		}

		CHECK_ERROR_MSG(pCompartment->SetValue(m_pTextService->getClientId(), &var) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateFullShapeMode 4");
		VariantClear(&var); 
	}
	hr = S_OK;
Exit0:
	return hr;
}

HRESULT CCompartmentEventSink::updateChineseMode(BOOL bChineseMode)
{
	HRESULT hr = E_FAIL;
	CComPtr<ITfCompartmentMgr> pComMgr;
	CHECK_ERROR_MSG(m_pTextService->getThreadMgr()->QueryInterface(&pComMgr) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateChineseMode 1");
	{
		CComPtr<ITfCompartment> pCompartment;
		CHECK_ERROR_MSG(pComMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &pCompartment) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateChineseMode 2");

		VARIANT var;
		VariantInit(&var);
		hr = pCompartment->GetValue(&var);
		CHECK_ERROR_MSG(hr == S_OK || hr == S_FALSE, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateChineseMode 3");

		var.vt = VT_I4; // 设置为 DWORD 类型
		if (bChineseMode)
		{
			var.lVal |= TF_CONVERSIONMODE_NATIVE;
		} 
		else
		{
			var.lVal &= ~TF_CONVERSIONMODE_NATIVE;
		}

		CHECK_ERROR_MSG(pCompartment->SetValue(m_pTextService->getClientId(), &var) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateChineseMode 4");
		VariantClear(&var); 
	}
	hr = S_OK;
Exit0:
	return hr;
}

HRESULT CCompartmentEventSink::updateChinesePunctuationMode(BOOL bChinesePunctuationMode)
{
	HRESULT hr = E_FAIL;
	CComPtr<ITfCompartmentMgr> pComMgr;
	CHECK_ERROR_MSG(m_pTextService->getThreadMgr()->QueryInterface(&pComMgr) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateChinesePunctuationMode 1");
	{
		CComPtr<ITfCompartment> pCompartment;
		CHECK_ERROR_MSG(pComMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &pCompartment) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateChinesePunctuationMode 2");

		VARIANT var;
		VariantInit(&var);
		hr = pCompartment->GetValue(&var);
		CHECK_ERROR_MSG(hr == S_OK || hr == S_FALSE, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateChinesePunctuationMode 3");

		var.vt = VT_I4; 
		if (bChinesePunctuationMode)
		{
			var.lVal |= TF_CONVERSIONMODE_SYMBOL;
		}
		else
		{
			var.lVal &= ~TF_CONVERSIONMODE_SYMBOL;
		}

		CHECK_ERROR_MSG(pCompartment->SetValue(m_pTextService->getClientId(), &var) == S_OK, m_pTextService->getProcessName(), L"CCompartmentEventSink::updateChinesePunctuationMode 4");
		VariantClear(&var);
	}
	hr = S_OK;
Exit0:
	return hr;
}

HRESULT CCompartmentEventSink::handleCompartment(REFGUID guidCompartment)
{
	if (::IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE))
	{
		BOOL isOpen = isKeyboardOpen();
		m_pTextService->m_pLanguageProfile->enableLanguageBar(isOpen);
	}
	return S_OK;
}

HRESULT CCompartmentEventSink::advise(const CComPtr<IUnknown>& punk, REFGUID guidCompartment)
{
	HRESULT hr = E_FAIL;
	CComPtr<ITfCompartmentMgr> pCompartmentMgr;
	CComPtr<ITfSource> pSource;
	{
		hr = punk->QueryInterface(&pCompartmentMgr);
		LOG_ERROR_MSG_W(SUCCEEDED(hr), m_pTextService->getProcessName(), L"CCompartmentEventSink::_advise 1");
		hr = pCompartmentMgr->GetCompartment(guidCompartment, &m_compartment);
		LOG_ERROR_MSG_W(SUCCEEDED(hr), m_pTextService->getProcessName(), L"CCompartmentEventSink::_advise 2");
		hr = m_compartment->QueryInterface(&pSource);
		LOG_ERROR_MSG_W(SUCCEEDED(hr), m_pTextService->getProcessName(), L"CCompartmentEventSink::_advise 3");
		hr = pSource->AdviseSink(IID_ITfCompartmentEventSink, (ITfCompartmentEventSink*)m_pTextService, &m_cookie);
		LOG_ERROR_MSG_W(SUCCEEDED(hr), m_pTextService->getProcessName(), L"CCompartmentEventSink::_advise 4");
	}
	hr = S_OK;
Exit0:
	return hr;
}

HRESULT CCompartmentEventSink::unAdvise()
{
	if (!m_compartment)
		return S_OK;

	CComPtr<ITfSource> pSource;
	HRESULT hr = m_compartment->QueryInterface(&pSource);
	if (SUCCEEDED(hr))
	{
		hr = pSource->UnadviseSink(m_cookie);
	}

	m_compartment = NULL;
	m_cookie = TF_INVALID_COOKIE;

	return hr;
}

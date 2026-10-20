#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_DISPLAYATTRIBUTE_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_DISPLAYATTRIBUTE_H_INCLUDED

// 本文字服务的显示属性提供方发布的 GUID。
// 编辑会话在赋值 GUID_PROP_ATTRIBUTE 前先把它转换为 TfGuidAtom。
extern const GUID GUID_CoralPinyin_DisplayAttr;

// 提供组串期间拼音下方的点状下划线样式，
// 与微软拼音和微信输入法的视觉风格保持一致。
//
// TSF 通过 GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER 发现提供方
// （已在 register.cpp 中为 CLSID_TextService 注册），并自动把返回的
// TF_DISPLAYATTRIBUTE 应用到组串范围。

// ---- CDisplayAttributeInfo ----
// 持有一个具体的显示属性（拼音输入的点状下划线）。
class ATL_NO_VTABLE CDisplayAttributeInfo
	: public CComObjectRootEx<CComSingleThreadModel>
	, public ITfDisplayAttributeInfo
{
public:
	DECLARE_NO_REGISTRY()

	BEGIN_COM_MAP(CDisplayAttributeInfo)
		COM_INTERFACE_ENTRY(ITfDisplayAttributeInfo)
	END_COM_MAP()

	// ITfDisplayAttributeInfo
	STDMETHODIMP GetGUID(GUID* pguid) override;
	STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
	STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override;
	STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) override;
	STDMETHODIMP Reset() override;
};

// ---- CEnumDisplayAttributeInfo ----
// 由 EnumDisplayAttributeInfo 返回的简单单项枚举器。
class ATL_NO_VTABLE CEnumDisplayAttributeInfo
	: public CComObjectRootEx<CComSingleThreadModel>
	, public IEnumTfDisplayAttributeInfo
{
public:
	DECLARE_NO_REGISTRY()

	BEGIN_COM_MAP(CEnumDisplayAttributeInfo)
		COM_INTERFACE_ENTRY(IEnumTfDisplayAttributeInfo)
	END_COM_MAP()

	HRESULT Init(ITfDisplayAttributeInfo* pInfo);

	// IEnumTfDisplayAttributeInfo
	STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override;
	STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) override;
	STDMETHODIMP Reset() override;
	STDMETHODIMP Skip(ULONG ulCount) override;

private:
	CComPtr<ITfDisplayAttributeInfo> m_pInfo;
	ULONG m_ulFetched = 0; // 0 = 尚未消费，1 = 已返回
};

#endif // DISPLAYATTRIBUTE_H

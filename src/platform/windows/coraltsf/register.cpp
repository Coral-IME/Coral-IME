#include "coral/product/product_identity.h"
// ime.cpp: DLL 导出的实现。


#include "pch.h"
#include "resource.h"
#include "ime.h"
#include "dllmain.h"
#include "ini_store.h"
#include <windows.h>
#include <VersionHelpers.h>
#ifndef SPI_GETTHREADLOCALINPUTSETTINGS
#define SPI_GETTHREADLOCALINPUTSETTINGS 0x104E
#endif

#define TEXTSERVICE_LANGID MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)
typedef HRESULT(WINAPI* PTF_INSTALLLAYOUTORTIP)(LPCWSTR psz, DWORD dwFlags);
#define PSZTITLE_HANS                                                     \
  L"0804:{81A20B05-7035-4929-9181-DF030E5455B9}{3F535CF4-1A54-472F-87B5-02B2E9210D05}"

void manage_ime(const BOOL is_register)
{
	HMODULE hInputDLL = LoadLibrary(TEXT("input.dll"));
	if (hInputDLL) {
		PTF_INSTALLLAYOUTORTIP pfnInstallLayoutOrTip = nullptr;
		pfnInstallLayoutOrTip = (PTF_INSTALLLAYOUTORTIP)GetProcAddress(hInputDLL, "InstallLayoutOrTip");
		if (pfnInstallLayoutOrTip) {
			DWORD flag = is_register ? 0 : 0x00000001;
			(*pfnInstallLayoutOrTip)(PSZTITLE_HANS, flag);
		}
		FreeLibrary(hInputDLL);
	}
}

void enable_profile(BOOL fEnable, bool hant = false) {
	ITfInputProcessorProfiles* pProfiles = NULL;

	HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL,
		CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
		reinterpret_cast<LPVOID*>(&pProfiles));

	if (SUCCEEDED(hr)) {
		LANGID lang_id = 0x0804;
		if (fEnable) {
			pProfiles->EnableLanguageProfile(CLSID_TextService, lang_id,
				c_guidProfile, fEnable);
			pProfiles->EnableLanguageProfileByDefault(CLSID_TextService, lang_id,
				c_guidProfile, fEnable);
		}
		else {
			pProfiles->RemoveLanguageProfile(CLSID_TextService, lang_id,
				c_guidProfile);
		}

		pProfiles->Release();
	}
}

static bool ReadRegistryStringValue(HKEY rootKey, const WCHAR* subKey, const WCHAR* valueName, REGSAM extraSam, std::wstring& outValue)
{
	outValue.clear();

	HKEY hKey = nullptr;
	if (RegOpenKeyExW(rootKey, subKey, 0, KEY_QUERY_VALUE | extraSam, &hKey) != ERROR_SUCCESS)
		return false;

	WCHAR buf[MAX_PATH] = { 0 };
	DWORD cbData = sizeof(buf);
	DWORD dwType = REG_SZ;

	const LONG result = RegQueryValueExW(hKey, valueName, nullptr, &dwType,
		reinterpret_cast<LPBYTE>(buf), &cbData);
	RegCloseKey(hKey);

	if (result != ERROR_SUCCESS || dwType != REG_SZ || cbData < sizeof(WCHAR))
		return false;

	if (cbData < sizeof(buf))
		buf[cbData / sizeof(WCHAR)] = 0;
	else
		buf[(sizeof(buf) / sizeof(WCHAR)) - 1] = 0;
	outValue.assign(buf);
	return !outValue.empty();
}

static std::wstring ReadInstallPathFromRegistry()
{
	const WCHAR* subKey = coral::product::kAppRegistryWide;
	const WCHAR* valueName = coral::product::kInstallPathValueWide;

	std::wstring path;
	// 获取值
	ReadRegistryStringValue(HKEY_LOCAL_MACHINE, subKey, valueName, KEY_WOW64_32KEY, path);
	if (path.empty())
		return L"";

	while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
		path.pop_back();
	return path;
}

BOOL RegisterProfiles() {
	BOOL bRet = FALSE;
	CComPtr<ITfInputProcessorProfiles> pInputProcessProfiles;
	HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
		IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&pInputProcessProfiles));
	LOG_ERROR_MSG_W(SUCCEEDED(hr), L"[RegisterProfiles]", L"RegisterProfiles 1");
	hr = pInputProcessProfiles->Register(CLSID_TextService);
	LOG_ERROR_MSG_W(SUCCEEDED(hr), L"[RegisterProfiles]", L"RegisterProfiles 2");
	{
		std::filesystem::path configPath = hlp::getLocalLowCoralPinyinPath() / L"config" / L"coralpinyinConfig.ini";
		std::string iniFilePath = configPath.string();
		IniFileManager ini(iniFilePath);

		std::wstring installPath = ReadInstallPathFromRegistry();
		CString achIconFile(installPath.c_str());
		// TSF 语言配置文件图标。保留在已安装的资源目录中，
		// 使注册对 x86 与 x64 构建都有效。
		achIconFile += L"\\res\\coral_logo.ico";
		std::wstring achIconFileStr = std::wstring(L"[RegisterProfiles] achIconFile: ") + achIconFile.GetString();
		LOG_INFO_W(achIconFileStr.c_str());

		hr = pInputProcessProfiles->AddLanguageProfile(
			CLSID_TextService,
			TEXTSERVICE_LANGID,
			c_guidProfile,
			hlp::_tr(IDS_CORAL_SHURUFA),
			hlp::_tr(IDS_CORAL_SHURUFA).GetLength(),
			achIconFile,
			achIconFile.GetLength(),
			0);
		LOG_ERROR_MSG_W(SUCCEEDED(hr), L"[RegisterProfiles]", L"RegisterProfiles 3");
	}
	bRet = TRUE;
Exit0:
	return bRet;
}

void UnregisterProfiles()
{
	CComPtr<ITfInputProcessorProfiles> pInputProcessProfiles;
	HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
		IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&pInputProcessProfiles));
	if (SUCCEEDED(hr))
	{
		pInputProcessProfiles->RemoveLanguageProfile(CLSID_TextService, TEXTSERVICE_LANGID, c_guidProfile);
		pInputProcessProfiles->Unregister(CLSID_TextService);
	}
	else
	{
		LOG_ERROR_W(L"[UnregisterProfiles] CoCreateInstance failed");
	}
}

static const GUID SupportCategories[] = {
	GUID_TFCAT_CATEGORY_OF_TIP,
	GUID_TFCAT_TIP_KEYBOARD,
	GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
	GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
	GUID_TFCAT_TIPCAP_SECUREMODE,
	GUID_TFCAT_TIPCAP_COMLESS,
	GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
	GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
	GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
};

void UnregisterCategories()
{
	CComPtr<ITfCategoryMgr> pCategoryMgr;
	HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(&pCategoryMgr));
	if (hr != S_OK)
	{
		LOG_ERROR_W(L"[UnregisterCategories] CoCreateInstance failed");
		return;
	}

	for (GUID guid : SupportCategories)
	{
		pCategoryMgr->UnregisterCategory(CLSID_TextService, guid, CLSID_TextService);
	}
}

BOOL RegisterCategories()
{
	BOOL bRet = FALSE;
	CComPtr<ITfCategoryMgr> pCategoryMgr;
	HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(&pCategoryMgr));
	LOG_ERROR_MSG_W(hr == S_OK, L"[RegisterCategories]", L"RegisterCategories 1");
	for (GUID guid : SupportCategories)
	{
		hr = pCategoryMgr->RegisterCategory(CLSID_TextService, guid, CLSID_TextService);
		LOG_ERROR_MSG_W(hr == S_OK, L"[RegisterCategories]", L"RegisterCategories 2");
	}
	bRet = TRUE;
Exit0:
	if (!bRet)
	{
		UnregisterCategories();
	}
	return bRet;
}

////////////////////////////////////////////////////////////////////////////////////
///

_Use_decl_annotations_
STDAPI DllCanUnloadNow(void)
{
#ifdef _MERGE_PROXYSTUB
	HRESULT hr = PrxDllCanUnloadNow();
	if (hr != S_OK)
		return hr;
#endif
	return _AtlModule.DllCanUnloadNow();
}

_Use_decl_annotations_
STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID* ppv)
{
#ifdef _MERGE_PROXYSTUB
	HRESULT hr = PrxDllGetClassObject(rclsid, riid, ppv);
	if (hr != CLASS_E_CLASSNOTAVAILABLE)
		return hr;
#endif
	HRESULT hr = _AtlModule.DllGetClassObject(rclsid, riid, ppv);
	return hr;
}

STDAPI DllRegisterServer(void)
{
	hlp::initCoralPinyinLog(hlp::LogPathType::AppType);
#ifdef _WIN64
	LOG_INFO_W(L"[DllRegisterServer] Starting CORALTSF64 registration");
#else
	LOG_INFO_W(L"[DllRegisterServer] Starting CORALTSF32 registration");
#endif
	if (!RegisterProfiles() || !RegisterCategories())
	{
		UnregisterProfiles();
		UnregisterCategories();
		return E_FAIL;
	}
	HRESULT hr = _AtlModule.DllRegisterServer();
	if(FAILED(hr))
	{
		LOG_ERROR_W(L"[DllRegisterServer] DllRegisterServer failed");
	}
	else
	{
		enable_profile(TRUE);
		manage_ime(TRUE);
	}
#ifdef _MERGE_PROXYSTUB
	if (FAILED(hr))
		return hr;
	hr = PrxDllRegisterServer();
	if(FAILED(hr))
	{
		LOG_ERROR_W(L"[DllRegisterServer] PrxDllRegisterServer failed");
	}
#endif
	return hr;
}

void SwitchToSafeInputMethod()
{
	auto isReinstalling = []() -> bool {
		const WCHAR* subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\coralpinyin";
		const WCHAR* valueName = L"IsReinstalling";
		std::wstring value;
		if (ReadRegistryStringValue(HKEY_LOCAL_MACHINE, subKey, valueName, KEY_WOW64_32KEY, value) && value == L"1")
			return true;
		return false;
	};

	if (isReinstalling())
		return;
	BOOL enabled = FALSE;
	BOOL ok = FALSE;
	//win7默认就是不同进程使用不同输入法，所以不需要切换，且无“允许我为每个应用窗口使用不同的输入法”的设置
	if (IsWindows8OrGreater())
	{
		ok = SystemParametersInfo(
			SPI_GETTHREADLOCALINPUTSETTINGS,
			0,
			&enabled,
			0);
	}
	if (!ok || enabled) 
	{
		LOG_INFO_W(L"[SwitchToSafeInputMethod] Thread local input settings enabled or query failed, skip switching");
		return;
	}
	// 检查当前输入法是否是 CORAL 拼音，如果不是则不需要切换
	if (!hlp::isCoralPinyinActive())
	{
		return;
	}
	//这里选择使用win+space切换输入法，因为win+space是默认的切换输入法，且不能通过设置禁用
	INPUT inputs[4] = {};
	inputs[0].type = INPUT_KEYBOARD;
	inputs[0].ki.wVk = VK_LWIN;
	inputs[0].ki.dwFlags = 0;
	
	inputs[1].type = INPUT_KEYBOARD;
	inputs[1].ki.wVk = VK_SPACE;
	inputs[1].ki.dwFlags = 0;
	
	inputs[2].type = INPUT_KEYBOARD;
	inputs[2].ki.wVk = VK_SPACE;
	inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
	
	inputs[3].type = INPUT_KEYBOARD;
	inputs[3].ki.wVk = VK_LWIN;
	inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
	
	UINT sent = SendInput(4, inputs, sizeof(INPUT));
	if (sent != 4)
	{
		LOG_ERROR_W(L"[SwitchToSafeInputMethod] Failed to send Ctrl+Shift key combination");
	}
}

STDAPI DllUnregisterServer(void)
{
	hlp::initCoralPinyinLog(hlp::LogPathType::AppType);
	SwitchToSafeInputMethod();
	manage_ime(FALSE);
	enable_profile(FALSE);
	UnregisterProfiles();
	UnregisterCategories();
	HRESULT hr = _AtlModule.DllUnregisterServer();
	return hr;
}

// DllInstall - 按用户和计算机在系统注册表中逐一添加/移除项。
STDAPI DllInstall(BOOL bInstall, _In_opt_  LPCWSTR pszCmdLine)
{
	HRESULT hr = E_FAIL;
	static const wchar_t szUserSwitch[] = L"user";

	if (pszCmdLine != nullptr)
	{
		if (_wcsnicmp(pszCmdLine, szUserSwitch, _countof(szUserSwitch)) == 0)
		{
			ATL::AtlSetPerUserRegistration(true);
		}
	}

	if (bInstall)
	{
		hr = DllRegisterServer();
		if (FAILED(hr))
		{
			DllUnregisterServer();
		}
	}
	else
	{
		hr = DllUnregisterServer();
	}

	return hr;
}



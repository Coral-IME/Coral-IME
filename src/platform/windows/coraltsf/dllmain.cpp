// dllmain.cpp: DllMain 的实现。

#include "pch.h"
#include "resource.h"
#include "dllmain.h"

CimeModule _AtlModule;
HINSTANCE g_hInst = nullptr;

// DLL 入口点
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
#ifdef _MERGE_PROXYSTUB
	if (!PrxDllMain(hInstance, dwReason, lpReserved))
		return FALSE;
#endif
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
	{
		g_hInst = hInstance;
		::DisableThreadLibraryCalls(hInstance);
		// 异常过滤器属于宿主应用程序，不属于本 DLL。
		break;
	}

	case DLL_PROCESS_DETACH:
		g_hInst = nullptr;
		break;
	}
	return _AtlModule.DllMain(dwReason, lpReserved);
}
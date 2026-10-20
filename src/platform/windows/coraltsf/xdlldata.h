#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_XDLLDATA_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_XDLLDATA_H_INCLUDED

#ifdef _MERGE_PROXYSTUB

extern "C"
{
	BOOL WINAPI PrxDllMain(HINSTANCE hInstance, DWORD dwReason,
		LPVOID lpReserved);
	STDAPI PrxDllCanUnloadNow(void);
	STDAPI PrxDllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv);
	STDAPI PrxDllRegisterServer(void);
	STDAPI PrxDllUnregisterServer(void);
}
#endif

#endif // XDLLDATA_H

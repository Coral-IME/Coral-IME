#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_DLLMAIN_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_DLLMAIN_H_INCLUDED

// dllmain.h: 模块类的声明。

class CimeModule : public ATL::CAtlDllModuleT< CimeModule >
{
public :
	DECLARE_LIBID(LIBID_imeLib)
	DECLARE_REGISTRY_APPID_RESOURCEID(IDR_IME, "{aad37e8c-6d1e-410a-b307-256f51e6f69d}")
};

extern class CimeModule _AtlModule;

#endif // DLLMAIN_H

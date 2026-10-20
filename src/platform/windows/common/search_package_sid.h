#pragma once

#include <windows.h>
#include <userenv.h>
#include <sddl.h>
#include <string>
#include <vector>

namespace coral::win32
{

// SearchHost 可能使用 LPAC 令牌，该令牌忽略 ALL APPLICATION PACKAGES。
// 在保留交互用户 ACE 以完成 AppContainer 双主体访问检查的同时，
// 显式授予其包标识。
inline std::vector<std::wstring> SearchPackageSids()
{
	std::vector<std::wstring> result;
	for (const wchar_t* family : {
		L"MicrosoftWindows.Client.CBS_cw5n1h2txyewy",
		L"Microsoft.Windows.Search_cw5n1h2txyewy" })
	{
		PSID sid = nullptr;
		if (FAILED(::DeriveAppContainerSidFromAppContainerName(family, &sid)) || !sid)
			continue;
		LPWSTR sidText = nullptr;
		if (::ConvertSidToStringSidW(sid, &sidText) && sidText)
		{
			result.emplace_back(sidText);
			::LocalFree(sidText);
		}
		::FreeSid(sid);
	}
	return result;
}

} // 命名空间 coral::win32 结束

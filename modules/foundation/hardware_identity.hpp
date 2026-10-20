/*
* @author: jijiabao
* @date: 2025-04-21
* @info：得到相关设备硬件信息
* Qt-free 版本：WMI 查询磁盘序列号 -> MD5（Windows CNG），返回 UTF-8 hex 小写，
* 与原 QString + QCryptographicHash(Md5) 结果一致（输入为序列号的 UTF-8 字节）。
*/
#ifndef CORAL_MODULES_FOUNDATION_HARDWARE_IDENTITY_HPP_INCLUDED
#define CORAL_MODULES_FOUNDATION_HARDWARE_IDENTITY_HPP_INCLUDED

#include <comdef.h>
#include <Wbemidl.h>
#include <bcrypt.h>
#include <string>

#pragma comment(lib, "bcrypt.lib")

namespace HardInfo
{
	inline std::string getHdid()
	{
		static std::string hdidStr = []() -> std::string {
			std::string result;
			HRESULT res = ::CoInitialize(NULL);
			if (FAILED(res)) {
				return result;
			}
			do {
				CComPtr<IWbemLocator> spLoc;
				HRESULT hres = ::CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)&spLoc);
				if (FAILED(hres)) break;

				CComPtr<IWbemServices> spSvc;
				hres = spLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &spSvc);
				if (FAILED(hres)) break;

				hres = ::CoSetProxyBlanket(spSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
					RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
				if (FAILED(hres)) break;

				CComPtr<IEnumWbemClassObject> spEnumerator;
				hres = spSvc->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT * FROM Win32_DiskDrive"),
					WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &spEnumerator);
				if (FAILED(hres)) break;

				while (spEnumerator) {
					CComPtr<IWbemClassObject> spClsObj;
					ULONG ulReturn = 0;

					HRESULT hr = spEnumerator->Next(WBEM_INFINITE, 1, &spClsObj, &ulReturn);
					if (FAILED(hr) || ulReturn == 0)
						break;

					CComVariant vtProp;
					hr = spClsObj->Get(L"SerialNumber", 0, &vtProp, 0, 0);
					if (FAILED(hr) || vtProp.vt != VT_BSTR)
						continue;

					std::wstring serialW(vtProp.bstrVal ? vtProp.bstrVal : L"");
					if (!serialW.empty()) {
						// 宽字符 -> UTF-8 再 MD5（与原 QString::toUtf8 行为一致）
						int len = ::WideCharToMultiByte(CP_UTF8, 0, serialW.c_str(),
							static_cast<int>(serialW.size()), nullptr, 0, nullptr, nullptr);
						std::string utf8(len, '\0');
						if (len > 0)
							::WideCharToMultiByte(CP_UTF8, 0, serialW.c_str(),
								static_cast<int>(serialW.size()), &utf8[0], len, nullptr, nullptr);

						BCRYPT_ALG_HANDLE alg = nullptr;
						BCRYPT_HASH_HANDLE hash = nullptr;
						if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0))
							&& BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)))
						{
							if (BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(utf8.data()),
								static_cast<ULONG>(utf8.size()), 0)))
							{
								BYTE digest[16] = { 0 };
								if (BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0)))
								{
									static const char* hex = "0123456789abcdef";
									for (BYTE b : digest)
									{
										result += hex[b >> 4];
										result += hex[b & 0x0F];
									}
								}
							}
						}
						if (hash) BCryptDestroyHash(hash);
						if (alg) BCryptCloseAlgorithmProvider(alg, 0);
						break;
					}
				}
			} while (false);

			::CoUninitialize();
			return result;
		}();

		return hdidStr;
	}
}
#endif

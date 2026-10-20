// -----------------------------------------------------------------------
// 创建人	:	黄智寿
// 创建时间:	2011:7:16 11:55:36
// 功能描述 :	数据解析
// -----------------------------------------------------------------------
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_IPC_COMMON_DATA_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_IPC_COMMON_DATA_H_INCLUDED

#include "helper.h"

//通信报文格式:ResultCode:0|Key1:Val1|Key2:Val2|...
class CDataParser
{
	static const int ecs_bar = 0x14C;
	static const int ecs_colon = 0x14E;
public:
	static const WCHAR* RESULT_CODE;
	inline static constexpr WCHAR PercentPrefix[] = L"@coralipc2|";
	static bool IsPercentEncoded(LPCWSTR data)
	{
		return data && wcsncmp(data, PercentPrefix, _countof(PercentPrefix) - 1) == 0;
	}

public:
	static void ClientSplit(LPCWSTR szData, std::map<CString, CString> &MapData)
	{
		const bool percentEncoding = IsPercentEncoded(szData);
		if (percentEncoding) szData += _countof(PercentPrefix) - 1;
		LPCWSTR pos1, pos2;
		pos1 = szData;
		CString strLastKey, strLastVal;
		while (TRUE)
		{
			pos2 = wcschr(pos1, L':');
			if (!pos2)
			{
				if (wcslen(pos1) > 0)
					strLastKey = pos1;
				break;
			}
			CString strKey = CString(pos1, (int)(pos2 - pos1));

			pos1 = pos2 + 1;
			pos2 = wcschr(pos1, L'|');
			if (!pos2)
			{
				strLastKey = strKey;
				strLastVal = pos1;
				break;
			}
			CString strVal = CString(pos1, (int)(pos2 - pos1));

			MapData[DecodeWord(strKey, percentEncoding)] = DecodeWord(strVal, percentEncoding);
			pos1 = pos2 + 1;
		}

		// 处理最后一项
		if (!strLastKey.IsEmpty())
			MapData[DecodeWord(strLastKey, percentEncoding)] = DecodeWord(strLastVal, percentEncoding);
	}

	static CString ClientMerge(LPCWSTR szMethod, std::vector<CString>& vecParams)
	{
		CString strMerge(PercentPrefix);
		strMerge.Append(EncodeWord(szMethod));
		std::vector<CString>::iterator it = vecParams.begin();
		for (; it != vecParams.end(); ++it)
		{
			strMerge.AppendChar(L'|');
			strMerge.Append(EncodeWord(*it));
		}
		return strMerge;
	}

	static void ServerSplit(LPCWSTR szData, std::vector<CString> &vecData)
	{
		const bool percentEncoding = IsPercentEncoded(szData);
		if (percentEncoding) szData += _countof(PercentPrefix) - 1;
		// Normal
		LPCWSTR pos1, pos2;
		pos1 = szData;
		while (TRUE)
		{
			pos2 = wcschr(pos1, L'|');
			if (!pos2)
				break;
			CString strDecode = CString(pos1, (int)(pos2 - pos1));
			vecData.push_back(DecodeWord(strDecode, percentEncoding));
			pos1 = pos2 + 1;
		}

		// 处理最后一项
		if (!ipc_hlp::IsStrEmpty(szData))
			vecData.push_back(DecodeWord(pos1, percentEncoding));
	}

	static CString ServerMerge(const std::map<CString, CString>& mapKV, bool percentEncoding = true)
	{
		CString strMerge;
		for (std::map<CString, CString>::const_iterator it = mapKV.begin(); it != mapKV.end(); ++it)
		{
			if (!strMerge.IsEmpty())
				strMerge.Append(L"|");
			strMerge.Append(EncodeWord(it->first, percentEncoding));
			strMerge.Append(L":");
			strMerge.Append(EncodeWord(it->second, percentEncoding));
		}
		return percentEncoding ? CString(PercentPrefix) + strMerge : strMerge;
	}

private:
	static CString EncodeWord(LPCWSTR szWord, bool percentEncoding = true)
	{
		CString strWord(szWord);
		if (percentEncoding)
		{
			strWord.Replace(L"%", L"%25");
			strWord.Replace(L"|", L"%7C");
			strWord.Replace(L":", L"%3A");
			return strWord;
		}
		strWord.Replace(L'|', ecs_bar);
		strWord.Replace(L':', ecs_colon);
		return strWord;
	}

	static CString DecodeWord(LPCWSTR szWord, bool percentEncoding)
	{
		CString strWord(szWord);
		if (percentEncoding)
		{
			CString decoded;
			for (int i = 0; i < strWord.GetLength(); ++i)
			{
				WCHAR ch = strWord[i];
				if (ch == L'%' && i + 2 < strWord.GetLength())
				{
					CString escape = strWord.Mid(i, 3);
					if (escape.CompareNoCase(L"%25") == 0) ch = L'%';
					else if (escape.CompareNoCase(L"%7C") == 0) ch = L'|';
					else if (escape.CompareNoCase(L"%3A") == 0) ch = L':';
					else { decoded.AppendChar(ch); continue; }
					i += 2;
				}
				decoded.AppendChar(ch);
			}
			return decoded;
		}
		strWord.Replace(ecs_bar, L'|');
		strWord.Replace(ecs_colon, L':');
		return strWord;
	}
};

#endif // DATA_H

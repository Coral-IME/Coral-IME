#pragma once

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include "utf8conv.h"

namespace coral::settings {
struct CandidateFont {
    std::wstring name;
    std::wstring label;
};

inline std::wstring CanonicalFontName(const std::wstring& name) {
    if (name == L"微软雅黑") return L"Microsoft YaHei";
    if (name == L"宋体") return L"SimSun";
    if (name == L"黑体") return L"SimHei";
    if (name == L"等线") return L"DengXian";
    if (name == L"楷体") return L"KaiTi";
    return name;
}
inline bool SameFont(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(CanonicalFontName(a).c_str(), CanonicalFontName(b).c_str()) == 0;
}
inline int CALLBACK FontFound(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}
// 查询请求的字族而不是用 CreateFont，后者在字族缺失时会
// 静默替换成别的字体。GDI 同时也会解析别名。
inline bool IsInstalledFont(const std::wstring& name) {
    if (name.empty() || name.front() == L'@' || name.size() >= LF_FACESIZE || name.find(L'\0') != std::wstring::npos) return false;
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(query.lfFaceName, name.c_str());
    bool found = false;
    EnumFontFamiliesExW(dc, &query, FontFound, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}
inline std::vector<std::wstring> ParseAddedFonts(const std::string& json) {
    rapidjson::Document document;
    document.Parse(json.c_str());
    std::vector<std::wstring> names;
    if (!document.HasParseError() && document.IsArray()) {
        for (const auto& item : document.GetArray()) {
            if (!item.IsString()) continue;
            const auto name = utf8ToWstr(std::string(item.GetString(), item.GetStringLength()));
            if (!name.empty() && name.find(L'\0') == std::wstring::npos &&
                std::none_of(names.begin(), names.end(), [&](const auto& n) { return SameFont(n, name); }))
                names.push_back(name);
        }
    }
    return names;
}
inline std::string SerializeAddedFonts(const std::vector<std::wstring>& names) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartArray();
    for (const auto& name : names) {
        const auto utf8 = wstrToUtf8(name);
        writer.String(utf8.c_str(), static_cast<rapidjson::SizeType>(utf8.size()));
    }
    writer.EndArray();
    return buffer.GetString();
}
// 保留持久化的缺失字体，使重新安装后可恢复用户的列表，
// 但下拉框中只展示已安装的字族。
template<class IsInstalled>
inline std::vector<CandidateFont> BuildCandidateFonts(const std::vector<std::wstring>& added,
    const std::wstring& current, IsInstalled installed) {
    std::vector<CandidateFont> fonts;
    const auto add = [&](const std::wstring& name, const std::wstring& label) {
        if (installed(name) && std::none_of(fonts.begin(), fonts.end(),
            [&](const auto& font) { return SameFont(font.name, name); }))
            fonts.push_back({ CanonicalFontName(name), label });
    };
    add(L"Microsoft YaHei", L"微软雅黑");
    add(L"SimSun", L"宋体");
    add(L"SimHei", L"黑体");
    add(L"DengXian", L"等线");
    add(L"KaiTi", L"楷体");
    for (const auto& name : added) add(name, name);
    add(current, current);
    return fonts;
}
} // 命名空间 coral::settings 结束

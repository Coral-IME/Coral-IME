#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "thirdparty/simpleini/SimpleIni.h"

namespace coral::config {

// 读取 UTF-8 INI 元数据（可有可无 BOM）。结果为空时由
// 调用方选择回退。Unicode 名称保留原生文件系统路径。
inline std::string readDoublepinyinSchemeTitle(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};

    const std::string data((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    if (file.bad()) return {};

    // 在所有操作系统上使用 ASCII 大小写折叠与保字节的 UTF-8 转换。
    CSimpleIniTempl<char, SI_GenericNoCase<char>, SI_ConvertA<char>> ini(true);
    if (ini.LoadData(data) < 0) return {};
    return ini.GetValue("metadata", "title", "");
}

} // 命名空间 coral::config 结束

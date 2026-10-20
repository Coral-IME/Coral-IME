#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "thirdparty/simpleini/SimpleIni.h"

namespace coral::config {

// 包版本使用四个数字分量，同样适用于 VERSIONINFO。
inline bool isAppVersion(const std::string& version)
{
    unsigned parts = 0;
    unsigned value = 0;
    bool hasDigit = false;
    for (const char ch : version) {
        if (ch >= '0' && ch <= '9') {
            value = value * 10 + (ch - '0');
            if (value > 65535) return false;
            hasDigit = true;
        } else if (ch == '.' && hasDigit && parts < 3) {
            ++parts;
            value = 0;
            hasDigit = false;
        } else {
            return false;
        }
    }
    return parts == 3 && hasDigit;
}

// 从运行中的应用旁读取包元数据，绝不读取可变的用户配置，
// 也不读取可能指向其他已安装版本的注册表路径。
// 原生文件系统路径可在 Windows 上保留 Unicode 安装路径。
inline std::string readInstalledAppVersion(const std::filesystem::path& installDir)
{
    std::ifstream file(installDir / "config" / "coralpinyinConfig.ini", std::ios::binary);
    if (!file) return {};
    const std::string data((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    if (file.bad()) return {};
    CSimpleIniTempl<char, SI_GenericNoCase<char>, SI_ConvertA<char>> ini(true);
    if (ini.LoadData(data) < 0) return {};
    const std::string version = ini.GetValue("app", "Version", "");
    return isAppVersion(version) ? version : std::string{};
}

} // 命名空间 coral::config 结束

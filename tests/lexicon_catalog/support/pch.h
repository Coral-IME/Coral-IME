#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "coralnet.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#define LOG_INFO(message) ((void)0)
#define LOG_ERROR(message) ((void)0)
std::string wstrToUtf8(const std::wstring& text);
namespace hlp { std::filesystem::path getLocalLowCoralPinyinPath(); }

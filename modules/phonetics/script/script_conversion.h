#pragma once
#ifndef CORAL_MODULES_PHONETICS_SCRIPT_SCRIPT_CONVERSION_H_INCLUDED
#define CORAL_MODULES_PHONETICS_SCRIPT_SCRIPT_CONVERSION_H_INCLUDED


#include <opencc/src/opencc.h>
#include <vector>
#include <filesystem>

namespace coral::phonetics {
class ScriptConversion
{
public:
	static ScriptConversion& instance();
	~ScriptConversion();
    void reset();

	bool load(const std::filesystem::path& curPath, const std::string& configJsonName = "s2t.json");
	std::string ConvertText(const std::wstring& simplified, const std::string& configJsonName = "s2t.json");
	std::vector<std::string> ConvertText(const std::vector<std::wstring>& simplified, const std::string& configJsonName = "s2t.json");

private:
	ScriptConversion();
	bool init(const std::string& jsonName);
	
private:
	opencc_t convert_hwnd_;
	std::filesystem::path data_path_;
	std::string config_json_name_;
};
} // 命名空间 coral::phonetics 结束（批次 2 辅助输入）


#endif // S2T_H

#include <filesystem>
#include <iostream>
#include <string>
#include <opencc/src/UTF8Util.hpp>
#include <opencc/src/opencc.h>
#include "script_conversion.h"
#include "trace_log.h"

coral::phonetics::ScriptConversion& coral::phonetics::ScriptConversion::instance()
{
	static coral::phonetics::ScriptConversion instance;
	return instance;
}

coral::phonetics::ScriptConversion::ScriptConversion() : convert_hwnd_(nullptr), data_path_(""), config_json_name_("")
{

}

coral::phonetics::ScriptConversion::~ScriptConversion()
{
    reset();
}

void coral::phonetics::ScriptConversion::reset()
{
    if (convert_hwnd_) opencc_close(convert_hwnd_);
    convert_hwnd_ = nullptr;
    data_path_.clear();
    config_json_name_.clear();
}

bool coral::phonetics::ScriptConversion::load(const std::filesystem::path& curPath, const std::string& jsonName)
{
    reset();
	this->data_path_ = curPath;
	this->config_json_name_ = jsonName;
	return true;
}

std::string coral::phonetics::ScriptConversion::ConvertText(const std::wstring& simplified, const std::string& configJsonName)
{
	if(!init(configJsonName))
	{
		return "";
	}
	std::string convertStr = opencc::UTF8Util::U16ToU8(simplified);
	auto ret = opencc_convert_utf8(convert_hwnd_, convertStr.c_str(), convertStr.size());
	std::string outText(ret);
	opencc_convert_utf8_free(ret);
	return outText;
}

std::vector<std::string> coral::phonetics::ScriptConversion::ConvertText(const std::vector<std::wstring>& simplified, const std::string& configJsonName)
{
	if(!init(configJsonName))
	{
		return {};
	}
	std::vector<std::string> resRet;
	for (auto& simple : simplified)
	{
		std::string convertStr = opencc::UTF8Util::U16ToU8(simple);
		auto ret = opencc_convert_utf8(convert_hwnd_, convertStr.c_str(), convertStr.size());
		resRet.push_back(ret);
		opencc_convert_utf8_free(ret);
	}
	return resRet;
}

bool coral::phonetics::ScriptConversion::init(const std::string& jsonName)
{
	if(this->convert_hwnd_ == nullptr || this->config_json_name_ != jsonName)
	{
		this->config_json_name_ = jsonName;
		std::filesystem::path dataPath = this->data_path_ / "opencc" / this->config_json_name_;
		if (!std::filesystem::exists(dataPath))
		{
			std::cout << "data path not exists: " << dataPath << "\n";
			return false;
		}
		if(convert_hwnd_)
		{
			opencc_close(convert_hwnd_);
		}
#ifdef _MSC_VER
		convert_hwnd_ = opencc_open_w(dataPath.c_str());
#else
		convert_hwnd_ = opencc_open(dataPath.u8string().c_str());
#endif
		if(convert_hwnd_ == nullptr)
		{
			LOG_ERROR(std::string("opencc init failed: ") + dataPath.u8string());
			return false;
		}
	}
	return true;
}
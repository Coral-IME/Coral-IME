#include "pch.h"
#include "settingmenu.h"
#include "settingswindow.h"
#include "addphrasedialog.h"
#include "hardware_identity.hpp"
#include "apphost.h"

#include "coralnet.h"
#include <rapidjson/document.h>

namespace
{
	// 与 WebConfig 保持一致
	constexpr const char* kPlaceholderApiUrl = "https://shurufa.coral.cn/api/v1/soft";
	constexpr const char* kFeedbackUrl = "https://f.coral.cn/g/lhfCW2In/";
	constexpr int kGetRequestTimeout = 10000;

}

SettingMenu* SettingMenu::getMenu() {
	static SettingMenu menuIns;
	return &menuIns;
}

void SettingMenu::showSettingMenu()
{
	SettingsWindow::showInstance(); // 显示并居中设置窗口
	fetchPlaceholderData();

}

void SettingMenu::showAddPhrasePage(const std::wstring& pinyin, const std::wstring& word)
{
	AddPhraseDialog::Request request;
	request.content = word;
	// 正在输入的拼音作为输入码预填；带分隔符以外的符号时交给默认输入码。
	std::wstring code;
	for (const wchar_t c : pinyin)
	{
		if (c == L'\'') continue;
		if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'))) { code.clear(); break; }
		code += static_cast<wchar_t>(towlower(c));
	}
	request.code = code;
	AddPhraseDialog::show(nullptr, request);
}

void SettingMenu::onSendFeedbackActionTriggered() {
	openUrl(utf8ToWstr(kFeedbackUrl));
}

void SettingMenu::openUrl(const std::wstring& url)
{
	::ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_RESTORE);
}

void SettingMenu::fetchPlaceholderData()
{
	// 上一次请求仍在进行中则跳过
	if (m_placeholderRequest && m_placeholderRequest->isRunning())
		return;

	std::string version = sConfig->getSettingStore()->getIniValueAs<std::string>("app", "Version");
	std::string hdid = HardInfo::getHdid();
	std::string url = std::string(kPlaceholderApiUrl)
		+ "?version=" + version + "&hdid=" + hdid;

	coralnet::RequestOptions options;
	options.url = url;
	options.timeoutMs = kGetRequestTimeout;

	// coralnet 回调在工作线程，转发回主线程处理
	m_placeholderRequest = coralnet::asyncRequest(coralnet::HttpMethod::Get, options,
		[this](const std::string& body, coralnet::NetResult result) {
			std::string response = result.success ? body : std::string();
			bool success = result.success;
			appPostTask([this, response, success]() {
				onPlaceholderDataReceived(response, success);
			});
		});
}

void SettingMenu::onPlaceholderDataReceived(const std::string& response, bool success)
{
	if (!success || response.empty())
	{
		LOG_ERROR("SettingMenu: Failed to fetch placeholder data");
		return;
	}

	rapidjson::Document doc;
	doc.Parse(response.c_str());
	if (doc.HasParseError() || !doc.IsObject())
	{
		LOG_ERROR("SettingMenu: JSON parse error");
		return;
	}
	if (!doc.HasMember("code") || !doc["code"].IsInt() || doc["code"].GetInt() != 0)
	{
		LOG_ERROR("SettingMenu: API returned error code");
		return;
	}
	if (!doc.HasMember("data") || !doc["data"].IsObject())
	{
		LOG_ERROR("SettingMenu: No data object in response");
		return;
	}
	const rapidjson::Value& dataObj = doc["data"];
	if (!dataObj.HasMember("data") || !dataObj["data"].IsArray())
	{
		LOG_ERROR("SettingMenu: No data array in response");
		return;
	}

	// 读取数据并写入ini
	std::unordered_map<std::string, std::string> placeholderConfig;
	for (const auto& item : dataObj["data"].GetArray())
	{
		if (!item.IsObject())
			continue;
		if (item.HasMember("sname") && item.HasMember("conf")
			&& item["sname"].IsString() && item["conf"].IsString())
		{
			placeholderConfig[item["sname"].GetString()] = item["conf"].GetString();
		}
	}
	if (!placeholderConfig.empty())
		sConfig->setPlaceHolderValue(placeholderConfig);
}

#include "pch.h"
#include "calendar_text.h"
#include <text_file_helpers.hpp>
#include <regex>
#include <chrono>

//todo 和kunmbercov 的有重复，考虑独立出来
ToDate::ToDate() : chinese_nums_ ({ u8"零", u8"一", u8"二", u8"三", u8"四", u8"五", u8"六", u8"七", u8"八", u8"九", u8"十" }),
uppercase_chinese_nums_ ({ u8"零", u8"壹", u8"贰", u8"叁", u8"肆", u8"伍", u8"陆", u8"柒", u8"捌", u8"玖", u8"拾" }),
days_in_month_ ({ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }){}

ToDate::~ToDate()
{

}

std::string ToDate::number_to_chinese(const std::string& str)
{
	std::string res;
	if (str.size() > 2)
	{
		for (size_t i = 0; i < str.size(); i++)
		{
			res += chinese_nums_[str[i] - '0'];
		}
	}
	else
	{
		for (size_t i = 0; i < str.size(); i++)
		{
			if (str.size() > 1 && i == 0)
			{
				if (str[i] - '0' > 1)
				{
					res += chinese_nums_[str[i] - '0'];
				}
				if (str[i] > '0')
				{
					res += chinese_nums_[10];
				}
			}
			else
			{
				if (str[i] > '0' || str.size() == 1)
				{
					res += chinese_nums_[str[i] - '0'];
				}
			}
		}
	}
	return res;
}

std::string ToDate::number_to_uppercase_chinese(const std::string& str)
{
	std::string res;
	if (str.size() > 2)
	{
		for (size_t i = 0; i < str.size(); i++)
		{
			res += uppercase_chinese_nums_[str[i] - '0'];
		}
	}
	else
	{
		for (size_t i = 0; i < str.size(); i++)
		{
			if (str.size() > 1 && i == 0)
			{
				if (str[i] - '0' > 1)
				{
					res += uppercase_chinese_nums_[str[i] - '0'];
				}
				if (str[i] > '0')
				{
					res += uppercase_chinese_nums_[10];
				}
			}
			else
			{
				if (str[i] > '0' || str.size() == 1)
				{
					res += uppercase_chinese_nums_[str[i] - '0'];
				}
			}
		}
	}
	return res;
}

bool ToDate::parse_date(const std::string& dateStr, std::string& numberRes, std::string& chineseRes, std::string& uppercaseChineseRes)
{
	std::regex yearMonthDayPattern(
		R"((\d{4})[-/.](\d{1,2})[-/.](\d{1,2}))"
	);
	std::regex yearMonthPattern(
		R"((\d{4})[-/.](\d{1,2}))"
	);
	std::regex monthDayPattern(
		R"((\d{1,2})[-/.](\d{1,2}))"
	);
	std::regex timePattern(
		R"((\d{1,2})[:：](\d{1,2})[:：]?(\d{1,2})?)"
	);

	std::smatch matches;
	if (std::regex_search(dateStr, matches, yearMonthDayPattern))
	{
		if (!is_date_valid_ymd(std::stoi(matches[1]), std::stoi(matches[2]), std::stoi(matches[3])))
		{
			return false;
		}
		std::string yearChinese = number_to_chinese(matches[1]);
		std::string monthChinese = number_to_chinese(matches[2]);
		std::string dayChinese = number_to_chinese(matches[3]);
		std::string yearUppercaseChinese = number_to_uppercase_chinese(matches[1]);
		std::string monthUppercaseChinese = number_to_uppercase_chinese(matches[2]);
		std::string dayUppercaseChinese = number_to_uppercase_chinese(matches[3]);

		numberRes = matches[1].str() + u8"年" + matches[2].str() + u8"月" + matches[3].str() + u8"日";
		chineseRes = yearChinese + u8"年" + monthChinese + u8"月" + dayChinese + u8"日";
		uppercaseChineseRes = yearUppercaseChinese + u8"年" + monthUppercaseChinese + u8"月" + dayUppercaseChinese + u8"日";

		return true;
	}
	else if (std::regex_search(dateStr, matches, yearMonthPattern))
	{
		if (!is_date_valid_ym(std::stoi(matches[1]), std::stoi(matches[2])))
		{
			return false;
		}
		std::string yearChinese = number_to_chinese(matches[1]);
		std::string monthChinese = number_to_chinese(matches[2]);
		std::string yearUppercaseChinese = number_to_uppercase_chinese(matches[1]);
		std::string monthUppercaseChinese = number_to_uppercase_chinese(matches[2]);

		numberRes = matches[1].str() + u8"年" + matches[2].str() + u8"月";
		chineseRes = yearChinese + u8"年" + monthChinese + u8"月";
		uppercaseChineseRes = yearUppercaseChinese + u8"年" + monthUppercaseChinese + u8"月";

		return true;
	}
	else if (std::regex_search(dateStr, matches, monthDayPattern))
	{
		if (!is_date_valid_md(std::stoi(matches[1]), std::stoi(matches[2])))
		{
			return false;
		}
		std::string monthChinese = number_to_chinese(matches[1]);
		std::string dayChinese = number_to_chinese(matches[2]);
		std::string monthUppercaseChinese = number_to_uppercase_chinese(matches[1]);
		std::string dayUppercaseChinese = number_to_uppercase_chinese(matches[2]);

		numberRes = matches[1].str() + u8"月" + matches[2].str() + u8"日";
		chineseRes = monthChinese + u8"月" + dayChinese + u8"日";
		uppercaseChineseRes = monthUppercaseChinese + u8"月" + dayUppercaseChinese + u8"日";

		return true;
	}
	else if (std::regex_search(dateStr, matches, timePattern))
	{
		for (size_t i = 1; i < matches.size(); i++)
		{
			if (matches[i].matched)
			{
				time_.push_back(matches[i]);
			}

		}
		return false;
	}
	else
	{

	}
	return false;
}

bool ToDate::is_date_valid_ymd(const int year, const int month, const int day)
{
	if (year < 0 || month < 1 || month > 12)
	{
		return false;
	}

	// 如果是闰年，二月有29天
	if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
	{
		days_in_month_[1] = 29;
	}
	else
	{
		days_in_month_[1] = 28;
	}

	if (day < 1 || day > days_in_month_[month - 1])
	{
		return false;
	}

	return true;
}

bool ToDate::is_date_valid_ym(const int year, const int month)
{
	if (year < 0 || month < 1 || month > 12)
	{
		return false;
	}

	return true;
}

bool ToDate::is_date_valid_md(const int month, const int day)
{
	if (month < 1 || month > 12)
	{
		return false;
	}

	days_in_month_[1] = 29;

	// 检查天数是否在月份的有效范围内
	if (day < 1 || day > days_in_month_[month - 1])
	{
		return false;
	}

	return true;
}

bool ToDate::to_time(std::vector<std::string>& timeRes)
{
	if (time_.size() == 0)
	{
		return false;
	}
	else if (time_.size() > 2)
	{
		if (std::stoi(time_[0]) >= 24 || std::stoi(time_[1]) >= 60 || std::stoi(time_[2]) >= 60)
		{
			return false;
		}
		else
		{
			std::string res1 = time_[0] + u8"时" + time_[1] + u8"分" + time_[2] + u8"秒";
			std::string res2 = number_to_chinese(time_[0]) + u8"时" + number_to_chinese(time_[1]) + u8"分" + number_to_chinese(time_[2]) + u8"秒";
			std::string res3 = number_to_uppercase_chinese(time_[0]) + u8"时" + number_to_uppercase_chinese(time_[1]) + u8"分" + number_to_uppercase_chinese(time_[2]) + u8"秒";
			timeRes.push_back(res1);
			timeRes.push_back(res2);
			timeRes.push_back(res3);
		}
	}
	else
	{
		if (std::stoi(time_[0]) >= 60 || std::stoi(time_[1]) >= 60)
		{
			return false;
		}
		else
		{
			if (std::stoi(time_[0]) < 24)
			{
				std::string res1 = time_[0] + u8"时" + time_[1] + u8"分";
				std::string res2 = number_to_chinese(time_[0]) + u8"时" + number_to_chinese(time_[1]) + u8"分";
				std::string res3 = number_to_uppercase_chinese(time_[0]) + u8"时" + number_to_uppercase_chinese(time_[1]) + u8"分";
				timeRes.push_back(res1);
				timeRes.push_back(res2);
				timeRes.push_back(res3);
			}
			std::string res4 = time_[0] + u8"分" + time_[1] + u8"秒";
			std::string res5 = number_to_chinese(time_[0]) + u8"分" + number_to_chinese(time_[1]) + u8"秒";
			std::string res6 = number_to_uppercase_chinese(time_[0]) + u8"分" + number_to_uppercase_chinese(time_[1]) + u8"秒";
			timeRes.push_back(res4);
			timeRes.push_back(res5);
			timeRes.push_back(res6);
		}
	}
	return true;
}

bool ToDate::digital_to_date(const std::string& expression, std::string& numberRes, std::string& chineseRes, std::string& uppercaseChineseRes, std::vector<std::string>& timeRes, std::string& nowRes)
{
	bool isok = true;
	if (expression == "nowtime")
	{
		auto now = std::chrono::system_clock::now();
		std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
		std::tm now_tm = {};
		LOCALTIME_R(&now_time_t, &now_tm);
		std::string hour = std::to_string(now_tm.tm_hour);
		std::string min = std::to_string(now_tm.tm_min);
		std::string sec = std::to_string(now_tm.tm_sec);
		nowRes = hour + u8"时" + min + u8"分" + sec + u8"秒";
	}
	else if (expression == "nowdate")
	{
		auto now = std::chrono::system_clock::now();
		std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
		std::tm now_tm = {};
		LOCALTIME_R(&now_time_t, &now_tm);
		std::string year = std::to_string(now_tm.tm_year + 1900);
		std::string mon = std::to_string(now_tm.tm_mon + 1);
		std::string day = std::to_string(now_tm.tm_mday);
		nowRes = year + u8"年" + mon + u8"月" + day + u8"日";
	}
	else
	{
		bool isFinished = parse_date(expression, numberRes, chineseRes, uppercaseChineseRes);
		if (!isFinished)
		{
			isok = to_time(timeRes);
		}
	}

	time_.clear();

	return isok;
}
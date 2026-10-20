
//--------------------------------------------------------
// 创建人：
// 创建时间：20240823
// 功能：实现数字转日期、时间以及获取当前时间和日期的功能
//--------------------------------------------------------
#pragma once
#ifndef CORAL_MODULES_PHONETICS_CALENDAR_TEXT_H_INCLUDED
#define CORAL_MODULES_PHONETICS_CALENDAR_TEXT_H_INCLUDED

#include<string>
#include<vector>

class ToDate
{
public:
	ToDate();
	~ToDate();

	std::string number_to_chinese(const std::string& str);
	std::string number_to_uppercase_chinese(const std::string& str);
	bool parse_date(const std::string& dateStr, std::string& numberRes, std::string& chineseRes, std::string& uppercaseChineseRes);
	bool is_date_valid_ymd(const int year, const int month, const int day);
	bool is_date_valid_ym(const int year, const int month);
	bool is_date_valid_md(const int month, const int day);
	bool to_time(std::vector<std::string>& timeRes);
	bool digital_to_date(
		const std::string& expression,
		std::string& numberRes,
		std::string& chineseRes,
		std::string& uppercaseChineseRes,
		std::vector<std::string>& mtimeRes,
		std::string& nowRes);

private:
	std::vector<std::string> time_;
	std::vector<std::string> chinese_nums_;
	std::vector<std::string> uppercase_chinese_nums_;
	std::vector<int> days_in_month_;
};

#endif // KTODATE_H

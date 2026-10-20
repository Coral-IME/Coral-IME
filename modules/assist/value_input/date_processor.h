#pragma once
#ifndef CORAL_MODULES_ASSIST_VALUE_INPUT_DATE_PROCESSOR_H_INCLUDED
#define CORAL_MODULES_ASSIST_VALUE_INPUT_DATE_PROCESSOR_H_INCLUDED

// -------------------------------------------------------------------------- //
//	文件名		：	dateconvertprocessor.h
//	创建者		：	
//	创建时间	：	2024-08-19
//	功能描述	：	v模式日期转换
//
// -------------------------------------------------------------------------- //
#include <string>
#include <ctime>
#include <array>
#include <string_view>
#include "value_processor.h"

namespace coral::assist {
class DateProcessor : public coral::assist::ValueProcessor {
public:
    virtual bool process(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) override;

private:
    using DateParts = std::array<std::string, 4>;
    std::vector<std::string> convert_date(const std::string& input);
    std::vector<std::string> process_full_format(const DateParts& match);
    std::vector<std::string> process_year_month_format(const DateParts& match);
    std::vector<std::string> process_month_day_format(const DateParts& match);
    bool is_valid_date(const std::string& year, const std::string& month, const std::string& day);//日期检验
    template <size_t N>
    std::string to_chinese_number(int number, const std::array<std::string_view, N>& yearstr);
    template <size_t N>
    std::string convert_number_to_chinese(int num, const std::array<std::string_view, N>& numstr, bool isTra);
    std::string convert_to_chinese_date(const std::string& year, const std::string& month, const std::string& day, bool isTra);//转换成中文日期
    std::string convert_to_week(const std::string& year, const std::string& month, const std::string& day);//转换成星期几
    std::string get_day_of_week(const std::tm& date);
    std::string convert_to_lunar(const std::string& year, const std::string& month, const std::string& day);//转换为农历
    bool is_leap_year(int year);
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


#endif // KDATECONVERTPROCESSOR_H

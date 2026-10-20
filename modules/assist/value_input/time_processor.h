#pragma once
#ifndef CORAL_MODULES_ASSIST_VALUE_INPUT_TIME_PROCESSOR_H_INCLUDED
#define CORAL_MODULES_ASSIST_VALUE_INPUT_TIME_PROCESSOR_H_INCLUDED

// -------------------------------------------------------------------------- //
//	文件名		：	timeconvertprocessor.h
//	创建者		：	
//	创建时间	：	2024-08-21
//	功能描述	：	v模式日时间转换
//
// -------------------------------------------------------------------------- //

#include "value_processor.h"
#include <vector>
#include <array>
#include <string>

namespace coral::assist {
class TimeProcessor : public coral::assist::ValueProcessor {
public:
    ~TimeProcessor() override = default;
    virtual bool process(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) override;
private:
    bool parse_time(const std::string& timeStr, std::array<int, 3>& parts, int& count);
    std::string number_to_chinese(int number);
    std::vector<std::string> generate_time_formats(const std::array<int, 3>& timeParts, int count);
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


#endif // KTIMECONVERTPROCESSOR_H

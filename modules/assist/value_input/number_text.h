#pragma once
#ifndef CORAL_MODULES_ASSIST_VALUE_INPUT_NUMBER_TEXT_H_INCLUDED
#define CORAL_MODULES_ASSIST_VALUE_INPUT_NUMBER_TEXT_H_INCLUDED

#include "value_processor.h"

// -------------------------------------------------------------------------- //
//	文件名		：vmodenumber.h
//	创建者		：zhanghuarong	
//	创建时间	：	2024-08-19
//	功能描述	：	v模式处理输入数字情况的类
// -------------------------------------------------------------------------- //

namespace coral::assist {
class NumberProcessor: public coral::assist::ValueProcessor
{
public:
	NumberProcessor();
	~NumberProcessor() override;
	NumberProcessor(const NumberProcessor&) = delete;
	NumberProcessor(const NumberProcessor&&) = delete;
	NumberProcessor& operator=(const NumberProcessor&) = delete;
	NumberProcessor& operator=(NumberProcessor&&)=delete;

public:
	bool process(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) override;
	bool is_number(const std::string& input);
	// 转为中文小写 v123.33 一百二十三点三三
	std::string number_to_chinese(const std::string& numString);
	// 转为中文大写 v123.33 壹佰贰拾叁点叁叁
	std::string number_to_capital_chinese(const std::string& numString);
	// 单纯转为中文小写  v123.33 一百二十三点三三
	std::string ChineseLowercase(const std::string& numString);
	// 单纯转为中文大写  v123.33 壹贰叁点叁叁
	std::string ChineseCapitalization(const std::string& numString);
	// 转为中文小写+元+角+分  v123.33 一百二十三元三角三分
	std::string lowercase_rmb(const std::string& numString);
	// 转为中文大写+元+角+分  v123.33 壹佰贰拾叁元叁角叁分
	std::string capital_rmb(const std::string& numString);
	// 转为罗马 v123.3 CXXIII  1 <= num <= 3999
	std::string int_to_roma(long long num);

	std::string convert_integer_to_chinese(const std::string& numString);
	std::string convert_fraction_to_chinese(const std::string& fractionalPart);
	void solution_sort(std::vector<coral::phonetics::CandidateResult>& sents);
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


#endif // KVMODENUMBER_H

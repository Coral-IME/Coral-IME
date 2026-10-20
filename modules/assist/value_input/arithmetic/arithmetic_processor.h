#pragma once
#ifndef CORAL_MODULES_ASSIST_VALUE_INPUT_ARITHMETIC_ARITHMETIC_PROCESSOR_H_INCLUDED
#define CORAL_MODULES_ASSIST_VALUE_INPUT_ARITHMETIC_ARITHMETIC_PROCESSOR_H_INCLUDED
// -------------------------------------------------------------------------- //
//	文件名		：	exprcalculateprocessor.h
//	创建者		：	
//	创建时间	：	2024-08-19
//	功能描述	：	v模式拼音解析-算术表达式处理类
//
// -------------------------------------------------------------------------- //
#include <string>
#include <vector>
#include "../value_processor.h"
#include "expression_evaluator.h"
namespace coral::assist {
class ArithmeticProcessor :public coral::assist::ValueProcessor {
public:
    ~ArithmeticProcessor() override= default;
    bool process(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) override;
private:
    coral::assist::ExpressionEvaluator expr_calcu_;
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


#endif // KEXPRCALCULATEPROCESSOR_H

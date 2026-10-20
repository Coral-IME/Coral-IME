#pragma once
#ifndef CORAL_MODULES_ASSIST_VALUE_INPUT_VALUE_PROCESSOR_H_INCLUDED
#define CORAL_MODULES_ASSIST_VALUE_INPUT_VALUE_PROCESSOR_H_INCLUDED

// -------------------------------------------------------------------------- //
//	文件名		：	vmodepinyinprocessor.h
//	创建者		：	
//	创建时间	：	2024-08-19
//	功能描述	：	v模式拼音解析处理抽象类
//
// -------------------------------------------------------------------------- //
#include <string>
#include <vector>
#include "candidate_result.h"

namespace coral::assist {
class ValueProcessor {
public:
    virtual ~ValueProcessor() = default;
    virtual bool process(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) = 0;
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


#endif // KVMODEPINYINPROCESSOR_H

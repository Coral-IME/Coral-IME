#pragma once
#ifndef CORAL_MODULES_ASSIST_VALUE_INPUT_CLIPBOARD_CLIPBOARD_PROCESSOR_H_INCLUDED
#define CORAL_MODULES_ASSIST_VALUE_INPUT_CLIPBOARD_CLIPBOARD_PROCESSOR_H_INCLUDED

// -------------------------------------------------------------------------- //
//	文件名		：	clipboardprocessor.h
//	创建者		：	
//	创建时间	：	2024-08-18
//	功能描述	：	vv模式获取剪切板历史记录-vmode剪切板处理，输入vv显示最近几条剪切板的记录
//
// -------------------------------------------------------------------------- //

#include <string>
#include <vector>
#include "../value_processor.h"

class ClipboardProcessor :public coral::assist::ValueProcessor {
public:
	ClipboardProcessor();

	~ClipboardProcessor() override;
	bool process(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) override;

};

#endif // KCLIPBOARDPROCESSOR_H

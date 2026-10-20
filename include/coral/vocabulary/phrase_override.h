#pragma once
#ifndef CORAL_INCLUDE_CORAL_VOCABULARY_PHRASE_OVERRIDE_H_INCLUDED
#define CORAL_INCLUDE_CORAL_VOCABULARY_PHRASE_OVERRIDE_H_INCLUDED

#include <cstdint>
#include <string>

// 常用语：输入码（小写字母）命中后排在候选首位的固定短语。字符串均为 UTF-8。
struct CustomPhraseEntry {
	std::string code;
	std::string phrase;
	std::uint64_t sequence = 0; // 越大越新
};

#endif // CUSTOMPHRASE_H

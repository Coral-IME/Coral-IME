/*
* @author: xuxiang
* @date: 2022-5-31 16:02:25
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_CANDIDATE_TRANSFORMS_H_INCLUDED
#define CORAL_MODULES_PHONETICS_CANDIDATE_TRANSFORMS_H_INCLUDED

#include <thirdparty/parallel_hashmap/btree.h>
#include <thirdparty/parallel_hashmap/phmap.h>
#include "candidate_result.h"
#include <ctime>

using phmap::btree_set;
using phmap::flat_hash_map;

namespace coral::phonetics {
class CandidateTransform
{
public:
	CandidateTransform() {}
	explicit CandidateTransform(const string& name): name_(name) {}
	virtual ~CandidateTransform() = default;
	virtual bool apply(const string& text, coral::phonetics::ParseStrings& postTexts) const = 0;
	virtual const string& commit_text(const string& input, const string& display_text) const { return display_text; }
	virtual bool process_middleware(
		const string& pystring, 
		const string& sentence, 
		coral::phonetics::ParseStrings& postStrings,
		bool& shouldBreak,
		bool& shouldContinue,
		int& insertPosition,
		int& maxInsertCount,
		MiddlewareType& middlewareType) const = 0;
	const string& get_name() { return name_; }
	
protected:
	string name_;
};
} // 命名空间 coral::phonetics 结束（批次 2 辅助输入）


namespace coral::phonetics {
class DateTimeTransform : public coral::phonetics::CandidateTransform
{
public:
	DateTimeTransform() : coral::phonetics::CandidateTransform() {}
	explicit DateTimeTransform(const string& name) : coral::phonetics::CandidateTransform(name) {}
	~DateTimeTransform() override = default;
	bool apply(const string& text, coral::phonetics::ParseStrings& postTexts) const override;
	bool process_middleware(
		const string& pystring, 
		const string& sentence, 
		coral::phonetics::ParseStrings& postStrings,
		bool& shouldBreak,
		bool& shouldContinue,
		int& insertPosition,
		int& maxInsertCount,
		MiddlewareType& middlewareType) const override;
protected:
	std::vector<std::shared_ptr<coral::phonetics::CandidateTransform>> funcs_;
private:
	bool get_current_local_time(std::tm& localTime) const;
	std::string get_formatted_date(const std::tm& localTime) const;
	std::string get_formatted_time(const std::tm& localTime) const;
	std::string get_formatted_week(const std::tm& localTime) const;
};
} // 命名空间 coral::phonetics 结束（批次 2 辅助输入）


namespace coral::phonetics {
class NumberTransform : public coral::phonetics::CandidateTransform
{
public:
	NumberTransform() :coral::phonetics::CandidateTransform() {}
	~NumberTransform() override = default;
	bool initialize(const string& name);
	bool apply(const string& text, coral::phonetics::ParseStrings& postTexts) const override;
	bool process_middleware(
		const string& pystring, 
		const string& sentence, 
		coral::phonetics::ParseStrings& postStrings,
		bool& shouldBreak,
		bool& shouldContinue,
		int& insertPosition,
		int& maxInsertCount,
		MiddlewareType& middlewareType) const override;
private:
	std::map<std::string, std::array<std::string, 2>> number_map_;
};
} // 命名空间 coral::phonetics 结束（批次 2 辅助输入）



namespace coral::phonetics {
class SymbolWordTransform : public coral::phonetics::CandidateTransform {
public:
    SymbolWordTransform() : coral::phonetics::CandidateTransform() {}
    ~SymbolWordTransform() override = default;
    bool initialize(const string& name, const filesystem::path& configPath);
    bool apply(const string& text, coral::phonetics::ParseStrings& postTexts) const override;
    bool process_middleware(
        const string& pystring, 
        const string& sentence, 
        coral::phonetics::ParseStrings& postStrings,
        bool& shouldBreak,
        bool& shouldContinue,
        int& insertPosition,
        int& maxInsertCount,
        MiddlewareType& middlewareType) const override;
private:
    std::unordered_map<std::string, std::vector<std::string>> symbol_word_map_;
};
} // 命名空间 coral::phonetics 结束（批次 2 辅助输入）


namespace coral::phonetics {
class EmojiTransform : public coral::phonetics::CandidateTransform {
public:
    EmojiTransform() : coral::phonetics::CandidateTransform() {}
    ~EmojiTransform() override = default;
    bool initialize(const string& name, const filesystem::path& configPath);
    bool apply(const string& text, coral::phonetics::ParseStrings& postTexts) const override;
    bool process_middleware(
        const string& pystring, 
        const string& sentence, 
        coral::phonetics::ParseStrings& postStrings,
        bool& shouldBreak,
        bool& shouldContinue,
        int& insertPosition,
        int& maxInsertCount,
        MiddlewareType& middlewareType) const override;
private:
    std::unordered_map<std::string, std::vector<std::string>> emoji_word_map_;
	std::atomic<bool> is_loaded_ = false;
};
} // 命名空间 coral::phonetics 结束（批次 2 辅助输入）


namespace coral::phonetics {
class PronunciationTransform : public coral::phonetics::CandidateTransform
{
public:
	PronunciationTransform() : coral::phonetics::CandidateTransform() {}
	~PronunciationTransform() override = default;
	bool initialize(const string& name);
	bool apply(const string& text, coral::phonetics::ParseStrings& postTexts) const override;
	const string& commit_text(const string& input, const string& display_text) const override;
	bool process_middleware(
		const string& pystring, 
		const string& sentence, 
		coral::phonetics::ParseStrings& postStrings,
		bool& shouldBreak,
		bool& shouldContinue,
		int& insertPosition,
		int& maxInsertCount,
		MiddlewareType& middlewareType) const override;
private:
	std::map<std::string, std::pair<std::string, std::string>> mispronunciation_map_;
};
} // 命名空间 coral::phonetics 结束（批次 2 辅助输入）


#endif // MIDDLEWARES_H

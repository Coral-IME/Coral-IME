/*
* @author: xuxiang
* @date: 2022-5-31 16:12:25
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_SCRIPT_TRANSFORM_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_SCRIPT_TRANSFORM_HPP_INCLUDED

#include "middleware.h"
#include <opencc/Config.hpp>
#include <opencc/Converter.hpp>
#include <opencc/Conversion.hpp>
#include <opencc/ConversionChain.hpp>
#include <opencc/Dict.hpp>
#include <opencc/DictEntry.hpp>
#include "pch.h"
namespace coral::phonetics {
class ScriptConverter {
public:
    ScriptConverter(const string& config_path) {
		// LOG_INFO(hlp::formatString("initializing opencc: %s",  config_path.c_str()));
        opencc::Config config;
        try {
            converter_ = config.NewFromFile(config_path);
            const list<opencc::ConversionPtr> conversions =
                converter_->GetConversionChain()->GetConversions();
            dict_ = conversions.front()->GetDict();
        }
        catch (opencc::Exception& e) {
		    LOGGER_WRITE(hlp::formatString("Error initializing opencc: %s",  e.what()));
        }
    }

    bool convert_word(const string& text, vector<string>& forms) {
		// LOG_INFO(hlp::formatString("ConvertWord: %s",  text.c_str()));
        if (dict_ == nullptr) return false;
        opencc::Optional<const opencc::DictEntry*> item = dict_->Match(text);
        if (item.IsNull()) {
            // 未找到匹配项
            return false;
        }
        else {
            const opencc::DictEntry* entry = item.Get();
            for (auto&& value : entry->Values()) {
                forms.push_back(std::move(value));
            }
            return forms.size() > 0;
        }
    }

    bool convert_text(const string& text, string& simplified) {
        if (converter_ == nullptr) return false;
        simplified = converter_->Convert(text);
		// LOG_INFO(hlp::formatString("ConvertText: %s %s",  text.c_str(), simplified.c_str()));
        return simplified != text;
    }

private:
    opencc::ConverterPtr converter_;
    opencc::DictPtr dict_;
};
} // 命名空间 coral::phonetics 结束（批次 2 辅助输入）


namespace coral::phonetics {
class ScriptTransform : public coral::phonetics::CandidateTransform
{
public:
    ScriptTransform(): coral::phonetics::CandidateTransform(), ptr_opencc_(nullptr) {}
    ~ScriptTransform() {}
    bool initialize(const string& name, const filesystem::path& configPath) {
        bool initialized = false;
        try {
            ptr_opencc_.reset(new coral::phonetics::ScriptConverter(configPath.u8string()));
            name_ = name;
            initialized = true;
        }
        catch (opencc::Exception& e) {
		    LOGGER_WRITE(hlp::formatString("Error initializing opencc: %s",  e.what()));
        }
        return initialized;
    }
	bool apply(const string& text, vector<string>& postTexts) {
		if (!ptr_opencc_)
			return false;
		vector<string> forms;
		bool success = ptr_opencc_->convert_word(text, forms);
		if (success) {
			for (size_t i = 0; i < forms.size(); ++i) {
				if (forms[i] == text) {
					continue;
				}
				else {
					postTexts.push_back(forms[i]);
				}
			}
			return true;
		}
		else {
			string simplified;
			success = ptr_opencc_->convert_text(text, simplified);
			if (success) {
				postTexts.push_back(simplified);
				return true;
			}
		}
		return false;
	};
protected:
    shared_ptr<coral::phonetics::ScriptConverter> ptr_opencc_;
};
} // 命名空间 coral::phonetics 结束（批次 2 辅助输入）


#endif // OPENCCMIDDLEWARE_HPP（OpenCC 中间件头文件）

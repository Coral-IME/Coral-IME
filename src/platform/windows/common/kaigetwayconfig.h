#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_COMMON_KAIGETWAYCONFIG_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_COMMON_KAIGETWAYCONFIG_H_INCLUDED

namespace KAIGetwayConfig
{
    constexpr const char *const AI_TOKEN = "2IEmcPrMnP734kzk0YmSNh87FHNCVjQf";
    constexpr const char *const AI_CORAL_UID = "9036";
    constexpr const char *const AI_PRODUCT_NAME = "coral_aigctest_campusexam";
    constexpr const char *const AI_INTENTION_CODE = "";
    constexpr const char *const AI_HOST_URL = "https://ai-gateway.coral.cn";

    constexpr const char *const AI_CONTEXT = R"(You are a Chinese search suggestion generator. Always return the results in Chinese. 
	The results must be practical, concise, highly relevant to the keyword, and should avoid irrelevant or duplicate content.)";

    constexpr const char *const AI_PROMPT =	u8R"([背景] 你是一个中文输入建议生成器。始终以中文返回结果。结果必须实用、简洁、与关键词高度相关，并应避免不相关或重复的内容。
    [任务] 请为关键词"%1"生成%2个相关联的词语或短语, 要求:
    1. 每个结果都是完整的词语或短语
    2. 结果要具体、实用、相关性强
    3. 返回格式为纯文本，每行一个结果
    4. 不要包含序号、标点符号等额外内容
    5. 结果要符合中文表达习惯
    6. 重要：每个结果必须以"%1"开头，不要返回无关内容
    7. 结果不能重复
    8. 示例：输入"中国人民银行"，应返回"中国人民银行货币政策", "中国人民银行外汇管理"等，而不是"货币政策", "外汇管理")";

    constexpr int AI_RETURN_NUM = 8;

    constexpr const char *const AI_MODEL = "qwen-turbo-latest";
    constexpr const char *const AI_PROVIDER = "ali";

    constexpr float AI_MODEL_TEMPERATURE = 0.05f;
    constexpr float AI_MODEL_MAX_TOKEN = 500.f;
    constexpr float AI_MODEL_TOP_P = 0.9f;
    constexpr float AI_MODEL_TOP_K = 1.f;
}

#endif // KAIGETWAYCONFIG_H

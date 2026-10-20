#include "time_processor.h"

std::string coral::assist::TimeProcessor::number_to_chinese(int number) {
    if (number == 0) return u8"零";
    if (number == 10) {
        return "十";
    }

    std::string result;
    int unitPos = 0;
    while (number > 0) {
        const std::string units[] = { "", u8"十" };

        if (const int digit = number % 10; unitPos == 1 && digit == 1 && number < 20) {
            result = units[unitPos] + result;  // 处理10-19的情况，如十二、十三等
        }
        else
        {
            const std::string chineseDigits[] = { u8"零", u8"一", u8"二", u8"三", u8"四", u8"五", u8"六", u8"七", u8"八", u8"九" };
            if (digit != 0) {
                result = chineseDigits[digit] + units[unitPos] + result;
            }
            else if (!result.empty()) {
                result = chineseDigits[digit] + result;  // 添加“零”在中间
            }
        }

        number /= 10;
        unitPos++;
    }

    return result;
}

bool coral::assist::TimeProcessor::parse_time(const std::string& timeStr,
    std::array<int, 3>& parts, int& count) {
    count = 0;
    std::size_t pos = 0;
    while (pos < timeStr.size() && count < 3) {
        const auto begin = pos;
        int value = 0;
        while (pos < timeStr.size() && timeStr[pos] >= '0' && timeStr[pos] <= '9') {
            if (pos - begin >= 2) return false;
            value = value * 10 + timeStr[pos++] - '0';
        }
        if (pos == begin || pos - begin > 2 || value >= 60) return false;
        parts[count++] = value;
        if (pos == timeStr.size()) break;
        if (timeStr[pos++] != ':' || pos == timeStr.size()) return false;
    }
    return pos == timeStr.size() && (count == 2 || (count == 3 && parts[0] < 24));
}

std::vector<std::string> coral::assist::TimeProcessor::generate_time_formats(
    const std::array<int, 3>& timeParts, int count) {
    std::vector<std::string> result;

    if (count == 2) {
        const int first = timeParts[0];
        const int second = timeParts[1];

        // 阿拉伯数字格式
        if (first < 24) {
            result.push_back(std::to_string(first) + u8"时" + std::to_string(second) + u8"分");
            result.push_back(number_to_chinese(first) + u8"时" + number_to_chinese(second) + u8"分");
        }
     
        result.push_back(std::to_string(first) + u8"分" + std::to_string(second) + u8"秒");
        result.push_back(number_to_chinese(first) + u8"分" + number_to_chinese(second) + u8"秒");
    }
    else if (count == 3) {
        const int hour = timeParts[0];
        const int minute = timeParts[1];
        const int second = timeParts[2];

        // 阿拉伯数字格式
        result.push_back(std::to_string(hour) + u8"时" + std::to_string(minute) + u8"分" + std::to_string(second) + u8"秒");

        // 中文数字格式
        result.push_back(number_to_chinese(hour) + u8"时" + number_to_chinese(minute) + u8"分" + number_to_chinese(second) + u8"秒");
    }

    return result;
}

bool coral::assist::TimeProcessor::process(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents)
{
    const std::string parseStr = input.substr(1);
    std::array<int, 3> parts{};
    int count = 0;
    if (parse_time(parseStr, parts, count))
    {
        std::vector<std::string> formats = generate_time_formats(parts, count);
        for (const auto& format : formats) {
            coral::phonetics::CandidateResult sln;
            sln.sentence = format;
            sln.pinyin.push_back(input);
            sents.push_back(std::move(sln));
        }
        return true;
    }
    else
    {
        return false;
    }
}

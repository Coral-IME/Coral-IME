#include "date_processor.h"
#include "calendar/convertcalendar.h"

namespace
{
    constexpr std::array<std::string_view, 10> chineseTraDigits = { u8"〇", u8"壹", u8"贰", u8"叁", u8"肆", u8"伍", u8"陆", u8"柒", u8"捌", u8"玖" };
    constexpr std::array<std::string_view, 10> chineseDigits = { u8"〇", u8"一", u8"二", u8"三", u8"四", u8"五", u8"六", u8"七", u8"八", u8"九" };
    constexpr std::array<std::string_view, 11> simpleDigits = { "", u8"一", u8"二", u8"三", u8"四", u8"五", u8"六", u8"七", u8"八", u8"九", u8"十" };
    constexpr std::array<std::string_view, 11> traditionalDigits = { "", u8"壹", u8"贰", u8"叁", u8"肆", u8"伍", u8"陆", u8"柒", u8"捌", u8"玖", u8"拾" };
}


bool coral::assist::DateProcessor::process(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) {
    std::vector<std::string> convertedDates = convert_date(input);

    if (!convertedDates.empty()) {
        for (const auto& date : convertedDates) {
            coral::phonetics::CandidateResult sol;
            sol.sentence = date;
            sol.pinyin.push_back(input);
            sol.probability = 1;
            sents.push_back(std::move(sol));
        }
        return true;
    }
    return false;
}

std::vector<std::string> coral::assist::DateProcessor::convert_date(const std::string& input) {
    if (input.size() < 4 || input.size() > 16) return {};
    const std::string_view body(input.data() + 1, input.size() - 1);
    DateParts parts{};
    std::size_t cursor = 0;
    const auto field = [&](int index, std::size_t maxDigits) {
        const auto begin = cursor;
        while (cursor < body.size() && body[cursor] >= '0' && body[cursor] <= '9') ++cursor;
        if (cursor == begin || cursor - begin > maxDigits) return false;
        parts[index] = std::string(body.substr(begin, cursor - begin));
        return true;
    };
    if (!field(1, 4) || cursor == body.size()) return {};
    const char delimiter = body[cursor++];
    if (!field(2, 2)) return {};
    if (delimiter == '.' || delimiter == '-' || delimiter == '/') {
        if (cursor == body.size()) return process_year_month_format(parts);
        if (body[cursor++] != delimiter || !field(3, 2) || cursor != body.size()) return {};
        return process_full_format(parts);
    }
    if (delimiter == 'y' && cursor + 1 == body.size() && body[cursor] == 'r' &&
        parts[1].size() <= 2) return process_month_day_format(parts);
    if (delimiter != 'n' || cursor == body.size() || body[cursor++] != 'y') return {};
    if (cursor == body.size()) return process_year_month_format(parts);
    if (!field(3, 2) || cursor + 1 != body.size() || body[cursor] != 'r') return {};
    return process_full_format(parts);
}

std::vector<std::string> coral::assist::DateProcessor::process_month_day_format(const DateParts& match) {
    const std::string year = "2020";
    const std::string month = match[1];
    const std::string day = match[2];
    std::vector<std::string> candidates;
    if (is_valid_date(year, month, day)) {
        candidates.push_back(month + u8"月"+day + u8"日");
        std::string chineseMonth = convert_number_to_chinese(std::stoi(month), simpleDigits,false);
        std::string chineseDay = convert_number_to_chinese(std::stoi(day), simpleDigits,false);
        candidates.push_back(chineseMonth + u8"月" + chineseDay + u8"日");
        chineseMonth = convert_number_to_chinese(std::stoi(month), traditionalDigits,true);
        chineseDay = convert_number_to_chinese(std::stoi(day), traditionalDigits,true);
        candidates.push_back(chineseMonth + u8"月" + chineseDay + u8"日");

    }
    return candidates;
}

std::vector<std::string> coral::assist::DateProcessor::process_year_month_format(const DateParts& match) {
    std::vector<std::string> candidates;
    std::string year = match[1];
    std::string month = match[2];
    std::string day = "20";
    if (is_valid_date(year, month, day))
    {
        candidates.push_back(year + u8"年" + month + u8"月");
        std::string chineseYear = to_chinese_number(std::stoi(year), chineseTraDigits);
        std::string chineseMonth = convert_number_to_chinese(std::stoi(month),traditionalDigits,true);
        candidates.push_back(chineseYear + u8"年" + chineseMonth + u8"月");
        chineseYear = to_chinese_number(std::stoi(year), chineseDigits);
        chineseMonth = convert_number_to_chinese(std::stoi(month), simpleDigits,false);
        candidates.push_back(chineseYear + u8"年" + chineseMonth + u8"月");
    }
    year = "2020";
    month = match[1];
    day = match[2];
    if (is_valid_date(year, month, day))
    {
        candidates.push_back(month + u8"月" + day + u8"日");
        std::string chineseMonth = convert_number_to_chinese(std::stoi(month), simpleDigits, false);
        std::string chineseDay = convert_number_to_chinese(std::stoi(day), simpleDigits, false);
        candidates.push_back(chineseMonth + u8"月" + chineseDay + u8"日");
        chineseMonth = convert_number_to_chinese(std::stoi(month), traditionalDigits, true);
        chineseDay = convert_number_to_chinese(std::stoi(day), traditionalDigits, true);
        candidates.push_back(chineseMonth + u8"月" + chineseDay + u8"日");

    }
    return candidates;
}

std::vector<std::string> coral::assist::DateProcessor::process_full_format(const DateParts& match) {
    std::vector<std::string> candidates;
    std::string year = match[1];
    std::string month = match[2];
    std::string day = match[3];
    if (is_valid_date(year, month, day)) 
    {
        candidates.push_back(year + u8"年" + month + u8"月" + day + u8"日");    
        candidates.push_back(convert_to_chinese_date(year, month, day,false)); 
        candidates.push_back(convert_to_chinese_date(year, month, day, true)); 
        if (std::stoi(year) > 1900 && std::stoi(year) < 1900 + static_cast<int>(std::size(Calendar::CHINESEYEARCODE)))
        {
            candidates.push_back(convert_to_lunar(year, month, day));
        }
        std::string weekday = convert_to_week(year, month, day);
        if (weekday != "")
        {
            candidates.push_back(std::move(weekday));
        }
       
    }
    return candidates;
}

bool coral::assist::DateProcessor::is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

bool coral::assist::DateProcessor::is_valid_date(const std::string& year, const std::string& month, const std::string& day)
{
    if (std::stoi(year) < 1 || std::stoi(month) < 1 || std::stoi(month) > 12 || std::stoi(day) < 1) {
        return false;
    }

    // 根据每个月的天数进行校验
    int daysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    // 闰年检查（如果是闰年，二月份的天数为29）
    if (std::stoi(month) == 2 && is_leap_year(std::stoi(year)) )
    {
        daysInMonth[1] = 29;
    }

    if (std::stoi(day) > daysInMonth[std::stoi(month) - 1]) {
        return false;
    }

    return true;
}

template <size_t N>
std::string coral::assist::DateProcessor::to_chinese_number(int number, const std::array<std::string_view, N>& yearstr)
{
    if (number == 0) return std::string(yearstr[0]);

    std::string result;
    while (number > 0) {
        int digit = number % 10;
        result = std::string(yearstr[digit]) + result;
        number /= 10;
    }

    return result;
}

template <size_t N>
std::string coral::assist::DateProcessor::convert_number_to_chinese(int num, const std::array<std::string_view, N>& numstr, bool isTra) {
    if (num < 10)
    {
        return std::string(numstr[num]);
    }
    if (num == 10)
    {
        if (isTra)
        {
            return std::string(numstr[1]) + std::string(numstr[10]);
        }
        else
        {
            return std::string(numstr[10]);
        }
    }
    int tens = num / 10;
    int ones = num % 10;
    std::string result;
    if (!isTra)
    {
        result = (tens == 1) ? std::string(numstr[10]) : std::string(numstr[tens]) + std::string(numstr[10]);
    }
    else
    {
        result = std::string(numstr[tens]) + std::string(numstr[10]);
    }

    if (ones > 0) result += std::string(numstr[ones]);

    return result;
}

std::string coral::assist::DateProcessor::convert_to_chinese_date(const std::string& year, const std::string& month, const std::string& day, bool isTra) {
    if (isTra)
    {
        std::string chineseYear = to_chinese_number(std::stoi(year), chineseTraDigits);
        std::string chineseMonth = convert_number_to_chinese(std::stoi(month), traditionalDigits,true);
        std::string chineseDay = convert_number_to_chinese(std::stoi(day), traditionalDigits,true);
        return chineseYear + u8"年" + chineseMonth + u8"月"+chineseDay + u8"日";

    }
    else
    {
        std::string chineseYear = to_chinese_number(std::stoi(year), chineseDigits);
        std::string chineseMonth = convert_number_to_chinese(std::stoi(month), simpleDigits,false);
        std::string chineseDay = convert_number_to_chinese(std::stoi(day), simpleDigits,false);
        return chineseYear + u8"年" + chineseMonth+ u8"月" + chineseDay + u8"日";
    }
}

std::string coral::assist::DateProcessor::convert_to_week(const std::string& year, const std::string& month, const std::string& day) {
    // 将字符串转换为整数
    int y = std::stoi(year);
    int m = std::stoi(month);
    int d = std::stoi(day);

    // 创建 tm 结构体
    std::tm date = {};
    date.tm_year = y - 1900; // 年份，减去 1900
    date.tm_mon = m - 1;     // 月份，从 0 开始
    date.tm_mday = d;        // 日期

    time_t t = mktime(&date);
    if (t == -1) {
        return "";
    }

    std::string dayOfWeek = get_day_of_week(date);
    return dayOfWeek;
}

std::string coral::assist::DateProcessor::get_day_of_week(const std::tm& date) {
    // 获取星期几，tm_wday 是星期几的索引（0-6），其中 0 代表周日
    switch (date.tm_wday) {
        case 0: return u8"星期日";
        case 1: return u8"星期一";
        case 2: return u8"星期二";
        case 3: return u8"星期三";
        case 4: return u8"星期四";
        case 5: return u8"星期五";
        case 6: return u8"星期六";
        default: return "Unknown";
    }
}

std::string coral::assist::DateProcessor::convert_to_lunar(const std::string& year, const std::string& month, const std::string& day)
{
    uint8_t y = static_cast<uint8_t>(std::stoi(year) - 1900); // Date类存储的是相对1900年的年数
    uint8_t m = static_cast<uint8_t>(std::stoi(month) - 1);   // Date类的月份范围是[0,11]
    uint8_t d = static_cast<uint8_t>(std::stoi(day));         // 日期

    // 创建Date对象并设置为公历日期
    Calendar::Date date(y, m, d);

    // 将公历日期转换为农历日期
    date.nongli();

    // 返回农历日期的字符串表示
    return u8"农历" + date.toStr();
}

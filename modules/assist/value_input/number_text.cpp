#include "number_text.h"
#include "number_syntax.h"
#include <array>
#include <string_view>
#include <unordered_map>

coral::assist::NumberProcessor::NumberProcessor() {

}

coral::assist::NumberProcessor::~NumberProcessor() {

}

bool coral::assist::NumberProcessor::process(const std::string& input,  std::vector<coral::phonetics::CandidateResult>& sents) {
    const std::string numString = input.substr(1);
    constexpr int romaMax = 3999;

    coral::uv::DecimalParts parts;
    if (coral::uv::decimal_parts(numString, parts)) {
        const bool flag = parts.fraction.empty();
        unsigned num = 0;
        if (flag && parts.integer.size() <= 4)
            for (char c : parts.integer) num = num * 10 + c - '0';
        if (flag && num < romaMax && num > 0) {
            coral::phonetics::CandidateResult Roma;
            Roma.sentence = int_to_roma(num);
            Roma.pinyin.push_back(input);
            sents.push_back(std::move(Roma));
        }

        coral::phonetics::CandidateResult Chinese;
        Chinese.sentence = number_to_chinese(numString);
        Chinese.probability = static_cast<coral::phonetics::Score>(0.9);
        Chinese.pinyin.push_back(input);

        coral::phonetics::CandidateResult capitalChinese;
        capitalChinese.sentence = number_to_capital_chinese(numString);
        capitalChinese.probability = static_cast<coral::phonetics::Score>(0.8);
        capitalChinese.pinyin.push_back(input);

        
        std::string str = ChineseLowercase(numString);
        if (str != Chinese.sentence) {
            coral::phonetics::CandidateResult Lowercase;
            Lowercase.sentence = str;
            Lowercase.probability = static_cast<coral::phonetics::Score>(0.7);
            Lowercase.pinyin.push_back(input);
            sents.push_back(std::move(Lowercase));
        }
        
        str = ChineseCapitalization(numString);
        if (str != capitalChinese.sentence) {
            coral::phonetics::CandidateResult capitalization;
            capitalization.sentence = str;
            capitalization.probability = static_cast<coral::phonetics::Score>(0.6);
            capitalization.pinyin.push_back(input);
            sents.push_back(std::move(capitalization));
        }

        // 下面两个对象用完再移动
        sents.push_back(std::move(Chinese));
        sents.push_back(std::move(capitalChinese));

        if ((parts.integer.size() == 1 || parts.integer.front() != '0') && parts.fraction.size() <= 2) {
            coral::phonetics::CandidateResult rmb;
            rmb.sentence = lowercase_rmb(numString);
            rmb.pinyin.push_back(input);

            coral::phonetics::CandidateResult RMB;
            RMB.sentence = capital_rmb(numString);
            RMB.pinyin.push_back(input);

            if (!flag) {
                rmb.probability = static_cast<coral::phonetics::Score>(1.0);
                RMB.probability = static_cast<coral::phonetics::Score>(1.0);
            }
            sents.push_back(std::move(rmb));
            sents.push_back(std::move(RMB));
        }

        solution_sort(sents);
    }
    else {
        return false;
    }

    return true;
}

//判断是否是纯数字或小数
bool coral::assist::NumberProcessor::is_number(const std::string& input) {
    coral::uv::DecimalParts parts;
    return coral::uv::decimal_parts(input, parts);
}

// 无小数点的时候 优先级如下函数定义顺序
// 有小数点的时候 小写+元+角+分 > 大写+元+角+分 > 中文小写 > 中文大写 > 单纯转为中文小写 > 单纯转为中文小写
// 转为中文小写 v123.33 一百二十三点三三 （注意0开头的情况）
std::string coral::assist::NumberProcessor::number_to_chinese(const std::string& numString) {
    std::string res = "";
    const size_t dotPos = numString.find('.');
    const std::string integerPart = (dotPos == std::string::npos) ? numString : numString.substr(0, dotPos);
    const std::string fractionalPart = (dotPos == std::string::npos) ? "" : numString.substr(dotPos + 1);

    std::string integerChinese = convert_integer_to_chinese(integerPart);
    const std::string fractionalChinese = convert_fraction_to_chinese(fractionalPart);
    if (fractionalPart.empty()) {
        return integerChinese;
    }
    res = integerChinese + u8"点" + fractionalChinese;
    return res;
}

// 转为中文大写 v123.33 壹佰贰拾叁点叁叁
std::string coral::assist::NumberProcessor::number_to_capital_chinese(const std::string& numString) {
    static const std::unordered_map<std::string, std::string> conversion_map = {
                {u8"〇", u8"零"},
                {u8"一", u8"壹"},
                {u8"二", u8"贰"},
                {u8"三", u8"叁"},
                {u8"四", u8"肆"},
                {u8"五", u8"伍"},
                {u8"六", u8"陆"},
                {u8"七", u8"柒"},
                {u8"八", u8"捌"},
                {u8"九", u8"玖"}
    };
    const std::string ten_char = u8"十";
    const std::string number = number_to_chinese(numString);
    std::string res = u8"";

    if (number.substr(0, ten_char.size()) == ten_char) {
        res += u8"壹";
    }

    size_t pos = 0;
    while (pos < number.length()) {
        bool found = false;
        for (size_t len = 1; len <= 3 && pos + len <= number.length(); ++len) {
            std::string substring = number.substr(pos, len);
            if (conversion_map.find(substring) != conversion_map.end()) {
                res += conversion_map.at(substring);
                pos += len;
                found = true;
                break;
            }
        }
        if (!found) {
            res += number[pos];
            ++pos;
        }
    }
    return res;
}

// 单纯转为中文小写  v123.33 一二三点三三
std::string coral::assist::NumberProcessor::ChineseLowercase(const std::string& numString) {
    static const std::string digits[] = { u8"〇", u8"一", u8"二", u8"三", u8"四", u8"五", u8"六", u8"七", u8"八",u8"九" };
    std::string res = u8"";
    for (const auto i : numString) {
        if (i == '.') {
            res = res + u8"点";
        }
        else {
            res = res + digits[i - '0'];
        }
    }
    // std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return res;
}

// 单纯转为中文大写  v123.33 壹贰叁点叁叁
std::string coral::assist::NumberProcessor::ChineseCapitalization(const std::string& numString) {
    static const std::string digits[] = { u8"零", u8"壹", u8"贰", u8"叁", u8"肆", u8"伍", u8"陆", u8"柒", u8"捌", u8"玖" };
    std::string res = u8"";
    for (const auto i : numString) {
        if (i == '.') {
            res = res + u8"点";
        }
        else {
            res = res + digits[i - '0'];
        }
    }
    // std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    // return converter.to_bytes(res);
    return res;
}

// 转为中文小写+元+角+分  v123.33 一百二十三元三角三分（小数位数 <=2 ）
std::string coral::assist::NumberProcessor::lowercase_rmb(const std::string& numString) {
    static const std::vector<std::string> chn_str = { u8"〇", u8"一", u8"二", u8"三", u8"四", u8"五", u8"六", u8"七", u8"八", u8"九" };
    std::string res = u8"";

    const size_t dotPos = numString.find('.');
    const std::string integerPart = (dotPos == std::string::npos) ? numString : numString.substr(0, dotPos);
    const std::string fractionalPart = (dotPos == std::string::npos) ? "" : numString.substr(dotPos + 1);

    const std::string integerChinese = convert_integer_to_chinese(integerPart);

    res = integerChinese + u8"元";
    if (fractionalPart.empty() || fractionalPart.length() > 2) {
        return res;
    }
    if (fractionalPart.length() == 1) {
        res = res + chn_str[fractionalPart[0] - '0'] + u8"角";
    }
    else {
        res = res + chn_str[fractionalPart[0] - '0'] + u8"角" + chn_str[fractionalPart[1] - '0'] + u8"分";
    }
    return res;
}

// 转为中文大写+元+角+分  v123.33 壹佰贰拾叁元叁角叁分（小数位数 <=2 ）
std::string coral::assist::NumberProcessor::capital_rmb(const std::string& numString) {
    std::string res = u8"";
    const std::string rmb = lowercase_rmb(numString);
    if (rmb == u8"") {
        return u8"";
    }
    static const std::unordered_map<std::string, std::string> conversion_map = {
            {u8"〇", u8"零"},
            {u8"一", u8"壹"},
            {u8"二", u8"贰"},
            {u8"三", u8"叁"},
            {u8"四", u8"肆"},
            {u8"五", u8"伍"},
            {u8"六", u8"陆"},
            {u8"七", u8"柒"},
            {u8"八", u8"捌"},
            {u8"九", u8"玖"}
    };

    const std::string ten_char = u8"十";
    if (rmb.substr(0, ten_char.size()) == ten_char) {
        res += u8"壹";
    }
   
    size_t pos = 0;
    while (pos < rmb.length()) {
        bool found = false;
        for (size_t len = 1; len <= 3 && pos + len <= rmb.length(); ++len) {
            std::string substring = rmb.substr(pos, len);
            if (conversion_map.find(substring) != conversion_map.end()) {
                res += conversion_map.at(substring);
                pos += len;
                found = true;
                break;
            }
        }
        if (!found) {
            res += rmb[pos];
            ++pos;
        }
    }
    return res;
}

// 转为罗马 v123.3 CXXIII
std::string coral::assist::NumberProcessor::int_to_roma(long long num) {
    static const std::string thousands[] = { "", "M", "MM", "MMM" };
    static const std::string hundreds[] = { "", "C", "CC", "CCC", "CD", "D", "DC", "DCC", "DCCC", "CM" };
    static const std::string tens[] = { "", "X", "XX", "XXX", "XL", "L", "LX", "LXX", "LXXX", "XC" };
    static const std::string ones[] = { "", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX" };

    return thousands[num / 1000] + hundreds[num % 1000 / 100] + tens[num % 100 / 10] + ones[num % 10];
}

std::string coral::assist::NumberProcessor::convert_integer_to_chinese(const std::string& numString) {
    static const std::vector<std::string> chn_str = { u8"〇", u8"一", u8"二", u8"三", u8"四", u8"五", u8"六", u8"七", u8"八", u8"九" };
    static const std::vector<std::string> section_value = { u8"", u8"万", u8"亿", u8"万亿", u8"亿亿", u8"万亿亿", u8"亿亿亿",
                                                u8"万亿亿亿", u8"亿亿亿亿", u8"万亿亿亿亿", u8"亿亿亿亿亿", u8"万亿亿亿亿亿",
                                                u8"亿亿亿亿亿亿", u8"万亿亿亿亿亿亿",u8"亿亿亿亿亿亿亿", u8"万亿亿亿亿亿亿亿",
                                                u8"亿亿亿亿亿亿亿亿" }; // 节权
    static const std::vector<std::string> unit_value = { u8"", u8"十", u8"百", u8"千" }; // 位权

    const std::string zero_char = u8"〇";
    std::string result = u8"";
    const int len = numString.length();
    const int sectionSize = 4; // 每节4位
    const int sectionCount = (len + sectionSize - 1) / sectionSize; // 计算节数

    for (int i = 0; i < sectionCount; ++i) {
        int start = len - (i + 1) * sectionSize;
        const int end = len - i * sectionSize;
        if (start < 0) {
            start = 0;
        }

        std::string section = numString.substr(start, end - start);
        std::string sectionResult = u8"";

        const int num_section = std::stoi(section);
        if (num_section == 0) {
            if (!result.empty() && result.substr(0, zero_char.size()) != chn_str[0]) {
                result = chn_str[0] + result;
            }
            continue;
        }

        for (int j = section.length() - 1; j >= 0; --j) {
            const int digit = section[j] - '0';
            if (digit == 0) {
                if (!sectionResult.empty() &&
                    /*result.substr(0, zero_char.size()) != chn_str[0] &&*/
                    sectionResult.substr(0, zero_char.size()) != chn_str[0]) {
                    sectionResult = u8"〇" + sectionResult;
                }
            }
            else {
                sectionResult = chn_str[digit] + unit_value[section.length() - j - 1] + sectionResult;
            }
        }
        if (!sectionResult.empty()) {
            result = sectionResult + section_value[i] + result;
        }
    }

    // 零开头
    while (!result.empty() && result.substr(0, zero_char.size()) == chn_str[0]) {
        result = result.substr(zero_char.size());
    }

    // 零结尾
    // if(result.size() > zero_char.size() && result.substr(result.size() - zero_char.size()) == chn_str[0]) {
    // result = result.substr(0, result.size() - zero_char.size());
    //}

    if (!result.empty()
        && result.substr(0, zero_char.size()) == chn_str[1]
        && result.substr(zero_char.size(), zero_char.size()) == unit_value[1]) {
        result = result.substr(zero_char.size());
    }

    if (result.empty()) {
        return u8"〇";
    }

    return result;
}

std::string coral::assist::NumberProcessor::convert_fraction_to_chinese(const std::string& fractionalPart) {
    static const std::vector<std::string> chn_str = { u8"〇", u8"一", u8"二", u8"三", u8"四", u8"五", u8"六", u8"七", u8"八", u8"九" };
    std::string res = u8"";
    for (const auto i : fractionalPart) {
        res += chn_str[i - '0'];
    }
    return res;
}

// 对优先级排序
void coral::assist::NumberProcessor::solution_sort(std::vector<coral::phonetics::CandidateResult>& sents) {
    std::sort(sents.begin(), sents.end(), [](const coral::phonetics::CandidateResult& a, const coral::phonetics::CandidateResult& b) {
        return a.probability > b.probability;
    });
}

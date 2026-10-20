#include "arithmetic_processor.h"
#include <iostream>
#include <sstream>
#include <string>
#include <cmath>
#include <iomanip>
#include "../number_syntax.h"

// 去除数字字符串末尾多余的0和小数点
std::string trimTrailingZeros(const std::string& numStr)
{
	if (numStr.find('.') == std::string::npos) {
		return numStr;
	}
	
	std::string result = numStr;
	// 去除末尾的0
	while (!result.empty() && result.back() == '0') {
		result.pop_back();
	}
	// 如果最后是小数点，也去掉
	if (!result.empty() && result.back() == '.') {
		result.pop_back();
	}
	return result;
}

// 格式化为百分比
std::string formatPercentage(double value)
{
	std::ostringstream out;
	out << std::fixed << std::setprecision(2) << (value * 100) << "%";
	return out.str();
}

bool coral::assist::ArithmeticProcessor::process(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents)
{
	const std::string expr = input.substr(1);
	BigFloat ans;
	const bool ok = expr_calcu_.compute(expr, ans);
	if (!ok)
		return false;
	
	// 获取结果字符串并去除末尾多余的0
	std::string resultStr = trimTrailingZeros(ans.to_string());
	
	coral::phonetics::CandidateResult ansSln;
	ansSln.sentence = resultStr;
	ansSln.pinyin.push_back(input);
	sents.push_back(std::move(ansSln));
	
	// 检测是否是简单除法运算（简单除法：数字/数字）
    const auto slash = expr.find('/');
    coral::uv::DecimalParts numerator, denominator;
    const bool isSimpleDivision = slash != expr.npos &&
        coral::uv::decimal_parts(std::string_view(expr).substr(0, slash), numerator) &&
        coral::uv::decimal_parts(std::string_view(expr).substr(slash + 1), denominator);
	if (isSimpleDivision) {
		coral::phonetics::CandidateResult percentSln;
		double resultValue = ans.to_double();
		percentSln.sentence = formatPercentage(resultValue);
		percentSln.pinyin.push_back(input);
		sents.push_back(std::move(percentSln));
	}
	
	coral::phonetics::CandidateResult exprSln;
	exprSln.sentence = expr + "=" + resultStr;
	exprSln.pinyin.push_back(input);
	sents.push_back(std::move(exprSln));

	return true;
}

/*
* @author: huangzhishou
* @date: 2021-6-9 10:27:54
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_SYLLABLE_HELPERS_H_INCLUDED
#define CORAL_MODULES_PHONETICS_SYLLABLE_HELPERS_H_INCLUDED

#if defined(_WIN32) || defined(_MSC_VER)
#include <Sddl.h>
#endif

#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <vector>
#include <array>
#include <set>
#include "text_file_helpers.hpp"
#include "phonetic_types.h"
#include "syllable_catalog.h"
#include <string_view>
#include <regex>
#include <filesystem>
#include <string>
#include <sstream>

using syllable_strview_map = std::unordered_map<std::string_view, int>;
constexpr size_t HASH_BINARY_SIZE = 16;  // MD5二进制哈希长度
constexpr size_t HASH_HEX_LENGTH = 32;   // MD5十六进制字符串长度

namespace coral::phonetics::syntax {
using std::string;
using std::string_view;
using std::vector;
using std::set;
using std::unordered_map;
using std::unordered_set;
using std::regex;
using std::regex_match;

	// 检测程序是否在沙箱环境中运行
#if defined(_WIN32) || defined(_MSC_VER)
	inline bool checkIsRunningInAppContainer()
	{
		HANDLE hToken = nullptr;
		// 获取当前进程的访问令牌句柄
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
			// 如果无法获取令牌，出于安全考虑，假定不是UWP进程
			return false;
		}

		BOOL isAppContainer = FALSE;
		DWORD returnLength = 0;
		// 查询令牌信息，专门查询 TokenIsAppContainer 这一项
		if (GetTokenInformation(hToken, TokenIsAppContainer, &isAppContainer, sizeof(isAppContainer), &returnLength)) {
			CloseHandle(hToken);
			// 如果 isAppContainer 非零，则说明是在AppContainer中运行
			return isAppContainer != 0;
		}

		// 如果API调用失败（例如在不支持AppContainer的老系统上），也返回false
		CloseHandle(hToken);
		return false;
	}
#endif
	inline bool is_jian_pin(const string_view pinyin)
	{
		// 硬编码合理的简拼可能
		static constexpr std::array<std::string_view, 83> jianPins =
		{
			"b", "be", "bia", "c", "ch", "cho", "chon", "co", "con", "cua",
			"d", "din", "do", "don", "dua", "f", "fe", "g", "go", "gon",
			"h", "ho", "hon", "j", "jio", "jion", "jua", "k", "ko", "kon",
			"l", "len", "lo", "lon", "lua", "m", "mia", "n", "nia", "no",
			"non", "nua", "p", "pe", "pia", "q", "qio", "qion", "qua", "r",
			"ra", "ro", "ron", "rua", "s", "sh", "sho", "so", "son", "sua",
			"t", "ten", "tia", "tin", "to", "ton", "tua", "w", "we", "x",
			"xio", "xion", "xua", "y", "yon", "yua", "z", "zh", "zho", "zhon",
			"zo", "zon", "zua"
		};

		return std::binary_search(jianPins.begin(), jianPins.end(), pinyin);
	}

	inline std::vector<string> split_pinyin_string(const std::string_view pinyinStr) {
		std::vector<string> pinyinVec;
		pinyinVec.reserve(pinyinStr.length() / 2 + 1);
		size_t start = 0, pos = 0;
		while ((pos = pinyinStr.find('\'', start)) != std::string_view::npos) {
			pinyinVec.emplace_back(pinyinStr.substr(start, pos - start));
			start = pos + 1;
		}
		if (start < pinyinStr.size()) {
			pinyinVec.emplace_back(pinyinStr.substr(start));
		}
		return pinyinVec;
	}

	inline bool is_sheng_mu(const string_view& pinyin) 
	{
		static constexpr std::array<std::string_view, 23> jianPins_sorted = {
	   "b", "c", "ch", "d", "f", "g", "h", "j", "k", "l", "m", "n",
	   "p", "q", "r", "s", "sh", "t", "w", "x", "y", "z", "zh"
		};

		return std::binary_search(jianPins_sorted.begin(), jianPins_sorted.end(), pinyin);
	}

	// 判定用的都是 ASCII 字母，直接比范围：locale 版 isupper 对负值 char 是未定义行为，
	// 且这个函数在英文词判定里每个候选都会走一次
	inline bool is_ascii_upper(const char ch) noexcept
	{
		return ch >= 'A' && ch <= 'Z';
	}

	inline bool is_english_style_pinyin(const string_view& pinyin)
	{
		int upperCount = 0, lowerCount = 0;
		bool isFirstUpper = is_ascii_upper(pinyin[0]);
		for (size_t i = 1; i < pinyin.size(); ++i)
		{
			if (is_ascii_upper(pinyin[i]))
				++upperCount;
			else
				++lowerCount;
		}
		if (upperCount + isFirstUpper == static_cast<int>(pinyin.size()) || lowerCount + 1 == static_cast<int>(pinyin.size()))
			return true;
		return false;
	}

	inline bool is_yun_mu(const string_view& pinyin) 
	{
		static constexpr std::array<std::string_view, 39> yunmu_sorted = {
			"a", "ai", "an", "ang", "ao",
			"e", "ei", "en", "eng", "er",
			"i", "ia", "ian", "iang", "iao", "ie", "in", "ing", "iong", "iu",
			"o", "ong", "ou",
			"u", "ua", "uai", "uan", "uang", "ui", "un", "uo",
			"v", "van", "ve", "vn",  // ü 用 v 表示
		};

		return std::binary_search(yunmu_sorted.begin(), yunmu_sorted.end(), pinyin);
	}

	// 检查声母+韵母组合是否能构成合法的完整拼音
	// 例如：c + e = ce (合法)，s + h ≠ sh (不合法，sh是独立声母)
	inline bool is_valid_full_pinyin(const string_view& pinyin)
	{
		return validPinyinCombines.find(pinyin) != validPinyinCombines.end();
	}

	inline bool is_yuanyin(const string_view& pinyin)
	{
		static constexpr std::array<std::string_view, 3> yuanyin = {"a","e","o"};
		return std::any_of(yuanyin.begin(), yuanyin.end(),
			[&pinyin](const std::string_view& v) {
				return pinyin == v;
			}
		);
	}

	inline bool is_fu_yuanyin(const string_view& pinyin)
	{
		static constexpr std::array<std::string_view, 3> fuYuanyin = {"i","u","v"};
		return std::any_of(fuYuanyin.begin(), fuYuanyin.end(),
			[&pinyin](const std::string_view& v) {
				return pinyin == v;
			}
		);
	}

	inline bool is_alphabet(const string_view& pinyin)
	{
		if (pinyin.empty()) {
			return false;
		}
		return std::all_of(pinyin.begin(), pinyin.end(),
			[](unsigned char c) {
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
			}
		);
	}

	// 同时适配 stringViewVec（arena）与 stringViewList（默认分配器）
	template <typename StringViewSeq>
	inline bool  is_alphabet(const StringViewSeq& vecStringView, int index) {
		if (vecStringView.empty()) {
			return false;
		}
		if (index < 0 || index >= static_cast<int>(vecStringView.size())) {
			return false;
		}
		return is_alphabet(vecStringView[index]);
	}

	inline bool get_jian_pin_code(const string_view pinyin, string_view& jianPin, bool filterYuanyin = false)
	{
		if (pinyin.empty() || (int)pinyin.length() > kPinyinMaxLen) {
			return false;
		}
		string_view firstChar = pinyin.substr(0, 1);
		if (is_jian_pin(firstChar))
		{
			jianPin = firstChar;
			return true;
		}

		if (!filterYuanyin)
		{
			if (is_yuanyin(firstChar))
			{
				jianPin = firstChar;
				return true;
			}
		}
		return false;
	}

	inline std::string binary_to_hex(const char* buffer) {
		std::stringstream ss;
		ss << std::hex << std::setfill('0');
		for (size_t i = 0; i < HASH_BINARY_SIZE; ++i) {
			ss << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(buffer[i]));
		}
		return ss.str();
	}

	inline std::string extract_hash_from_filename(const std::filesystem::path& filePath) {
		const std::string stem = filePath.stem().string();
		const std::regex re(R"(_([a-fA-F0-9]{)" + std::to_string(HASH_HEX_LENGTH) + R"(})$)");
		if (std::smatch match; std::regex_search(stem, match, re) && match.size() > 1) {
			std::string hash = match[1].str();
			// 统一转换为小写
			std::transform(hash.begin(), hash.end(), hash.begin(),
						   [](const unsigned char c){ return std::tolower(c); });
			return hash;
		}
		return ""; // 未找到或格式不匹配
	}

	enum class CorrectionType
	{
		INSERTION,		// 插入
		DELETION,		// 删除
		SWAP,			// 交换相邻位置
		REPLACEMENT,	// 替换邻接字母
		EXTRA_RULE,		// 其它规则
		NONE,			// 无
	};

	struct CorrectionMethod
	{
		CorrectionType type = CorrectionType::INSERTION;// 纠错类型
		int operateIndex = 0;				// 针对纠错操作的下标插入，删除和替换的位置交换，其它规则的起点交换的终点就是起点的后一个位置
		char operateAlphabet{};					// 插入删除或者替换的字母
		std::vector<std::pair<int, int>> operateIndexRange;	// 针对其它规则的起点和终点数组
		std::vector<std::string> operateAlphabetString;				// 针对其它规则的替换字符串
		float charTrigramScore = 0.f;								// 字母三元语言模型得分
	};

	struct CorrectionResult
	{
		std::vector<std::string> result;
		float score = 0.0f;
		CorrectionMethod method;
	};

	const std::unordered_set<std::string_view> kSoftInitials = {
		"b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h", "j", "q", "x", "r", "z", "c", "s", "y", "w",
		"zh", "ch", "sh"
	};

	const std::unordered_set<std::string_view> ALL_PINYIN_INITIALS_STRICT = {
		"b", "p", "m", "f", "d", "t", "l", "k", "h", "j", "q", "x", "r", "z", "c", "s", "y", "w",
		"zh", "ch", "sh"
	};

	inline bool is_valid_pinyin_prefix(const std::string_view& pinyin) 
	{
		return std::any_of(validPinyinCombines.begin(), validPinyinCombines.end(),
			[&pinyin](const auto& validPinyin) {
				return pinyin.size() <= validPinyin.size()
					&& pinyin == validPinyin.substr(0, pinyin.size());
			});
	}

	inline bool has_consecutive_initials(const std::string& pinyin) {
		std::string tempStr;
	    for (const char c : pinyin) {
	        if (std::isalpha(c)) {
	            tempStr += c;
	        }
	    }
	    if (tempStr.length() < 2) {
	        return false;
	    }

	    for (size_t i = 0; i < tempStr.length(); ++i) {
	        std::string initialFirst;
	        int lenFirst = 0;

	        if (i + 1 < tempStr.length() && ALL_PINYIN_INITIALS_STRICT.count(tempStr.substr(i, 2))) {
	            initialFirst = tempStr.substr(i, 2);
	            lenFirst = 2;
	        }
	        else if (ALL_PINYIN_INITIALS_STRICT.count(tempStr.substr(i, 1))) {
	            initialFirst = tempStr.substr(i, 1);
	            lenFirst = 1;
	        }

	        if (lenFirst > 0) {
	            const size_t nextPos = i + lenFirst;
	            if (nextPos >= tempStr.length()) {
	                continue;
	            }

	            std::string initialSecond;
	            int lenSecond = 0;

	            if (nextPos + 1 < tempStr.length() && ALL_PINYIN_INITIALS_STRICT.count(tempStr.substr(nextPos, 2))) {
	                initialSecond = tempStr.substr(nextPos, 2);
	                lenSecond = 2;
	            } else if (ALL_PINYIN_INITIALS_STRICT.count(tempStr.substr(nextPos, 1))) {
	                initialSecond = tempStr.substr(nextPos, 1);
	                lenSecond = 1;
	            }

	            if (lenSecond > 0) {

	                if (initialFirst == initialSecond) {
	                    return true;
	                }

	                const size_t thirdPos = nextPos + lenSecond;
	                if (thirdPos < tempStr.length()) {
	                    if ((thirdPos + 1 < tempStr.length() && ALL_PINYIN_INITIALS_STRICT.count(tempStr.substr(thirdPos, 2))) ||
	                        ALL_PINYIN_INITIALS_STRICT.count(tempStr.substr(thirdPos, 1))) {
	                        return true;
	                    }
	                }
	            }
	        }
	    }

		return false;
	}

	inline bool is_initial_proportion_over_threshold(const std::string& pinyin, const double threshold = 0.5) {
		std::string tempStr;
		for (const char c : pinyin) {
			if (std::isalpha(c)) {
				tempStr += c;
			}
		}

		if (tempStr.empty()) {
			return false;
		}

		int initialCharCount = 0;
		for (size_t i = 0; i < tempStr.length(); ) {
			if (i + 1 < tempStr.length() && kSoftInitials.count(tempStr.substr(i, 2))) {
				initialCharCount += 2;
				i += 2;
			}
			else if (kSoftInitials.count(tempStr.substr(i, 1))) {
				initialCharCount += 1;
				i += 1;
			}
			else {
				i += 1;
			}
		}

		const double proportion = static_cast<double>(initialCharCount) / static_cast<double>(tempStr.length());
		return proportion > threshold;
	}

	struct CorrectionRule
	{
		string pattern;					// 匹配的模式
		string replacement;				// 替换成的内容
		int syllableCount = 0;			// 错误占几个音节
		unordered_set<string> prefix;	//错误内容必要的前缀
	};

	constexpr int optionalRulesNum = 14;

	inline const vector<CorrectionRule>& get_correct_rules() {
		static const vector<CorrectionRule> correctRules = []() {
			vector<CorrectionRule> rules;
			rules.reserve(112);
			rules.insert(rules.end(), {
		{"iou","iu",2,{"d","j","l","m","n","q","x"}},
		{"uen","un",2,{"ch","c","d","g","h","j","k","l","q","r","sh","s","t","x","y","zh","z"}},
		{"uei","ui",1,{"ch","c","d","g","h","k","r","sh","s","t","zh","z"}},
		{"van","uan",1,{"j","q","x","l","y","z"}},
		{"aun","uan",1,{"ch","c","d","g","h","j","k","l","n","q","r","sh","s","t","x","y","zh","z"}},
		{"aio","iao",2,{"b","d","j","l","m","n","p","q","t","x"}},
		{"eu","ue",1,{"j","l","n","q","x","y"}},
		{"ain","ian",1,{"b","d","j","l","m","n","p","q","t","x"}},
		{"aing","iang",1,{"j","l","n","q","x"}},
		{"inag","iang",1,{"j","l","n","q","x"}},
		{"amg","ang",3,{"", "b","c","ch","chu","d","f","g","gu","h","hu","ji","k","ku","l","li","m","n","ni","p","qi","r","s","sh","shu","t","w","xi","y","z","zh","zhu"}},
		{"emg","eng",3,{"w","r","t","p","s","d","f","g","h","k","l","z","c","b","n","m","zh","ch","sh"}},
		{"img","ing",3,{"q","t","y","p","d","j","l","x","b","n","m"}},
		{"omg","ong",3,{"r","t","y","s","d","g","h","k","l","z","c","n","zh","ch","ji"}},
		{"agn","ang",3,{"", "b","c","ch","chu","d","f","g","gu","h","hu","ji","k","ku","l","li","m","n","ni","p","qi","r","s","sh","shu","t","w","xi","y","z","zh","zhu"}},
		{"egn","eng",3,{"b","c","ch","d","f","g","h","k","l","m","n","p","r","s","sh","t","w","z","zh"}},
		{"ign","ing",3,{"b","d","j","l","m","n","p","q","t","x","y"}},
		{"ogn","ong",3,{"c","ch","d","g","h","ji","k","l","n","qi","r","s","t","xi","y","z","zh"}},
		{"nag", "ang",3,{"b","c","ch","chu","d","f","g","gu","h","hu","ji","k","ku","l","li","m","n","ni","p","qi","r","s","sh","shu","t","w","xi","y","z","zh","zhu"}},
		{"neg", "eng",3,{"b","c","ch","d","f","g","h","k","l","m","n","p","r","s","sh","t","w","z","zh"}},
		{"nig", "ing",3,{"b","d","j","l","m","n","p","q","t","x","y"}},
		{"nog", "ong",3,{"c","ch","d","g","h","ji","k","l","n","qi","r","s","t","xi","y","z","zh"}},
		{"ah","ha",2,{"z","c","s"}},
		{"eh","he",2,{"z","c","s"}},
		{"ih","hi",2,{"z","c","s"}},
		{"uh","hu",2,{"z","c","s"}},
		{"ia","ai",2,{"w","g","h","k"}},
		{"ie","ei",2,{"w","t","f","g","h","k","z"}},
		{"ai","ia",2,{"q","j","x"}},
		{"ei","ie",2,{"q","j","x"}},
		{"oa","ao",2,{"r","t","y","p","s","d","g","h","k","l","z","c","b","n","m"}},
		{"uo","ou",2,{"y","p","f","m"}},
		{"na","an",2,{"w","r","t","y","p","s","d","f","g","h","k","l","z","c","b","n","m"}},
		{"ne","en",2,{"w","r","p","s","d","f","g","h","k","l","z","c","b","n","m"}},
		{"nge","eng",3,{"b","c","ch","d","f","g","h","k","l","m","n","p","r","s","sh","t","w","z","zh"}},
		{"nig","ing",3,{"b","d","j","l","m","n","p","q","t","x","y"}},
		{"ogn","ong",3,{"c","ch","d","g","h","ji","k","l","n","qi","r","s","t","xi","y","z","zh"}},
		{"ioa","iao",3,{"q","t","p","d","j","l","x","b","n","m"}},
		{"oia","iao",3,{"q","t","p","d","j","l","x","b","n","m"}},
		{"iu","ui",2,{"r","t","s","g","h","k","z","c"}},
		{"ui","iu",2,{"q","j","l","x","n","m"}},
		{"ni","in",2,{"q","y","p","j","l","x","b","n","m"}},
		{"au","ua",2,{"g","k","h","zh","sh"}},
		{"aui","uai",2,{"g","h","k","h","zh","sh"}},
		{"uia","uai",2,{"g","h","k","h","zh","sh"}},
		{"nu","un",2,{"q","r","t","y","s","d","g","h","j","k","l","z","x","c"}},
		{"augn","uang",4,{"g","h","k","zh","ch","sh"}},
		{"aung","uang",4,{"g","h","k","zh","ch","sh"}},
		{"oing","iong",4,{"j","q","x"}},
		{"oign","iong",4,{"j","q","x"}}
				});
			rules.insert(rules.end(), {
		{"ng","ang",3,{"b","c","ch","chu","d","f","g","gu","h","hu","k","ku","l","m","n","p","r","s","sh","shu","t","w","y","z","zh","zhu"}},
		{"ng","eng",3,{"w","r","t","p","s","d","f","g","h","k","l","z","c","b","n","m","zh","ch","sh"}},
		{"ng","ing",3,{"q","t","y","p","d","j","l","x","b","n","m"}},
		{"ng","ong",3,{"r","t","y","s","d","g","h","k","l","z","c","n","zh","ch"}},
		{"hza","zha",2,{""}},
		{"hze","zhe",2,{""}},
		{"hzi","zhi",2,{""}},
		{"hzu","zhu",2,{""}},
		{"hzai","zhai",2,{""}},
		{"hzei","zhei",2,{""}},
		{"hzan","zhan",2,{""}},
		{"hzen","zhen",2,{""}},
		{"hzou","zhou",2,{""}},
		{"hzuo","zhuo",2,{""}},
		{"hzua","zhua",2,{""}},
		{"hzun","zhun",2,{""}},
		{"hzui","zhui",2,{""}},
		{"hzuan","zhuan",2,{""}},
		{"hzuai","zhuai",2,{""}},
		{"hzuang","zhuang",2,{""}},
		{"hzang","zhang",2,{""}},
		{"hzeng","zheng",2,{""}},
		{"hzong","zhong",2,{""}},
		{"hca","cha",2,{""}},
		{"hce","che",2,{""}},
		{"hci","chi",2,{""}},
		{"hcu","chu",2,{""}},
		{"hcai","chai",2,{""}},
		{"hcei","chei",2,{""}},
		{"hcan","chan",2,{""}},
		{"hcen","chen",2,{""}},
		{"hcou","chou",2,{""}},
		{"hcuo","chuo",2,{""}},
		{"hcua","chua",2,{""}},
		{"hcun","chun",2,{""}},
		{"hcui","chui",2,{""}},
		{"hcuan","chuan",2,{""}},
		{"hcuai","chuai",2,{""}},
		{"hcuang","chuang",2,{""}},
		{"hcang","chang",2,{""}},
		{"hceng","cheng",2,{""}},
		{"hcong","chong",2,{""}}
				});
			rules.insert(rules.end(), {
		{"hsa","sha",2,{""}},
		{"hse","she",2,{""}},
		{"hsi","shi",2,{""}},
		{"hsu","shu",2,{""}},
		{"hsai","shai",2,{""}},
		{"hsei","shei",2,{""}},
		{"hsan","shan",2,{""}},
		{"hsen","shen",2,{""}},
		{"hsou","shou",2,{""}},
		{"hsuo","shuo",2,{""}},
		{"hsua","shua",2,{""}},
		{"hsun","shun",2,{""}},
		{"hsui","shui",2,{""}},
		{"hsuan","shuan",2,{""}},
		{"hsuai","shuai",2,{""}},
		{"hsuang","shuang",2,{""}},
		{"hsang","shang",2,{""}},
		{"hseng","sheng",2,{""}},
		{"v","u",1,{"b","c","d","f","g","h","j","k","m","o","p","q","r","s","t","w","x","y","z"}},
		{"ve","ue",3,{"q","j","x"}}
				});

			return rules;
		}();
		return correctRules;
	}

	inline vector<int> find_correction_rule(
			const string& fullPinyin , bool isMustCorrect)
	{
		vector<int> res;
		const auto& correctRules = get_correct_rules();
		int opRulesNum = isMustCorrect ? 0 : optionalRulesNum;
		for (int i = 0; i < static_cast<int>(correctRules.size()) - opRulesNum; ++i)
		{
			const auto& rule = correctRules[i];
			if (fullPinyin.size() < rule.pattern.size())
				continue;
			size_t pos = fullPinyin.find(rule.pattern);

			while (pos != string::npos)
			{
				for (const auto& pre : rule.prefix)
				{
					if (pos >= pre.size() && fullPinyin.substr(pos - pre.size(), pre.size()) == pre)
					{
						res.push_back(i);
						break; 
					}
				}
				pos = fullPinyin.find(rule.pattern, pos + 1);
			}
		}
		return res;
	}

	// 同时适配 stringViewVec（arena）与 stringViewList（默认分配器）
	template <typename StringViewSeq>
	inline bool is_mix_pinyin(const StringViewSeq& pinyin_sequence, int startPos = 0, int skipEnd = 0, bool isStrict = false)
	{
		bool hasShengMu = false;
		bool hasJianPin = false;
		bool hasFullPin = false;

		for(int i = startPos; i < static_cast<int>(pinyin_sequence.size()) - skipEnd; ++i)
		{
			if(coral::phonetics::syntax::is_sheng_mu(pinyin_sequence[i]))
				hasShengMu = true;
			else if(coral::phonetics::syntax::is_jian_pin(pinyin_sequence[i]) || (isStrict && pinyin_sequence[i].size() == 1))
				hasJianPin = true;
			else 
				hasFullPin = true;
		}

		//全声母和全拼不需要开启子码查询
		bool allShengMu = hasShengMu && (!hasJianPin && !hasFullPin);
		bool allFullPin = hasFullPin && (!hasJianPin && !hasShengMu);
		return !(allShengMu || allFullPin);
	}

	inline bool get_extended_pinyin(const string_view& pinyin, string& extendedPinyin) 
	{
		// 扩展多样性，例如xian -> xi'an
		// todo:这里的列举不全，后续需要考虑一次性把所有类似可再切分的拼音获取并通过文件读写维护
		static unordered_map<string_view, string_view> jianPins = 
		{
				{"ao", "a'o"}, {"bian", "bi'an"}, {"chua", "chu'a"}, {"chuang", "chuan'g"},{"cun", "cu'n"},
				{"dang", "dan'g"}, {"den", "de'n"}, {"dia", "di'a"}, {"ding", "din'g"},
				{"dian", "di'an"}, {"diao", "di'ao"}, {"duan", "du'an"}, {"en", "e'n"}, {"guai", "gu'ai"},
				{"guan", "gu'an"}, {"huai", "hu'ai"}, {"huan", "hu'an"}, {"jia", "ji'a"}, {"jian", "ji'an"},
				{"jiang", "ji'ang"}, {"jiao", "ji'ao"}, {"jie", "ji'e"}, {"jue", "ju'e"}, {"kua", "ku'a"},
				{"kuai", "ku'ai"}, {"kuan", "ku'an"}, {"lia", "li'a"}, {"lian", "li'an"}, {"liang", "li'ang"},
				{"liao", "li'ao"}, {"lie", "li'e"}, {"luan", "lu'an"}, {"lue", "lu'e"}, {"mian", "mi'an"},
				{"ne", "n'e"}, {"nian", "ni'an"}, {"niao", "ni'ao"}, {"nve", "nv'e"}, {"piao", "pi'ao"},
				{"qia", "qi'a"}, {"qian", "qi'an"}, {"qiao", "qi'ao"}, {"qie", "qi'e"}, {"shao", "s'hao"},
				{"shua", "shu'a"}, {"shuan", "shu'an"}, {"suan", "su'an"}, {"tian", "ti'an"}, {"tie", "ti'e"},
				{"tuan", "tu'an"}, {"xia", "xi'a"}, {"xian", "xi'an"}, {"xiao", "xi'ao"}, {"yuan", "yu'an"},
				{"yue", "yu'e"}, {"zhua", "zhu'a"}, {"zhuan", "zhu'an"}, {"zuan", "zu'an"}};
		auto tmpextendedPinyin = jianPins.find(pinyin);
		if (tmpextendedPinyin != jianPins.end()) 
		{
			extendedPinyin = tmpextendedPinyin->second;
			return true;
		}
		return false;
	}

	inline bool is_all_alpha(const string& str) 
	{
		regex reg("^[a-zA-Z]+$");
		return regex_match(str, reg);
	}

	inline uint64_t combine_ints(uint32_t prev, uint32_t next)
	{
		return (static_cast<uint64_t>(prev) << 32) | next;
	}

	inline uint32_t bit_reverse(uint32_t n)
	{
		n = (n >> 1)  & 0x55555555 | (n & 0x55555555) << 1;
		n = (n >> 2)  & 0x33333333 | (n & 0x33333333) << 2;
		n = (n >> 4)  & 0x0F0F0F0F | (n & 0x0F0F0F0F) << 4;
		n = (n >> 8)  & 0x00FF00FF | (n & 0x00FF00FF) << 8;
		n = (n >> 16) | (n << 16);
		return n;
	}

	inline uint32_t compute_check_bits(uint32_t prev, uint32_t next)
	{
		return prev ^ bit_reverse(next);
	}

	inline void get_fuzzy_alternatives(
		const string_view py,
		coral::phonetics::ParseTextViews& alternatives,
		const std::unordered_map<string_view, std::set<string_view>>& fuzzyInitials,
		const std::unordered_map<string_view, std::set<string_view>>& fuzzyFinals,
		const std::unordered_map<string_view, string_view>& fuzzyAlls,
		const syllable_strview_map& syllables)
	{
		static const std::vector<std::string_view> initials =
		{
			"zh", "ch", "sh",
			"z", "c", "s",
			"b", "p", "m", "f",
			"d", "t", "n", "l",
			"g", "k", "h",
			"j", "q", "x",
			"r", "y", "w"
		};

		// 拆分声母和韵母
		auto splitPinyin = [](const string_view pinyin) -> std::pair<string_view, string_view>
		{
			for (const auto& init : initials) {
				if (pinyin.compare(0, init.size(), init) == 0) {
					return {init, pinyin.substr(init.size())};
				}
			}
			return {"", pinyin};
		};

		auto [init, final] = splitPinyin(py);

		// 生成声母的候选
		std::vector<string_view> altInitials = {init};
		altInitials.reserve(3);
		if (const auto it = fuzzyInitials.find(init); it != fuzzyInitials.end())
		{
			for (const auto& item : it->second)
				altInitials.push_back(item);
		}

		// 生成韵母的候选
		std::vector<std::string_view> altFinals = {final};
		altFinals.reserve(3);
		if (const auto it = fuzzyFinals.find(final); it != fuzzyFinals.end())
		{
			for (const auto & item : it->second)
				altFinals.push_back(item);
		}

		alternatives.reserve(alternatives.size() + altInitials.size() * altFinals.size() + 1);

		// 生成全拼的候选
		if (const auto it = fuzzyAlls.find(py); it != fuzzyAlls.end())
		{
			alternatives.push_back(it->second);
		}

		for (const auto& i : altInitials)
		{
			for (const auto& f : altFinals)
			{
				auto it = syllables.find(string(i) + string(f));
				if (it != syllables.end()) {
					alternatives.push_back(it->first);
				}			
			}
		}
	}

	// 生成所有笛卡尔积组合的辅助函数
	inline void cartesian_product(
		const coral::phonetics::ParseTextViews& pinyin_sequence,
		const std::vector<coral::phonetics::ParseTextViews>& lists,
		const size_t depth,
		const int editsMade,
		const int maxEdits,
		coral::phonetics::ParseTextViews& current,
		std::vector<coral::phonetics::ParseTextViews>& result)
	{
		if (editsMade > maxEdits) {
			// 如果已经超出最大编辑数，直接剪枝，不再继续递归
			return;
		}
		if (depth == lists.size())
		{
			// 只有当至少有一个编辑时才算作模糊匹配结果
			if (editsMade > 0) {
				result.push_back(current);
			}
			return;
		}

		for (const auto& item : lists[depth])
		{
			current[depth] = item;
			int newEdits = editsMade;
			// 检查当前项是否与原始拼音不同
			if (item != pinyin_sequence[depth]) {
				newEdits++;
			}
			cartesian_product(pinyin_sequence, lists, depth + 1, newEdits, maxEdits, current, result);
		}
	}

	// 主函数，生成所有模糊序列组合
	inline bool is_match_fuzzy_correct(
		const coral::phonetics::ParseTextViews& pinyin_sequence,
		std::vector<coral::phonetics::ParseTextViews>& fuzzySequences,
		const syllable_strview_map& syllables,
		const std::unordered_map<std::string_view, std::set<std::string_view>>& fuzzyInitials,
		const std::unordered_map<std::string_view, std::set<std::string_view>>& fuzzyFinals,
		const std::unordered_map<std::string_view, std::string_view>& fuzzyAlls)
	{
		if (pinyin_sequence.empty())
			return false;

		std::vector<coral::phonetics::ParseTextViews> allAlternatives;
		allAlternatives.reserve(pinyin_sequence.size());
		coral::phonetics::ParseTextViews fuzzySequence;
		for (const auto& py : pinyin_sequence)
		{
			fuzzySequence.clear();
			get_fuzzy_alternatives(py, fuzzySequence, fuzzyInitials, fuzzyFinals, fuzzyAlls, syllables);
			allAlternatives.push_back(fuzzySequence);
		}

		coral::phonetics::ParseTextViews current(pinyin_sequence.size());
		constexpr int MAX_FUZZY_EDITS = 2;
		cartesian_product(pinyin_sequence, allAlternatives, 0, 0, MAX_FUZZY_EDITS, current, fuzzySequences);
		return !fuzzySequences.empty();
	}

	// 拼进调用方的缓冲。词典查询 key 每次解析要拼上百次，复用一块缓冲即可摊掉堆分配
	inline void join_string_views_into(const char delimiter, const coral::phonetics::ParseTextViews& input, string& out) {
		out.clear();
		size_t total = input.empty() ? 0 : input.size() - 1;
		for (const auto& item : input)
			total += item.size();
		out.reserve(total);	// 一次到位省掉扩容重分配
		for (auto it = input.cbegin(); it != input.cend(); ++it) {
			if (it != input.cbegin()) {
				out.push_back(delimiter);
			}
			out.append(*it);
		}
	}

	inline string join_string_views(const char delimiter, const coral::phonetics::ParseTextViews& input) {
		string res;
		join_string_views_into(delimiter, input, res);
		return res;
	}

	[[nodiscard]] inline bool compare_phrase_pinyin(const coral::phonetics::ParseTextViews& pinyin_sequence, std::string_view phrase_pinyin, bool isPredict)
	{
		auto isPrefix = [](const std::string_view prefix, const std::string_view str)
		{
			// 检查前缀是否为合法全拼
			// 元音（a,o,e）当做简拼对待，允许前缀匹配
			// 非元音的合法全拼必须完全匹配，不允许前缀匹配
			if (validPinyinCombines.find(prefix) != validPinyinCombines.end())
			{ 	
				if (coral::phonetics::syntax::is_yuanyin(prefix))
					return prefix.size() <= str.size() && prefix == str.substr(0, prefix.size());
				else
					return str == prefix;
			}
			else
			{
				return prefix.size() <= str.size() && prefix == str.substr(0, prefix.size());
			}
		};

		// 边扫描边比对，不再先切分成 vector：混拼过滤下每个候选词都要走一次这里，
		// 原实现每次调用都要为切分结果分配一个 reserve(18) 的向量
		size_t matched = 0;
		size_t start = 0;
		const size_t total = phrase_pinyin.size();
		for (size_t end = 0; end <= total; ++end)
		{
			if (end < total && phrase_pinyin[end] != '\'')
				continue;
			if (end > start)	// 与切分实现一致：跳过空段
			{
				// 词的音节数多于输入音节数：isPredict 只要求输入是词的前缀，否则不匹配
				if (matched == pinyin_sequence.size())
					return isPredict;
				if (!isPrefix(pinyin_sequence[matched], phrase_pinyin.substr(start, end - start)))
					return false;
				++matched;
			}
			start = end + 1;
		}
		return matched == pinyin_sequence.size();
	}
}

#endif // HELP_H

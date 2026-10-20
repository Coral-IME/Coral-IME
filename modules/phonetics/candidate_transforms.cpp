/*
* @author: xuxiang
* @date: 2022-5-31 16:12:25
*/
#include "pch.h"
#include<ctime>
#include <unordered_map>
#include "candidate_transforms.h"
#include <fstream>
#include "trace_log.h"
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <array>
#if defined(_WIN32) || defined(_WIN64)
#include "safec/cstring_win.h"
#else
#include "safec/cstring_posix.h"
#endif
#if defined(__APPLE__)
#include <chrono>
#elif defined(__linux__) || defined(__ANDROID__)
#include <dlfcn.h>
#endif

// number数据映射表
const std::map<std::string, std::array<std::string, 2>> NUMBER_MAP = {
    {u8"一", {u8"①", u8"Ⅰ"}},  // 一 -> ①, Ⅰ
    {u8"二", {u8"②", u8"Ⅱ"}},  // 二 -> ②, Ⅱ
    {u8"三", {u8"③", u8"Ⅲ"}},  // 三 -> ③, Ⅲ
    {u8"四", {u8"④", u8"Ⅳ"}},  // 四 -> ④, Ⅳ
    {u8"五", {u8"⑤", u8"Ⅴ"}},  // 五 -> ⑤, Ⅴ
    {u8"六", {u8"⑥", u8"Ⅵ"}},  // 六 -> ⑥, Ⅵ
    {u8"七", {u8"⑦", u8"Ⅶ"}},  // 七 -> ⑦, Ⅶ
    {u8"八", {u8"⑧", u8"Ⅷ"}},  // 八 -> ⑧, Ⅷ
    {u8"九", {u8"⑨", u8"Ⅸ"}},  // 九 -> ⑨, Ⅸ
    {u8"十", {u8"⑩", u8"Ⅹ"}}   // 十 -> ⑩, Ⅹ
};

// mispronunciation数据映射表
const std::vector<std::tuple<std::string, std::string, std::string>> MISPRONUNCIATION_DATA = {
    {u8"hundun", u8"馄饨", u8"饨:tun"},
    {u8"gangfuziyong", u8"刚愎自用", u8"愎:bi"},
    {u8"shaizi", u8"骰子", u8"骰:tou"},
    {u8"guilie", u8"龟裂", u8"龟:jun"},
    {u8"bisai", u8"鼻塞", u8"塞:se"},
    {u8"chicha", u8"叱咤", u8"咤:zha"},
    {u8"diaose", u8"掉色", u8"色:shai"},
    {u8"jindi", u8"劲敌", u8"劲:jing"},
    {u8"deijin", u8"得劲", u8"得:de"},
    {u8"qiangjin", u8"强劲", u8"劲:jing"},
    {u8"zuoji", u8"坐骑", u8"骑:qi"},
    {u8"tongti", u8"胴体", u8"胴:dong"},
    {u8"renchen", u8"妊娠", u8"娠:shen"},
    {u8"kayou", u8"揩油", u8"揩:kai"},
    {u8"gusha", u8"古刹", u8"刹:cha"},
    {u8"xiqiao", u8"蹊跷", u8"蹊:qi"},
    {u8"zhisheng", u8"吱声", u8"吱:zi"},
    {u8"tangshi", u8"汤匙", u8"匙:chi"},
    {u8"cukuang", u8"粗犷", u8"犷:guang"},
    {u8"guanka", u8"关卡", u8"卡:qia"},
    {u8"gonggei", u8"供给", u8"给:ji"},
    {u8"shangjia", u8"商贾", u8"贾:gu"},
    {u8"moyang", u8"模样", u8"模:mu"},
    {u8"zhire", u8"炽热", u8"炽:chi"},
    {u8"zhigao", u8"桎梏", u8"梏:gu"},
    {u8"zhaoxiang", u8"着想", u8"着:zhuo"},
    {u8"side", u8"似的", u8"似:shi"},
    {u8"chanrang", u8"禅让", u8"禅:shan"},
    {u8"xiangdao", u8"巷道", u8"巷:hang"},
    {u8"nishang", u8"霓裳", u8"裳:chang"},
    {u8"guopu", u8"果脯", u8"脯:fu"},
    {u8"muna", u8"木讷", u8"讷:ne"},
    {u8"nvhong", u8"女红", u8"红:gong"},
    {u8"jiepao", u8"解剖", u8"剖:pou"},
    {u8"zashe", u8"咋舌", u8"咋:ze"},
    {u8"milu", u8"秘鲁", u8"秘:bi"},
    {u8"jiaojie", u8"狡黠", u8"黠:xia"},
    {u8"tongchou", u8"铜臭", u8"臭:xiu"},
    {u8"nizhuo", u8"泥淖", u8"淖:nao"},
    {u8"tonghe", u8"恫吓", u8"恫:dong"},
    {u8"weixia", u8"威吓", u8"吓:he"},
    {u8"weilin", u8"莅临", u8"莅:li"},
    {u8"bugao", u8"讣告", u8"讣:fu"},
    {u8"efu", u8"饿殍", u8"殍:piao"},
    {u8"jianlin", u8"悭吝", u8"悭:qian"},
    {u8"qinjia", u8"亲家", u8"亲:qing"},
    {u8"junxun", u8"逡巡", u8"逡:qun"},
    {u8"jueyu", u8"鳜鱼", u8"鳜:gui"},
    {u8"siye", u8"肄业", u8"肄:yi"},
    {u8"piyi", u8"裨益", u8"裨:bi"},
    {u8"biaohan", u8"剽悍", u8"剽:piao"},
    {u8"huzou", u8"胡诌", u8"诌:zhou"},
    {u8"yingruo", u8"羸弱", u8"羸:lei"},
    {u8"shengqin", u8"省亲", u8"省:xing"},
    {u8"tuoben", u8"拓本", u8"拓:ta"},
    {u8"xianmei", u8"谄媚", u8"谄:chan"},
    {u8"boyou", u8"柏油", u8"柏:bai"},
    {u8"haozhou", u8"亳州", u8"亳:bo"},
    {u8"leting", u8"乐亭", u8"乐:lao"},
    {u8"peiling", u8"涪陵", u8"涪:fu"},
    {u8"luoshifen", u8"螺蛳粉", u8"蛳:si"},
    {u8"youhuijuna", u8"优惠券", u8"券:quan"},
    {u8"xuelinlin", u8"血淋淋", u8"血:xie"},
    {u8"efanggong", u8"阿房宫", u8"房:pang"},
    {u8"efang", u8"阿房", u8"房:pang"},
    {u8"kuaizishou", u8"刽子手", u8"刽:gui"},
    {u8"boyang", u8"鄱阳", u8"鄱:po"},
    {u8"dafubianbian", u8"大腹便便", u8"便:pian"},
    {u8"xuyuweishe", u8"虚与委蛇", u8"蛇:yi"},
    {u8"xinkuantipang", u8"心宽体胖", u8"胖:pan"},
    {u8"guaguazhuidi", u8"呱呱坠地", u8"呱:gu"},
    {u8"buzhaobianji", u8"不着边际", u8"着:zhuo"},
    {u8"qierbushe", u8"锲而不舍", u8"锲:qie"},
    {u8"ruhuorucha", u8"如火如荼", u8"荼:tu"},
    {u8"shengjuanzaiwo", u8"胜券在握", u8"券:quan"},
    {u8"shujianbuxian", u8"数见不鲜", u8"数:shuo"},
    {u8"yuhsanlunjin", u8"羽扇纶巾", u8"纶:guan"},
    {u8"linengkangding", u8"力能扛鼎", u8"扛:gang"},
    {u8"yinjiuzhike", u8"饮鸩止渴", u8"鸩:zhen"},
    {u8"haohaotangtang", u8"浩浩汤汤", u8"汤:shang"},
    {u8"hunshenjieshu", u8"浑身解数", u8"解:xie"},
    {u8"pianyixingshi", u8"便宜行事", u8"便:bian"},
    {u8"xinxinxuezi", u8"莘莘学子", u8"莘:shen"},
    {u8"shijiershang", u8"拾级而上", u8"拾:she"},
    {u8"weihuzuozhang", u8"为虎作伥", u8"伥:chang"},
    {u8"huizhirenkou", u8"脍炙人口", u8"脍:kuai"},
    {u8"baozhentianwu", u8"暴殄天物", u8"殄:tian"},
    {u8"yuyongkejia", u8"余勇可贾", u8"贾:gu"},
    {u8"chengxinruyi", u8"称心如意", u8"称:chen"},
    {u8"yanmiaozhuzhang", u8"揠苗助长", u8"揠:ya"},
    {u8"gangdou", u8"豇豆", u8"豇:jiang"}
};

// CTimeDateMiddleware
std::string coral::phonetics::DateTimeTransform::get_formatted_date(const std::tm& t) const {
    return std::to_string(t.tm_year + 1900) + u8"年" +  // 年
           std::to_string(t.tm_mon + 1) + u8"月" +      // 月
           std::to_string(t.tm_mday) + u8"日";          // 日
}
std::string coral::phonetics::DateTimeTransform::get_formatted_time(const std::tm& t) const {
    std::array<char, 16> buffer{};
    ks_snprintf_s(buffer.data(), buffer.size(), buffer.size() - 1,
                  "%02d:%02d:%02d", 
                  t.tm_hour, t.tm_min, t.tm_sec);
    
    return std::string(buffer.data()); 
}

std::string coral::phonetics::DateTimeTransform::get_formatted_week(const std::tm& t) const {
    static const char* kWeeks[] = {
        u8"星期日", u8"星期一", u8"星期二", u8"星期三",
        u8"星期四", u8"星期五", u8"星期六"
    };
    // tm_wday: 0=周日, 1=周一,6=周六
    const int weekday = std::max(0, std::min(t.tm_wday, 6));
    return kWeeks[weekday];
}
bool coral::phonetics::DateTimeTransform::get_current_local_time(std::tm& localTime) const {
#if defined(_WIN32) || defined(_MSC_VER)
    SYSTEMTIME st{};
    GetLocalTime(&st);

    // 基本有效性检查
    if (st.wYear < 1970 || st.wYear > 2100) {
        return false;
    }
    localTime = std::tm{};
    localTime.tm_year = st.wYear - 1900;
    localTime.tm_mon = st.wMonth - 1;
    localTime.tm_mday = st.wDay;
    localTime.tm_hour = st.wHour;
    localTime.tm_min = st.wMinute;
    localTime.tm_sec = st.wSecond;
    localTime.tm_wday = st.wDayOfWeek;
    return true;
#elif defined(__APPLE__)
    using Clock = std::chrono::system_clock;
    const auto now = Clock::now();
    const std::time_t timeValue = Clock::to_time_t(now);
    return localtime_r(&timeValue, &localTime) != nullptr;
#else
    return false;
#endif
}

bool coral::phonetics::DateTimeTransform::apply(const string& text, coral::phonetics::ParseStrings& postTexts) const {
    if (text != "sj" && text != "shijian" && text != "rq" && text != "riqi" && text != "xq" && text != "xingqi") {
        return false;
    }

    std::tm localTime{};
    if (!get_current_local_time(localTime)) {
        return false;
    }

    const std::string dateString = get_formatted_date(localTime);
    const std::string timeString = get_formatted_time(localTime);
    const std::string weekString = get_formatted_week(localTime);

    if (text == "sj" || text == "shijian") {
        postTexts.emplace_back(dateString + " " + timeString);
    } else if (text == "rq" || text == "riqi") {
        postTexts.emplace_back(dateString);
    } else if(text == "xq" || text == "xingqi") { 
        postTexts.emplace_back(dateString + " " + weekString);
        postTexts.emplace_back(weekString);
    }
    return true;
}

bool coral::phonetics::DateTimeTransform::process_middleware(
	const string& pystring,
	const string& sentence,
	coral::phonetics::ParseStrings& postStrings,
	bool& shouldBreak,
	bool& shouldContinue,
	int& insertPosition,
	int& maxInsertCount,
	MiddlewareType& middlewareType) const {
	shouldBreak = true;
	shouldContinue = false;
	insertPosition = kMiddlewareDefaultInsertPosition;
	maxInsertCount = kMiddlewareDefaultMaxInsertCount;
	middlewareType = MiddlewareType::NeedConvert | MiddlewareType::NoStoreUserLexicon;
	return apply(pystring, postStrings) && !postStrings.empty();
}

// CNumberMiddleware
bool coral::phonetics::NumberTransform::initialize(const string& name) {
    name_ = name;
    number_map_ = NUMBER_MAP;
    
    LOG_INFO("NumberMiddleware '" + name_ + "' initialized with " + 
         std::to_string(number_map_.size()) + " number mappings");
    return true;
}

bool coral::phonetics::NumberTransform::apply(const string& text, coral::phonetics::ParseStrings& postTexts) const {
	auto it = number_map_.find(text);
	if (it == number_map_.end())
		return false;
	postTexts.emplace_back(it->second[0]);
	postTexts.emplace_back(it->second[1]);
	return true;
}

bool coral::phonetics::NumberTransform::process_middleware(
	const string& pystring, 
	const string& sentence, 
	coral::phonetics::ParseStrings& postStrings,
	bool& shouldBreak,
	bool& shouldContinue,
	int& insertPosition,
	int& maxInsertCount,
	MiddlewareType& middlewareType) const {
	shouldBreak = false;
	shouldContinue = false;
	insertPosition = kMiddlewareDefaultInsertPosition;
	maxInsertCount = kMiddlewareDefaultMaxInsertCount;
	middlewareType = MiddlewareType::NoConvert | MiddlewareType::NoStoreUserLexicon;
	return apply(sentence, postStrings);
}

// CSymbolWordMiddleware
bool coral::phonetics::SymbolWordTransform::process_middleware(
	const string& pystring, 
	const string& sentence, 
	coral::phonetics::ParseStrings& postStrings,
	bool& shouldBreak,
	bool& shouldContinue,
	int& insertPosition,
	int& maxInsertCount,
	MiddlewareType& middlewareType) const {
	bool result = apply(sentence, postStrings);	
	if (result) {
		shouldBreak = false;
		shouldContinue = false;
		insertPosition = kMiddlewareDefaultInsertPosition;
		maxInsertCount = kMiddlewareSymbolMaxInsertCount;  // 符号可以插入多个，如德尔塔匹配δ和Δ
		middlewareType = MiddlewareType::NoConvert | MiddlewareType::NoStoreUserLexicon;
	}
	
	return result;
}

bool coral::phonetics::SymbolWordTransform::initialize(const string& name, const filesystem::path& configPath) {
	name_ = name;
	std::ifstream infile(configPath);
	if (!infile.is_open())
		return false;
	std::string line;
	std::vector<std::string> tmp;
	while (std::getline(infile, line)) {
		hlp::trim(line);
		if (!line.empty()) {
			tmp.clear();
			hlp::splitString(line, "\t", tmp);
			if (tmp.size() == 2) {
				std::vector<std::string> values;
				hlp::splitString(tmp[1], " ", values);
				symbol_word_map_[tmp[0]] = values;
			}
		}
	}
	return true;
}

bool coral::phonetics::SymbolWordTransform::apply(const string& text, coral::phonetics::ParseStrings& postTexts) const {
	auto it = symbol_word_map_.find(text);
	if (it != symbol_word_map_.end()) {
		postTexts.assign(it->second.begin(), it->second.end());
		return true;
	}
	return false;
}

// CEmojiWordMiddleware
bool coral::phonetics::EmojiTransform::initialize(const string& name, const filesystem::path& configPath) {
	if (is_loaded_.load(std::memory_order_acquire)) {
		return true;
	}
	name_ = name;
	std::ifstream infile(configPath);
	if (!infile.is_open())
		return false;
	std::vector<std::string> tmp;
	std::vector<std::string> values;
	std::vector<std::string> emojis;
	tmp.reserve(3);
	values.reserve(6);
	//经统计文件中每行表情最多4个
	emojis.reserve(5);
	std::string line;
	while (std::getline(infile, line)) {
		hlp::trim(line);
		if (!line.empty()) {
			tmp.clear();
			hlp::splitString(line, "\t", tmp);
			if (tmp.size() == 2) {
				values.clear();
				hlp::splitString(tmp[1], " ", values);
				emojis.clear();
				for (size_t i = 1; i < values.size(); ++i) {
					emojis.push_back(values[i]);
				}
				if (!emojis.empty()) {
					emoji_word_map_[tmp[0]] = emojis;
				}
			}
		}
	}
	is_loaded_.store(true, std::memory_order_release);
	return true;
}

bool coral::phonetics::EmojiTransform::apply(const string& text, coral::phonetics::ParseStrings& postTexts) const {
	auto it = emoji_word_map_.find(text);
	if (it != emoji_word_map_.end()) {
		postTexts.assign(it->second.begin(), it->second.end());
		return true;
	}
	return false;
}

bool coral::phonetics::EmojiTransform::process_middleware(
	const string& pystring, 
	const string& sentence, 
	coral::phonetics::ParseStrings& postStrings,
	bool& shouldBreak,
	bool& shouldContinue,
	int& insertPosition,
	int& maxInsertCount,
	MiddlewareType& middlewareType) const {
	// 用句子匹配表情符号
	bool result = apply(sentence, postStrings);	
	if (result) {
		// 统一设置表情符号中间件的参数
		shouldBreak = false;
		shouldContinue = true;
		insertPosition = kMiddlewareDefaultInsertPosition;
		maxInsertCount = kMiddlewareDefaultMaxInsertCount;
		middlewareType = MiddlewareType::NoConvert | MiddlewareType::NoStoreUserLexicon;
	}
	
	return result;
}

// CMispronunciationMiddleware
bool coral::phonetics::PronunciationTransform::initialize(const string& name) {
    name_ = name;
    
    for (const auto& [wrongPinyin, correctHanzi, correctPinyin] : MISPRONUNCIATION_DATA) {
        mispronunciation_map_[wrongPinyin] = std::make_pair(correctHanzi, correctPinyin);
    }
    
    LOG_TRACE("MispronunciationMiddleware initialized with hardcoded data, size: " + std::to_string(mispronunciation_map_.size()));
    return true;
}

bool coral::phonetics::PronunciationTransform::apply(const string& text, coral::phonetics::ParseStrings& postTexts) const
{
	// 进行精确查找
	auto it = mispronunciation_map_.find(text);
	if (it != mispronunciation_map_.end()) {
		string correctPinyin = it->second.second;
		string word = it->second.first;
		size_t colonPos = correctPinyin.find(':');
		if (colonPos != string::npos) {
			string targetChar = correctPinyin.substr(0, colonPos);
			string pinyin = correctPinyin.substr(colonPos + 1);
			if (text.find(pinyin) != string::npos) {
				return false;
			}
			// 处理多个相同易错字的情况
			string result = word;
			size_t startPos = 0;
			while (true) {
				size_t charPos = result.find(targetChar, startPos);
				if (charPos == string::npos) {
					break;
				}
				const size_t insertPos = charPos + targetChar.length();
				result.insert(insertPos, "(" + pinyin + ")");
				startPos = insertPos + pinyin.length() + 2;
			}

			postTexts.emplace_back(result);
			return true;
		}
		else {
			if (text.find(correctPinyin) != string::npos) {
				return false;
			}
			string result = word + "(" + correctPinyin + ")";
			postTexts.emplace_back(result);
			return true;
		}
	}
	return false;
}

const string& coral::phonetics::PronunciationTransform::commit_text(const string& input, const string& display_text) const
{
	const auto it = mispronunciation_map_.find(input);
	return it != mispronunciation_map_.end() ? it->second.first : display_text;
}

bool coral::phonetics::PronunciationTransform::process_middleware(
	const string& pystring, 
	const string& sentence, 
	coral::phonetics::ParseStrings& postStrings,
	bool& shouldBreak,
	bool& shouldContinue,
	int& insertPosition,
	int& maxInsertCount,
	MiddlewareType& middlewareType) const {
	shouldBreak = true;
	shouldContinue = false;
	insertPosition = kMiddlewareDefaultInsertPosition;
	maxInsertCount = kMiddlewareDefaultMaxInsertCount;
	middlewareType = MiddlewareType::NeedConvert;
	return apply(pystring, postStrings);
}

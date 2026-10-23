/*
 * @author: xuxiang
 * @date: 2021-6-10 17:19:57
 */
#include "pch.h"
#include <algorithm>
#include <memory>
#include <string>
#include <numeric>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <rapidjson/document.h>
#include <string_view>
#include <unordered_map>
#include "text_file_helpers.hpp"
#include "phonetic_types.h"
#include "syllable_helpers.h"
#include "runtime_context.h"	// 配置快照（coral::Engine::compatInstance().config()）
#include "shape_handler.h"
#include "coral/composition/assist_mode.h"
#include "symbol_handler.h"
#include "value_handler.h"
#include "syllable_parser.h"
#include "correction_path.h"
#include <cstdint>
#include <limits>
#include "script_conversion.h"
#include "preference_registry.h"
#include "support/parse_arena.hpp"
#include "support/parse_metrics.hpp"

#ifdef _WIN32
#include <windows.h>
#include <versionhelpers.h>
#endif
#include "invalid_initials.h"
using namespace Settings;

constexpr std::string_view fullCostSyllables[] = {
	"a", "ai", "an", "ang", "ao",
	"ba", "b", "bai", "ban", "bang", "bao", "bei", "be", "ben", "beng",
	"bi", "bian", "bia", "biao", "bie", "bin", "bing", "bo", "bu",
	"ca", "c", "cai", "can", "cang", "cao", "ce", "cen", "ceng",
	"cha", "ch", "chai", "chan", "chang", "chao", "che", "chen", "cheng", "chi",
	"chong", "cho", "chon", "chou", "chu", "chua", "chuai", "chuan", "chuang",
	"chui", "chun", "chuo", "ci", "cong", "co", "con", "cou", "cu", "cuan",
	"cui", "cun", "cuo",
	"da", "d", "dai", "dan", "dang", "dao", "de", "dei", "den", "deng",
	"di", "dia", "dian", "diao", "die", "ding", "din", "diu", "dong", "do",
	"don", "dou", "du", "duan", "dua", "dui", "dun", "duo",
	"e", "ei", "en", "eng", "er",
	"fa", "f", "fan", "fang", "fei", "fe", "fen", "feng", "fo", "fou", "fu",
	"ga", "g", "gai", "gan", "gang", "gao", "ge", "gei", "gen", "geng",
	"gong", "go", "gon", "gou", "gu", "gua", "guai", "guan", "guang", "gui",
	"gun", "guo",
	"ha", "h", "hai", "han", "hang", "hao", "he", "hei", "hen", "heng",
	"hong", "ho", "hon", "hou", "hu", "hua", "huai", "huan", "huang", "hui",
	"hun", "huo",
	"ji", "j", "jia", "jian", "jiang", "jiao", "jie", "jin", "jing", "jiong",
	"jio", "jion", "jiu", "ju", "jua", "juan", "jue", "jun", "jv", "jva", "jvan", "jve", "jvn",
	"ka", "k", "kai", "kan", "kang", "kao", "ke", "kei", "ken", "keng", "kong",
	"ko", "kon", "kou", "ku", "kua", "kuai", "kuan", "kuang", "kui", "kun", "kuo",
	"la", "l", "lai", "lan", "lang", "lao", "le", "lei", "leng", "len",
	"li", "lia", "lian", "liang", "liao", "lie", "lin", "ling", "liu",
	"long", "lo", "lon", "lou", "lu", "luan", "lua", "lue", "lve", "lun", "luo", "lv",
	"ma", "m", "mai", "man", "mang", "mao", "me", "mei", "men", "meng",
	"mi", "mian", "mia", "miao", "mie", "min", "ming", "miu", "mo", "mou", "mu",
	"na", "n", "nai", "nan", "nang", "nao", "ne", "nei", "nen", "neng",
	"ni", "nian", "nia", "niao", "niang", "nie", "nin", "ning", "niu",
	"nong", "no", "non", "nou", "nu", "nuan", "nua", "nue", "nve", "nuo", "nv",
	"o", "ou",
	"pa", "p", "pai", "pan", "pang", "pao", "pei", "pe", "pen", "peng",
	"pi", "pian", "pia", "piao", "pie", "pin", "ping", "po", "pou", "pu",
	"qi", "q", "qia", "qian", "qiang", "qiao", "qie", "qin", "qing", "qiong",
	"qio", "qion", "qiu", "qu", "quan", "qua", "que", "qun", "qv", "qvan", "qva", "qve", "qvn",
	"ran", "r", "rang", "rao", "re", "ren", "reng", "ri", "rong",
	"ro", "ron", "rou", "ru", "ruan", "rui", "run", "ruo",
	"sa", "s", "sai", "san", "sang", "sao", "se", "sen", "seng",
	"sha", "sh", "shai", "shan", "shang", "shao", "she", "shei", "shen", "sheng",
	"shi", "shou", "sho", "shu", "shua", "shuan", "shuai", "shuang", "shui",
	"shun", "shuo", "si", "song", "so", "son", "sou", "su", "suan",
	"sui", "sun", "suo",
	"ta", "t", "tai", "tan", "tang", "tao", "te", "teng", "ten",
	"ti", "tian", "tia", "tiao", "tie", "ting", "tin", "tong", "to", "ton",
	"tou", "tu", "tuan", "tui", "tun", "tuo",
	"wa", "w", "wai", "wan", "wang", "wei", "we", "wen", "weng", "wo", "wu",
	"xi", "x", "xia", "xian", "xiang", "xiao", "xie", "xin", "xing", "xiong",
	"xio", "xion", "xiu", "xu", "xuan", "xua", "xue", "xun", "xv", "xvan", "xva", "xve", "xvn",
	"ya", "y", "yan", "yang", "yao", "ye", "yi", "yin", "ying", "yo",
	"yong", "yon", "you", "yu", "yue", "yuan", "yun", "yv", "yvan", "yva", "yve","yvn",
	"za", "z", "zai", "zan", "zang", "zao", "ze", "zei", "zen", "zeng",
	"zha", "zh", "zhai", "zhan", "zhang", "zhao", "zhe", "zhei", "zhen", "zheng",
	"zhi", "zhong", "zho", "zhon", "zhou", "zhu", "zhua", "zhuai", "zhuan",
	"zhuang", "zhui", "zhun", "zhuo", "zi", "zong", "zo", "zon", "zou",
	"zu", "zuan", "zui", "zun", "zuo"
};

constexpr std::string_view fuzzyOnlySyllables_Z_ZH[] = { "zua", "zuang"};
constexpr std::string_view fuzzyOnlySyllables_C_CH[] = { "cua" };
constexpr std::string_view fuzzyOnlySyllables_S_SH[] = { "sua" };
constexpr std::string_view fuzzyOnlySyllables_L_N[] = { "nun" };
constexpr std::string_view fuzzyOnlySyllables_G_K[] = { "kei" };
constexpr std::string_view fuzzyOnlySyllables_F_H[] = { "fai", "fao", "fong", "fua", "fuai", "fuan", "fuang", "fui", "fun", "fuo", "hiao" };
constexpr std::string_view fuzzyOnlySyllables_R_L[] = { "ra", "rai", "rei", "ria", "rian", "riang", "riao", "rie", "len", "rin", "ring", "riu", "rve" };

std::unique_ptr<coral::phonetics::SyllableParser, coral::phonetics::SyllableParser::InstanceDeleter>&
coral::phonetics::SyllableParser::instance_storage()
{
    static std::unique_ptr<SyllableParser, InstanceDeleter> owner;
    return owner;
}

void coral::phonetics::SyllableParser::InstanceDeleter::operator()(SyllableParser* parser) const
{
    delete parser;
}

coral::phonetics::SyllableParser& coral::phonetics::SyllableParser::instance()
{
    auto& owner = instance_storage();
    if (!owner) owner.reset(new SyllableParser());
    return *owner;
}

void coral::phonetics::SyllableParser::release_instance()
{
    // 不要仅为停止一个已停止的运行时而创建模型。
    instance_storage().reset();
}

/**
* input：
	context.pinyin
	context.isDoublepinyin
* output：
	context.pinyinSequences
*/
bool coral::phonetics::SyllableParser::get_pinyin_sequence(PinyinParseContext& context)
{
	const bool shouldLimit = (context.parseMode != ParseMode::Reparse || context.pinyin.size() != 1);
	context.wordLattice.set_span_fetch_cap(context.direct_candidate_budget());
	// 方案B第一阶段：模糊音表传给音节图，在建边时挂模糊平行边（一次性完成
	// 原来 getPinyinSequences + applyFuzzyReplace 两步的工作）。开关关闭或
	// 模糊功能未启用时传空表，图行为与旧路径完全一致。
	coral::phonetics::FuzzySyllableViews fuzzyMaps;
	if (is_enable_fuzzy_ && fuzzy_in_graph_enabled())
	{
		fuzzyMaps.initials = &fuzzy_initials_;
		fuzzyMaps.finals = &fuzzy_finals_;
		fuzzyMaps.alls = &fuzzy_alls_;
	}
	return context.syllableGraph.get_pinyin_sequences(
		context.pinyin,
		syllables_view_,
		lexicon_manager_.max_length(),
		context.pinyinSequences,
		context.isFullPinyinMatch,
		context.isDoublepinyin,
		context.wordLattice,
		lexicon_manager_,
		context.sentHitDict,
		fuzzy_only_syllables_,
		fuzzyMaps,
		shouldLimit,
        // 概率模型下，完整词条不能作为组合路径的得分上界。
        language_model_.surface_model() == nullptr
	);
}

/**
* input：
	context.pinyinSequences
	context.fuzzyLimit
* output：
	context.pinyinSequences (模糊音替换后)
*/
void coral::phonetics::SyllableParser::apply_fuzzy_replace(PinyinParseContext& context)
{
	const bool shouldLimit = (context.parseMode != ParseMode::Reparse || context.pinyin.size() != 1);
	fuzzy_replace(
		context.pinyinSequences,
		context.wordLattice,
		lexicon_manager_,
		context.sentHitDict,
		fuzzy_only_syllables_,
		shouldLimit
	);
}

/**
* input：
	context.wordLattice
	context.sentHitDict
* output：
	context.languageDecoder
*/
bool coral::phonetics::SyllableParser::init_language_decoder(PinyinParseContext& context)
{
	context.languageDecoder.set_candidate_pool(&solution_pool_);
	return context.languageDecoder.init(context.wordLattice, language_model_);
}

/**
* 这里获取最终的全部解析结果，包括：中间件、英文词库、用户自定义短语等
* input：
	context.languageDecoder
	context.pinyin
	context.englishPinyin
	context.pinyinSequences
	context.isFullPinyinMatch
* output：
	context.sents
*/
bool coral::phonetics::SyllableParser::get_parsed_result(PinyinParseContext& context)
{
	coral::phonetics::ParseTextSet outputSentences;
	// 首解只组装够翻几页的候选，翻页触发的 Reparse 放开上限把剩下的长尾补齐
	context.languageDecoder.set_direct_candidate_budget(context.direct_candidate_budget());
	bool success = context.languageDecoder.get_parsed_res(context.sents, syllables_view_, outputSentences, context.pinyinSequences, context.shortestPathCost);
	// 方案B第二阶段：纠错门控改用"第一条未编辑纠错的句子"计算。
	// 编辑边生效时 sents[0] 可能是纠错句（拼音与原始输入不一致），
	// 用它算 bMixPinyins/score 会静默改变旧纠错的触发条件。
	const coral::phonetics::CandidateResult* gateSent = nullptr;
	if (!context.sents.empty())
	{
		gateSent = &context.sents[0];
		if (!context.isDoublepinyin && edit_in_graph_enabled())
		{
			for (const auto& s : context.sents)
			{
				// 保留基于原始候选集做出的纠错决策。
				if (s.is_additional_mixed) continue;
				size_t len = 0;
				for (const auto& sv : s.pinyinview)
					len += sv.size();
				if (len != context.pinyin.size())
					continue;
				bool same = true;
				size_t pos = 0;
				for (const auto& sv : s.pinyinview)
				{
					if (context.pinyin.compare(pos, sv.size(), sv) != 0)
					{
						same = false;
						break;
					}
					pos += sv.size();
				}
				if (same)
				{
					gateSent = &s;
					break;
				}
			}
		}
	}
	bool bMixPinyins = false;
	if (gateSent)
		bMixPinyins = coral::phonetics::syntax::is_mix_pinyin(gateSent->pinyinview, 0, 1, true);
	bool bHitDict = context.sentHitDict;
	if (!context.sents.empty()) {
		float score = exp(gateSent->probability * (1.0f + static_cast<coral::phonetics::Score>(context.pinyin.length()))) / kInvMaxcount;
		if (!context.isDoublepinyin && !coral::phonetics::syntax::is_valid_pinyin_prefix(context.pinyin) && coral::Engine::compatInstance().config().pinyin_correction())
		{
			const bool shouldTryCorrection = (bMixPinyins || !bHitDict || (context.sentHitDict && gateSent->pinyinview.size() == 2 && score < 1000.0f));
			if (shouldTryCorrection)
			{
				auto correctionRes = generate_correction_string(context.pinyin, 2);
				update_correction_res(correctionRes);
				generate_correction_candidates(correctionRes, context.sents, outputSentences, context.pinyin, context.mainType, bHitDict, bMixPinyins, score);
			}
			else
			{
				if (context.pinyin.size() < 6)
				{
					std::string uvReplaced = context.pinyin;
					int replaceIndex = -1;
					auto it = std::find(uvReplaced.begin(), uvReplaced.end(), 'v');
					if (it != uvReplaced.end())
					{
						replaceIndex = static_cast<int>(it - uvReplaced.begin());
						*it = 'u';
					}
					if (replaceIndex >= 0 && coral::phonetics::syntax::is_valid_full_pinyin(uvReplaced))
					{
						coral::phonetics::syntax::CorrectionMethod method{ coral::phonetics::syntax::CorrectionType::REPLACEMENT, replaceIndex, 'u', {}, {}, 0.0f };
						vector<coral::phonetics::syntax::CorrectionResult> correctionRes;
						correctionRes.emplace_back(coral::phonetics::syntax::CorrectionResult{ {uvReplaced}, 0.0f, method });
						update_correction_res(correctionRes);
						generate_correction_candidates(correctionRes, context.sents, outputSentences, context.pinyin, context.mainType, bHitDict, bMixPinyins, score);
					}
				}
			}
		}

        if (!cached_condition_.empty()) {
            std::vector<coral::phonetics::LexiconPhrase> predicted;
            lexicon_manager_.get_primary_lexicon().collect_continuations(cached_condition_, context.pinyin, predicted);
            std::stable_sort(predicted.begin(), predicted.end(), [](const auto& a, const auto& b) {
                return a.phrase_weight > b.phrase_weight;
            });
            // 从完整词条中取出的剩余部分可独立上屏；已输入的前缀绝不重复输出。
            for (auto it = predicted.rbegin(); it != predicted.rend(); ++it) {
                context.sents.erase(std::remove_if(context.sents.begin(), context.sents.end(), [&](const auto& item) {
                    return item.sentence == it->phrase;
                }), context.sents.end());
                coral::phonetics::CandidateResult candidate;
                candidate.sentence = std::string(it->phrase);
                candidate.pinyin = coral::phonetics::syntax::split_pinyin_string(std::string(it->phrase_pinyin));
                candidate.pinyinview.assign(candidate.pinyin.begin(), candidate.pinyin.end());
                candidate.word_size = static_cast<int>(candidate.pinyin.size());
                candidate.probability = it->phrase_weight;
                candidate.set_probability_with_weight(1.0f / (1.0f + static_cast<float>(context.pinyin.size())));
                outputSentences.insert(candidate.sentence);
                context.sents.insert(context.sents.begin(), std::move(candidate));
            }
        }
		if (success)
			parse_post_middleware(context.sents, outputSentences, context.pinyin, context.mainType);
	}
	add_english_lexicon_results(context.sents, context.englishPinyin, bHitDict && context.isFullPinyinMatch, context.shortestPathCost, outputSentences);
	add_ban_and_top_word_results(context.pinyin, context.syllableGraph.get_vertex_count() - 1, context.sents, outputSentences, context.pinyinSequences, context.mainType);
	adjust_custom_phrases_position(context.sents, context.pinyin);
	// 主词典之后还会合入英文、用户词和中间件候选；单声母简拼的 70 条是最终
	// 候选总量上限，而不只是主词典查询上限，因此在所有来源合并完成后再收口一次。
	if (context.directCandidateBudgetOverride == kSingleJianpinCandidateLimit
		&& context.sents.size() > static_cast<size_t>(kSingleJianpinCandidateLimit))
	{
		context.sents.resize(kSingleJianpinCandidateLimit);
	}
	export_graph_dot_file(context);	// 在 Demo 模式下导出图的可视化文件
	return !context.sents.empty();
}

void coral::phonetics::SyllableParser::export_graph_dot_file(const PinyinParseContext& context) const
{
#ifdef _DEBUG
	bool isDemoDebug = false;
#ifdef _WIN32
	isDemoDebug = GetConsoleWindow() != nullptr;
#else
	isDemoDebug = isatty(fileno(stdin));  // Linux/Mac: 检测stdin是否连接到终端
#endif
	if(isDemoDebug)
	{
		// exe 名称需要为 demo
		auto exePath = hlp::getExePath();
		if (exePath.find("demo") != std::string::npos)
		{
			// 语言模型改为惰性组合搜索后，不再存在可导出的句型语言图。
			// 这里只导出两个输入格和 pinyinSequences。
			auto dir = filesystem::current_path().parent_path().parent_path().parent_path() / "dot_visualize" / "graphs";
			context.syllableGraph.export_to_dot(dir / "syllable_graph.dot");
			context.wordLattice.export_to_dot(dir / "lexicon_graph.dot");
			coral::phonetics::SyllableParser::export_pinyin_sequences_to_dot(context.pinyinSequences, dir / "pinyin_sequences.dot");
		}
	}
#endif	// _DEBUG
}

void coral::phonetics::SyllableParser::add_ban_and_top_word_results(const string& pinyin, int vertexCount, vector<coral::phonetics::CandidateResult>& sents, coral::phonetics::ParseTextSet& outputSentences, const coral::phonetics::ParseSequences& pinyinSequences, coral::LanguageVariant mainType)
{
	coral::phonetics::ParseSequences maxPinyinSequences;
	std::copy_if(pinyinSequences.begin(), pinyinSequences.end(), 
		std::back_inserter(maxPinyinSequences),
		[vertexCount](const auto& pinyin_sequence) {
			return pinyin_sequence.from_ == 0 && pinyin_sequence.to_ == vertexCount;
		});

	std::vector<coral::candidate_policy::Rule> policies;
	if (!maxPinyinSequences.empty())
	{
		const auto& sequence = maxPinyinSequences[0];
		const auto& original = sequence.original_sequences_.empty() ? sequence.sequences_ : sequence.original_sequences_;
		std::string policyKey = coral::phonetics::syntax::join_string_views('\'', original);
		policyKey.erase(std::remove(policyKey.begin(), policyKey.end(), '\''), policyKey.end());
		policies = lexicon_manager_.get_user_lexicon().get_candidate_policies(
			policyKey,
			mainType);
	}

	for (const auto& policy : policies)
	{
		if (policy.kind != coral::candidate_policy::RuleKind::Promote)
			continue;
		coral::phonetics::CandidateResult solution;
		solution.sentence = policy.phrase;
		solution.pinyin = coral::phonetics::syntax::split_pinyin_string(policy.full_pinyin.empty() ? policy.lookup_key : policy.full_pinyin);
		solution.pinyinview.assign(solution.pinyin.begin(), solution.pinyin.end());
		solution.word_size = static_cast<int>(solution.pinyin.size());
		solution.probability = 0;
		solution.set_probability_with_weight(1.0f / (1.0f + static_cast<coral::phonetics::Score>(pinyin.length())));
		solution.index = 1;
		solution.is_fixed_candidate = policy.pinned ? 1 : 0;
		solution.phrase_id = kInvalidPhraseId;
		solution.is_corrected = false;

		if (outputSentences.find(solution.sentence) != outputSentences.end())
		{
			auto it = std::find_if(sents.begin(), sents.end(), [&solution](const coral::phonetics::CandidateResult& s) { return s.sentence == solution.sentence; });
			if (it != sents.end())
			{
				solution.phrase_id = std::min(solution.phrase_id, it->phrase_id);
				sents.erase(it);
			}
		}
		else
		{
			outputSentences.insert(solution.sentence);
		}

		sents.push_back(std::move(solution));
	}

	for (auto& solution : sents)
	{
		const bool disabled = std::any_of(policies.begin(), policies.end(), [&](const auto& policy) {
			return policy.kind == coral::candidate_policy::RuleKind::Disable &&
				policy.phrase == solution.sentence;
		});
		if (disabled)
		{
			solution.index = 0;
		}
		if (!solution.is_fixed_candidate && solution.index >= 1)
		{
			solution.transform_flags = solution.transform_flags | MiddlewareType::NoStoreUserLexicon;
		}
	}
}

void coral::phonetics::SyllableParser::generate_correction_candidates(std::vector<coral::phonetics::syntax::CorrectionResult>& correctionResults
	, vector<coral::phonetics::CandidateResult>& sents
	, coral::phonetics::ParseTextSet& outputSentences
	, const std::string& originalPystring
	, coral::LanguageVariant mainType
	, bool bHitDict
	, bool bMixPinyins
	, float score
)
{
	bool needDeleteBadFirst = false;
	if(!sents.empty())
	{
		int hasFuYuanyin = 0;
		std::vector<std::string> pinyin_sequence;
		coral::phonetics::syntax::CorrectionMethod method{coral::phonetics::syntax::CorrectionType::NONE, 0, '\0', {}, {}, 0.0f};
		for(int i = 0, index = 0; i < static_cast<int>(sents[0].pinyinview.size()); ++i)
		{
			bool is_fu_yuanyin = coral::phonetics::syntax::is_fu_yuanyin(sents[0].pinyinview[i]);
			if (is_fu_yuanyin) {
				if (!coral::phonetics::syntax::is_alphabet(sents[0].pinyinview, i - 1) && !coral::phonetics::syntax::is_alphabet(sents[0].pinyinview, i + 1)) {
					++hasFuYuanyin;
				}
			}
			if(method.type == coral::phonetics::syntax::CorrectionType::NONE && is_fu_yuanyin)
			{
				method.type = coral::phonetics::syntax::CorrectionType::DELETION;
				method.operateIndex = index;
				method.operateAlphabet = sents[0].pinyinview[i][0];
			}
			else
			{
				pinyin_sequence.emplace_back(sents[0].pinyinview[i]);
			}
			index += sents[0].pinyinview[i].size();
		}
		if(hasFuYuanyin == 1 && sents[0].pinyinview.size() != 1)
			correctionResults.emplace_back(coral::phonetics::syntax::CorrectionResult{pinyin_sequence, 0.0f, method});

		if(hasFuYuanyin && sents[0].pinyinview.size() != 1 && !correctionResults.empty())
			needDeleteBadFirst = true;
	}
	for (const auto& cr : correctionResults)
	{
		if(score > 10000.0f || cr.result.size() != 1 || cr.method.type == coral::phonetics::syntax::CorrectionType::INSERTION || cr.method.type == coral::phonetics::syntax::CorrectionType::DELETION){
			continue;
		}

		// 直接查主词库，不做二次切分、不查英文词典、不查用户词典
		coral::phonetics::ParseTextViews pinyinSeq = { std::string_view(cr.result[0]) };
		coral::phonetics::ParsePhrases phrases;
		lexicon_manager_.collect_repaired_candidates(pinyinSeq, phrases, mainType, -1, false);//不做数量限制
		if (phrases.empty())
			continue;
		float correctionScore = exp(phrases[0].phrase_weight) / kInvMaxcount;
		bool shouldApplyRerank = false;
		
		if (sents.empty() || needDeleteBadFirst)
		{
			shouldApplyRerank = true;
		}
		else if (bMixPinyins)
		{
			if (!bHitDict)
				shouldApplyRerank = true;
			else if (correctionScore >= score)
				shouldApplyRerank = true;
		}
		else
		{
			if (!bHitDict || correctionScore - score >= 100000.0f)
				shouldApplyRerank = true;
		}
		if (!shouldApplyRerank)
			continue;

		// 构建纠错候选序列，插入到原始序列前面（去重时优先保留纠错结果）
		vector<coral::phonetics::CandidateResult> correctedSents;
		coral::phonetics::ParseTextSet correctedSentences;
		for (const auto& phrase : phrases)
		{
			coral::phonetics::CandidateResult sol;
			sol.sentence = std::string(phrase.phrase);
			sol.pinyin.emplace_back(originalPystring);
			sol.pinyinview.assign(sol.pinyin.begin(), sol.pinyin.end());
			sol.word_size = static_cast<int>(sol.pinyin.size());
			sol.probability = phrase.phrase_weight;
			sol.is_corrected = true;
			sol.transform_flags = sol.transform_flags | MiddlewareType::NoStoreUserLexicon;
			correctedSentences.insert(sol.sentence);
			correctedSents.push_back(std::move(sol));
		}
		// 从原始序列和outputSentences中移除与纠错结果重复的项，保持一致
		for (const auto& s : correctedSentences)
			outputSentences.erase(s);
		sents.erase(std::remove_if(sents.begin(), sents.end(),
			[&correctedSentences](const coral::phonetics::CandidateResult& s) { return correctedSentences.count(s.sentence) > 0; }),
			sents.end());
		// 将纠错结果插入到原始序列前面，同步更新outputSentences
		sents.insert(sents.begin(), std::make_move_iterator(correctedSents.begin()), std::make_move_iterator(correctedSents.end()));
		for (const auto& s : correctedSentences)
			outputSentences.insert(s);
		return;
	}
	bool hasHitMainDict = false, isInsertOrDeleteCorrection = false;
	std::vector<std::string> originalSequences;
	auto [sentence, probability] = find_best_sentence_path(correctionResults
		, originalSequences
		, originalPystring
		, hasHitMainDict
		, isInsertOrDeleteCorrection
		, mainType
		);
	if (sentence.empty() || originalSequences.empty())
		return;

	coral::phonetics::CandidateResult solution;
	solution.sentence = std::move(sentence);
	solution.pinyin.assign(originalSequences.begin(), originalSequences.end());
	solution.pinyinview.assign(solution.pinyin.begin(), solution.pinyin.end());
	solution.word_size = static_cast<int>(originalSequences.size());
	solution.probability = probability;
	solution.is_corrected = true;
	solution.transform_flags = solution.transform_flags | MiddlewareType::NoStoreUserLexicon;

	if(sents.empty() || needDeleteBadFirst)
	{
		insert_correction_solution(solution, 0, sents, outputSentences, needDeleteBadFirst);
	}
	else if(bMixPinyins)
	{
		if(!bHitDict && hasHitMainDict && !isInsertOrDeleteCorrection)
			insert_correction_solution(solution, 0, sents, outputSentences, needDeleteBadFirst);
		else if ((exp(solution.probability) / kInvMaxcount) >= score)
			insert_correction_solution(solution, 1, sents, outputSentences, needDeleteBadFirst);
	}
	else
	{
		if(!bHitDict && hasHitMainDict || (exp(solution.probability) / kInvMaxcount) - score >= 100000.0f)
			insert_correction_solution(solution, 1, sents, outputSentences, needDeleteBadFirst);
	}
}

void coral::phonetics::SyllableParser::insert_correction_solution(coral::phonetics::CandidateResult& solution, int index, vector<coral::phonetics::CandidateResult>& sents, coral::phonetics::ParseTextSet& outputSentences, bool needDeleteBadFirst)
{
	bool needInsert = true;
    if(outputSentences.find(solution.sentence) != outputSentences.end())
    {
        auto it = std::find_if(sents.begin(), sents.end(), [&solution](const coral::phonetics::CandidateResult& s) { return s.sentence == solution.sentence; });
        if(it != sents.end())
        {
            if(it == sents.begin())
                needInsert = false;
            else
                sents.erase(it);
        }
    }
    else 
    {
        outputSentences.insert(solution.sentence);
    }

    if(needInsert)
    {
        if(needDeleteBadFirst)
            sents.erase(sents.begin());
        sents.insert(sents.begin() + index, std::move(solution));
    }
}

ParseMode coral::phonetics::SyllableParser::get_parse_mode(const string& pystring, const int bound) const
{
	if (sents_cache_.empty() || pre_input_string_ != pystring)
	{
		return ParseMode::NewParseOnly;
	}
	else
	{
		if (tail_exhausted_)
			return ParseMode::CacheOnly;
		// 首解只组装 FIRST_PASS_DIRECT_CANDIDATE_BUDGET 条主词典候选，只有这部分保证与
		// 完整解析逐条一致；再往后的位次要先用不设上限的重解析把缓存换成完整结果，
		// 否则翻出来的会是首解那份被截断的列表尾巴。
		if (bound > kFirstPassDirectCandidateBudget)
			return ParseMode::Reparse;
		// 缓存本身翻到底了同样要重解析。+1 是提前一页触发，让 m_isExistNextPage 在缓存耗尽前
		// 就反映扩展后的结果，防止翻出空页。
		if (bound + 1 >= static_cast<int>(sents_cache_.size()))
			return ParseMode::Reparse;
		return ParseMode::CacheOnly;
	}
}

std::pair<std::string, coral::phonetics::Score> coral::phonetics::SyllableParser::find_best_sentence_path(const std::vector<coral::phonetics::syntax::CorrectionResult>& correctionResults
	, std::vector<std::string>& originalSequences
	, const std::string& originalPystring
	, bool& hasHitMainDict
	, bool& isInsertOrDeleteCorrection
	, coral::LanguageVariant mainType
)
{
	// 加边
	const size_t originalLen = originalPystring.length();
	if (originalLen > static_cast<size_t>(std::numeric_limits<int>::max())) return {"", 0.f};
	std::vector<std::vector<detail::CorrectionPathEdge>> edges(originalLen + 1);
	for(const auto& correctionResult : correctionResults)
	{
        // 结果只由内部生成，仍在进入坐标计算前拒绝空音节及不配对的规则字段。
        if (correctionResult.result.empty() ||
            correctionResult.result.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            correctionResult.method.operateIndexRange.size() != correctionResult.method.operateAlphabetString.size()) continue;
        std::size_t corrected_length = 0;
        bool valid = true;
        for (const auto& syllable : correctionResult.result) {
            if (syllable.empty() || syllable.size() > static_cast<size_t>(std::numeric_limits<int>::max()) - corrected_length) {
                valid = false;
                break;
            }
            corrected_length += syllable.size();
        }
        for (const auto& replacement : correctionResult.method.operateAlphabetString) {
            if (replacement.size() > static_cast<size_t>(std::numeric_limits<int>::max())) valid = false;
        }
        if (!valid) continue;
		const std::string_view lastSyllable = correctionResult.result.back();

		int correctIndex = -1;
		std::int64_t index = 0;
		for(int i = 0; i < static_cast<int>(correctionResult.result.size()); ++i)
		{
			const std::string_view syllable = correctionResult.result[i];
			if(correctionResult.method.type == coral::phonetics::syntax::CorrectionType::DELETION)
			{
				if(correctIndex == -1 && index <= correctionResult.method.operateIndex && correctionResult.method.operateIndex <= index + static_cast<int>(syllable.size()))
				{
					correctIndex = i;
					break;
				}
			}
			else
			{
				if(correctIndex == -1 && index <= correctionResult.method.operateIndex && correctionResult.method.operateIndex < index + static_cast<int>(syllable.size()))
				{
					correctIndex = i;
					break;
				}
			}
			index += syllable.size();
		}

		if(syllables_view_.find(lastSyllable) == syllables_view_.end() && correctIndex == static_cast<int>(correctionResult.result.size()) - 1)
			continue;

		const size_t n = correctionResult.result.size();
		coral::phonetics::ParseTextViews pinyinSeq;
		coral::phonetics::ParseIntegers operateIndexRange;
		pinyinSeq.reserve(n);

		std::int64_t start = 0;
		int insertIndex = -1;
		for(int i = 0; i < static_cast<int>(n); ++i)
		{
            if (start < 0 || start > static_cast<std::int64_t>(originalLen)) break;
			pinyinSeq.clear();
			operateIndexRange.clear();
			operateIndexRange.push_back(static_cast<int>(start));
			bool isMixPinyins = false;
			std::int64_t end = start;
			int correctType = 0;
			for(int j = i; j < static_cast<int>(n); ++j) {
				const std::string_view syllable = correctionResult.result[j];
				pinyinSeq.emplace_back(syllable);
				coral::phonetics::ParsePhrases phrases;
				lexicon_manager_.collect_repaired_candidates(pinyinSeq, phrases, mainType, 1, false);
				if (!isMixPinyins) {
					isMixPinyins = coral::phonetics::syntax::is_mix_pinyin(pinyinSeq);
				}
				if (isMixPinyins) {
					lexicon_manager_.collect_repaired_candidates(pinyinSeq, phrases, mainType, 1, true);
				}
				switch(correctionResult.method.type)
				{
					case coral::phonetics::syntax::CorrectionType::INSERTION:
						if((insertIndex == -1 || insertIndex == j) && end <= correctionResult.method.operateIndex && correctionResult.method.operateIndex < end + static_cast<int>(syllable.size()))
						{
							end -= 1;
							insertIndex = j;
							correctType |= (1 << static_cast<int>(coral::phonetics::syntax::CorrectionType::INSERTION));
						}
						break;
					case coral::phonetics::syntax::CorrectionType::DELETION:
						if(end <= correctionResult.method.operateIndex && correctionResult.method.operateIndex <= end + static_cast<int>(syllable.size()))
						{
							end += 1;
							correctType |= (1 << static_cast<int>(coral::phonetics::syntax::CorrectionType::DELETION));
						}
						break;
					case coral::phonetics::syntax::CorrectionType::SWAP:
						if(end <= correctionResult.method.operateIndex && correctionResult.method.operateIndex < end + static_cast<int>(syllable.size()))
							correctType |= (1 << static_cast<int>(coral::phonetics::syntax::CorrectionType::SWAP));
						break;
					case coral::phonetics::syntax::CorrectionType::REPLACEMENT:
						if(end <= correctionResult.method.operateIndex && correctionResult.method.operateIndex < end + static_cast<int>(syllable.size()))
							correctType |= (1 << static_cast<int>(coral::phonetics::syntax::CorrectionType::REPLACEMENT));
						break;
					case coral::phonetics::syntax::CorrectionType::EXTRA_RULE:
					{
						for(size_t k = 0; k < correctionResult.method.operateIndexRange.size(); ++k)
						{
							const auto& [left, right] = correctionResult.method.operateIndexRange[k];
							if(end == left)
							{
								end += (static_cast<std::int64_t>(right) - left + 1) - static_cast<std::int64_t>(correctionResult.method.operateAlphabetString[k].length());
								correctType |= (1 << static_cast<int>(coral::phonetics::syntax::CorrectionType::EXTRA_RULE));
							}
						}
						break;
					}
					default:
						break;
				}
				end += static_cast<std::int64_t>(syllable.size());
                // 不夹取坐标，避免把错误映射伪装成消耗了有效输入的边。
                if (end < operateIndexRange.back() || end > static_cast<std::int64_t>(originalLen)) break;
				operateIndexRange.push_back(static_cast<int>(end));

				if (!phrases.empty() && end > start)
				{
					if(i == 0 && j == n - 1 && end == static_cast<std::int64_t>(originalLen))
						hasHitMainDict = true;

					const auto& bestPhrase = phrases[0];
					edges[static_cast<size_t>(start)].push_back({static_cast<int>(end), std::string(bestPhrase.phrase), bestPhrase.phrase_weight,
                        std::vector<int>(operateIndexRange.begin(), operateIndexRange.end()), correctType});
				}
			}
			if (operateIndexRange.size() <= 1) break;
			start = operateIndexRange[1];
		}
	}

    int correction_types = 0;
    auto result = detail::solve_correction_path(edges, originalPystring, originalSequences, correction_types);
    if (!result.first.empty()) {
        isInsertOrDeleteCorrection = (correction_types & (1 << static_cast<int>(syntax::CorrectionType::INSERTION))) ||
            (correction_types & (1 << static_cast<int>(syntax::CorrectionType::DELETION)));
    }
    return result;
}

void coral::phonetics::SyllableParser::build_fuzzy_map()
{
	const auto fuzzy_choices = coral::Engine::compatInstance().config().fuzzy_choices();
	assert(fuzzy_choices.size() == FUZZY_CHOICES);
	// 如果当前规则和上一个规则相同 不进行更新
	if (fuzzy_choices == last_fuzzy_vector_)
		return;

	// 配置变化后，同一个拼音也必须重新解析，不能沿用上一规则的候选。
	clear_pinyin_model_cache();
	fuzzy_initials_.clear();
	fuzzy_finals_.clear();
	fuzzy_alls_.clear();
	// 先移除上一规则添加的专用音节，再装配当前规则。
	for (const auto syllable : fuzzy_only_syllables_)
		syllables_view_.erase(syllable);
	fuzzy_only_syllables_.clear();

	struct FuzzyDef {
		std::string_view original;
		std::string_view fuzzy;
		enum class Type { Initial, Final, All } type;
	};

	static const std::vector<FuzzyDef> fuzzyDefs = {
		{"z", "zh", FuzzyDef::Type::Initial}, // 对应 fuzzy_choices[0]
		{"c", "ch", FuzzyDef::Type::Initial}, // 对应 fuzzy_choices[1]
		{"s", "sh", FuzzyDef::Type::Initial}, // 对应 fuzzy_choices[2]
		{"l", "n",  FuzzyDef::Type::Initial}, // 对应 fuzzy_choices[3]
		{"g", "k",  FuzzyDef::Type::Initial}, // 对应 fuzzy_choices[4]
		{"f", "h",  FuzzyDef::Type::Initial}, // 对应 fuzzy_choices[5]
		{"r", "l",  FuzzyDef::Type::Initial}, // 对应 fuzzy_choices[6]
		{"an", "ang", FuzzyDef::Type::Final}, // 对应 fuzzy_choices[7]
		{"en", "eng", FuzzyDef::Type::Final}, // 对应 fuzzy_choices[8]
		{"in", "ing", FuzzyDef::Type::Final}, // 对应 fuzzy_choices[9]
		{"an", "ai", FuzzyDef::Type::Final}, // 对应 fuzzy_choices[10]
		{"eng", "ong", FuzzyDef::Type::Final}, // 对应 fuzzy_choices[11]
		{"on", "ong", FuzzyDef::Type::Final}, // 对应 fuzzy_choices[12]
		{"ian", "iang",FuzzyDef::Type::Final}, // 对应 fuzzy_choices[13]
		{"uan", "uang",FuzzyDef::Type::Final}, // 对应 fuzzy_choices[14]
		{"un", "ong", FuzzyDef::Type::Final}, // 对应 fuzzy_choices[15]
		{"un", "iong", FuzzyDef::Type::Final}, // 对应 fuzzy_choices[16]
		{"hui", "fei", FuzzyDef::Type::All}, // 对应 fuzzy_choices[17]
		{"huang", "wang", FuzzyDef::Type::All}, // 对应 fuzzy_choices[18]
	};

	for(size_t i = 0; i < fuzzy_choices.size(); ++i)
	{
		if (i >= fuzzyDefs.size()) break;

		if (fuzzy_choices[i])
		{
			const auto& [original, fuzzy, type] = fuzzyDefs[i];
			if (type == FuzzyDef::Type::Initial)
			{
				fuzzy_initials_[original].insert(fuzzy);
				fuzzy_initials_[fuzzy].insert(original);
			}
			else if (type == FuzzyDef::Type::Final)
			{
				fuzzy_finals_[original].insert(fuzzy);
				fuzzy_finals_[fuzzy].insert(original);
			}
			else
			{
				fuzzy_alls_[original] = fuzzy;
				fuzzy_alls_[fuzzy] = original;
			}
		}
	}

	auto loadFuzzySyllables = [&](size_t index, const auto& syllables) {
		if (index < fuzzy_choices.size() && fuzzy_choices[index]) {
			for (const auto& syllable : syllables) {
				syllables_view_.emplace(syllable, kFullCost);
				fuzzy_only_syllables_.insert(syllable);
			}
		}
	};
	loadFuzzySyllables(0, fuzzyOnlySyllables_Z_ZH);
	loadFuzzySyllables(1, fuzzyOnlySyllables_C_CH);
	loadFuzzySyllables(2, fuzzyOnlySyllables_S_SH);
	loadFuzzySyllables(3, fuzzyOnlySyllables_L_N);
	loadFuzzySyllables(4, fuzzyOnlySyllables_G_K);
	loadFuzzySyllables(5, fuzzyOnlySyllables_F_H);
	loadFuzzySyllables(6, fuzzyOnlySyllables_R_L);

	last_fuzzy_vector_ = fuzzy_choices;
	is_enable_fuzzy_ = !(fuzzy_initials_.empty() && fuzzy_finals_.empty() && fuzzy_alls_.empty());
}

bool coral::phonetics::SyllableParser::load(const coral::phonetics::LexiconLocations& path, bool bSync,
    bool retryUntilSuccess, bool smart_candidates)
{
    std::lock_guard<std::mutex> lock(load_mutex_);
    // 启动配置在模型加载前固定；等待旧任务后再改，后台重试继续沿用同一值。
    finish_model_loading_locked();
    smart_candidates_ = smart_candidates;
    return load_locked(path, bSync, retryUntilSuccess);
}

bool coral::phonetics::SyllableParser::load_locked(const coral::phonetics::LexiconLocations& path,
    bool synchronous, bool retry_until_success)
{
    // 必须等上一批结束后，才能替换其任务正在读取的路径。
    finish_model_loading_locked();
    has_loaded_ = false;
    clear_pinyin_model_cache();
    if (all_lexicon_path_.versionId != path.versionId ||
        all_lexicon_path_.lexiconBaseDir != path.lexiconBaseDir ||
        all_lexicon_path_.strokeTrie.identifier != path.strokeTrie.identifier ||
        all_lexicon_path_.bushouTrie.identifier != path.bushouTrie.identifier ||
        all_lexicon_path_.strokeData.identifier != path.strokeData.identifier ||
        all_lexicon_path_.bushouData.identifier != path.bushouData.identifier) {
        u_mode_.reset();
        uu_mode_.reset();
        uu_available_ = false;
    }
	all_lexicon_path_ = path;
	retry_load_until_success_ = retry_until_success;
	LOG_INFO(std::string("PinyinModel load policy: ") + (synchronous ? "sync" : "async")
		+ (retry_until_success ? " with retry-until-success" : ""));
	const bool shouldLoadAsync = !synchronous;
	
	if (!shouldLoadAsync) {
		bool primarySuccess = load_primary_lexicon();
		bool modelSuccess = load_pinyin_model();
		has_loaded_ = primarySuccess && modelSuccess;
		return has_loaded_;
	}
	else {
		loading_pending_ = loading_tasks_.start({
            [this] { return load_primary_lexicon(); },
            [this] { return load_pinyin_model(); }
        });
		return loading_pending_;
	}
}

bool coral::phonetics::SyllableParser::load_primary_lexicon()
{
	return lexicon_manager_.load_primary_lexicon(all_lexicon_path_);
}

bool coral::phonetics::SyllableParser::load_pinyin_model()
{
	const coral::phonetics::LexiconLocations& path = all_lexicon_path_;
	bool middlewaresAlreadyLoaded = !middlewares_.empty();
	LOG_TRACE("Langengine: Starting pinyin model loading process");
	LoadResult loadResult = LoadResult::Success;
	auto loggerCheck = [&](bool cond, const char *msg, bool fatal = false)
		{
			if (!cond)
			{
				LOG_ERROR("Langengine: " + std::string(msg));
				loadResult = fatal ? LoadResult::Failure : LoadResult::PartialFailure;
			}
		};
	if (!middlewaresAlreadyLoaded) {
		shared_ptr<coral::phonetics::DateTimeTransform> currTimeDate = make_shared<coral::phonetics::DateTimeTransform>("currTimeDate");
		shared_ptr<coral::phonetics::NumberTransform> numberMiddleWare = make_shared<coral::phonetics::NumberTransform>();
		shared_ptr<coral::phonetics::SymbolWordTransform> symbolMiddleware = make_shared<coral::phonetics::SymbolWordTransform>();
		shared_ptr<coral::phonetics::PronunciationTransform> mispronunciationMiddleware = make_shared<coral::phonetics::PronunciationTransform>();
		// 检测Windows版本，Win7下不加载emoji词库
		bool shouldLoadEmoji = true;
#ifdef _WIN32
		if (IsWindows7OrGreater() && !IsWindows8OrGreater()) 
			shouldLoadEmoji = false;
#endif
		shared_ptr<coral::phonetics::EmojiTransform> emojiMiddleware = nullptr;
		if (shouldLoadEmoji) {
			emojiMiddleware = make_shared<coral::phonetics::EmojiTransform>();
		}
		
		loggerCheck(numberMiddleWare->initialize("numberMiddleWare"),
			"numberMiddleWare load failed", false);
		loggerCheck(symbolMiddleware->initialize("symbol", path.lexiconBaseDir / "opencc" / "symbol_word.coral"),
			"symbol_word.coral load failed", false);
		if (shouldLoadEmoji) 
			loggerCheck(emojiMiddleware->initialize("emoji", path.lexiconBaseDir / "opencc" / "emoji_word.coral"),
				"emoji_word.coral load failed", false);
		loggerCheck(mispronunciationMiddleware->initialize("mispronunciation"),
			"mispronunciationMiddleware load failed", false);
		
		middlewares_.push_back(symbolMiddleware);
		if (shouldLoadEmoji && emojiMiddleware) 
			middlewares_.push_back(emojiMiddleware);
		middlewares_.push_back(mispronunciationMiddleware);
		middlewares_.push_back(currTimeDate);
		middlewares_.push_back(numberMiddleWare);
	}

	const bool lexiconLoadResult = lexicon_manager_.load_pinyin_model(path, syllables_view_);
	loggerCheck(lexiconLoadResult, "lexicon load failed (including primary, user, and english lexicons)", true);
	const bool correctorModelLoadResult = pinyin_corrector_.load(path.correctionData.path, path.versionId, path.correctionData.identifier);
	loggerCheck(correctorModelLoadResult, "correctionModelData load failed", true);
	if (correctorModelLoadResult) {
		const bool correctorLMLoadResult = pinyin_corrector_.load_char_language_model(path.charLMData.path, path.versionId, path.charLMData.identifier);
		loggerCheck(correctorLMLoadResult, "correctionLMData load failed", true);
	}

	loggerCheck(language_model_.load_g_bin_only(path.bigramGBin, path.versionId), "bigram language model load failed", false);
#if CORAL_ENABLE_CHARACTER_MODEL
    language_model_.configure_character_model(path.bigramGBin.path.parent_path() / "character_model.coral", smart_candidates_);
#endif
	// 解析期的临时容器统一由 utils::ParseArena 供给，按需扩段、每次解析由
	// ParseArenaScope 整体回收，不需要在这里预设各类型的容量
	LOG_TRACE("Langengine: CPinyinModel load completed with status: " + to_string(loadResult));
	return loadResult != LoadResult::Failure;
}

bool coral::phonetics::SyllableParser::parse_input(
	const string& pystring,
	vector<string>& res,
	vector<string>& pyRes,
	vector<int>& isFixedRes,
	int offset,
	int solutionSize,
	int asciiMode,
	uint32_t conditionId, const std::string& condition)
{
    // 整次解析与加载/重试串行；排空异步任务建立所有模型写入到解析读取的同步关系。
    std::unique_lock<std::mutex> load_lock(load_mutex_);
    if (!ensure_model_loaded_locked()) {
        res.clear();
        pyRes.clear();
        isFixedRes.clear();
        is_exist_next_page_ = false;
        return false;
    }
	//g_settingsConfig->setShowInfos(true); // 自动化测试时启用词典信息显示

	// 本次解析用到的临时容器内存全部来自 arena，作用域结束时统一回收。
	// 必须声明在 context 之前：局部对象按声明逆序析构，这样它才会晚于所有使用者销毁。
	coral::phonetics::support::ParseArenaScope arenaScope;

	res.clear();
	pyRes.clear();

	is_show_infos_ = coral::Engine::compatInstance().config().isShowInfos();
	bool isok = false;
	bool isDoublepinyin = (asciiMode == PinyinMode::DOUBLEPINYIN);
	// 双拼键码已在输入层按所选码表展开；引擎只接收带音节分隔符的拼音。
	const string& processedPystring = pystring;
	PinyinParseContext context;
	ParseMode parseMode = ParseMode::NewParseOnly;
    std::string active_condition;
	CHECK_ERROR(has_loaded_ && processedPystring.size() != 0);
    // 语法/配置变化属于特殊输入缓存键的一部分。读取配置
    // 仅限 U/V 触发；普通拼音输入保持轻量。
    {
        int specialMode = 0;
        if ((asciiMode == 1 && (pystring[0] == 'U' || pystring[0] == 'V')) ||
            (asciiMode == 0 && (pystring[0] == 'u' || pystring[0] == 'v'))) {
            const auto& modeConfig = coral::Engine::compatInstance().config();
            specialMode = static_cast<int>(coral::composition::specialMode(std::string_view(pystring),
                asciiMode == 1, modeConfig.isUMode(), modeConfig.isVMode()));
        }
        if (cached_special_mode_ != specialMode) {
            recycle_sents_cache();
            tail_exhausted_ = false;
            cached_special_mode_ = specialMode;
        }
    }
	// 条件属于查询身份；相同拼音在不同上下文中不能共用候选缓存。
    active_condition = coral::Engine::compatInstance().config().smart_prediction()
        && processedPystring.size() > 1 && cached_special_mode_ == 0 ? condition : std::string{};
    if (!active_condition.empty() && get_language_variant() == coral::LanguageVariant::Traditional)
        active_condition = coral::phonetics::ScriptConversion::instance().ConvertText(hlp::str2wstr(active_condition), "t2s.json");
    if (cached_condition_ != active_condition) {
        clear_pinyin_model_cache();
        cached_condition_ = active_condition;
    }
    build_fuzzy_map();
    parseMode = get_parse_mode(pystring, offset + solutionSize);
	if (parseMode != ParseMode::CacheOnly)
	{
		if (parseMode == ParseMode::NewParseOnly)
		{
			recycle_sents_cache();
			tail_exhausted_ = false;
		}
		pre_input_string_ = processedPystring;
		LOG_TRACE("Langengine: Begin pinyin parsing - input: " + pystring);
		bool processedByStateMachine = process_mode_with_state_machine(processedPystring, context.sents, asciiMode);
        CHECK_ERROR(!special_mode_unavailable_);

		if (!processedByStateMachine)
		{
			context.pinyin = processedPystring;
			context.englishPinyin = processedPystring;
			context.isDoublepinyin = isDoublepinyin;
			context.parseMode = parseMode;
			context.mainType = get_language_variant();
			// 单字母声母是纯简拼，只保留最常用的 10 页候选。这个覆盖值同时约束
			// span 取词和最终组装，并且在 Reparse 时也不解除，避免用户翻页后又把
			// 该声母开头的全部单字重新搜入候选。a/e/o 等完整音节不受影响。
			if (processedPystring.size() == 1 && coral::phonetics::syntax::is_jian_pin(processedPystring))
				context.directCandidateBudgetOverride = kSingleJianpinCandidateLimit;

			// 1. 获取拼音序列
			CHECK_ERROR_MSG(get_pinyin_sequence(context),
				"Langengine: getPinyinSequences failed");

			// 2. 应用模糊音匹配
			// 方案B第一阶段：模糊音已在音节图内以平行边形式生成（见 getPinyinSequence），
			// 旧的后置展开路径仅在 CORAL_FUZZY_IN_GRAPH=0 时作为 A/B 回退保留。
			if (!fuzzy_in_graph_enabled())
				apply_fuzzy_replace(context);

			PERF_INC(parseCalls);
			PERF_ADD(sentHitDictTrue, context.sentHitDict ? 1 : 0);
			PERF_ADD(sequencesBuilt, context.pinyinSequences.size());

			// 查询最长的有效中文后缀，语言模型不只看到最后一次选出的单字。
            context.languageDecoder.set_condition_id(active_condition.empty() ? kInvalidPhraseId :
                lexicon_manager_.get_primary_lexicon().find_context_id(active_condition));
#if CORAL_ENABLE_CHARACTER_MODEL
            context.languageDecoder.set_smart_candidates(smart_candidates_);
            if (smart_candidates_) context.languageDecoder.set_character_context(active_condition);
#endif

            // 3. 初始化惰性语言模型解码器
			CHECK_ERROR_MSG(init_language_decoder(context),
				"Langengine: language decoder initialization failed");

			// 4. 获取解析结果
			bool success = get_parsed_result(context);

			if (!success)
			{
				LOG_ERROR("Langengine: No candidates after middleware processing");
			}
			else
			{
				LOG_TRACE("Langengine: Generated " + to_string(context.sents.size()) + " candidates after middleware processing");
			}
			CHECK_ERROR(success);
		}
		if (parseMode == ParseMode::NewParseOnly)
		{
			// 首解没被预算截断（或者压根没走解析图）就说明没有长尾可补，翻到底也不必再做
			// 一次注定拿不到新候选的重解析。单字符输入例外：它的重解析还要放开词库查询条数，
			// 那部分候选只有翻页时才取，跟预算截断无关。
			const bool cappedSingleJianpin = processedPystring.size() == 1
				&& coral::phonetics::syntax::is_jian_pin(processedPystring);
			tail_exhausted_ = cappedSingleJianpin
				|| (!context.languageDecoder.candidates_truncated() && pystring.size() != 1);
		}
	}
	// 5. 获取分页结果
	get_page_res(solutionSize, context.sents, res, pyRes, isFixedRes, offset, parseMode);
	isok = true;

Exit0:
	if (!isok)
	{
		is_exist_next_page_ = false;
		recycle_sents_cache();
		tail_exhausted_ = false;
		LOG_TRACE("Langengine: Parse failed for input: " + pystring);
	}
	{
		// arena 的水位和扩段次数：容量不够时只会变慢，不会报错，所以必须留一个可观测的口子
		const auto& arena = coral::phonetics::support::ParseArena::instance();
		LOG_TRACE("Langengine: arena live=" + to_string(arena.live_bytes())
			+ " peak=" + to_string(arena.peak_bytes())
			+ " capacity=" + to_string(arena.capacity_bytes())
			+ " chunks=" + to_string(arena.chunk_count())
			+ " grow=" + to_string(arena.grow_count()));
	}
	return isok;
}

void coral::phonetics::SyllableParser::update_correction_res(const vector<coral::phonetics::syntax::CorrectionResult>& correctionRes)
{
	correction_res_.clear();
	if (correctionRes.empty())
		return;

	// 纠错串预分配空间
	correction_res_.reserve(correctionRes.size());

	for (const auto& res : correctionRes)
	{
		const auto& syllablesVec = res.result;

		std::wstring correctRes;
		if (!syllablesVec.empty())
		{
			// 先计算总长度并预分配
			size_t totalLength = 1; // 最终多一个分隔符或类型标注
			totalLength += std::accumulate(syllablesVec.begin(), syllablesVec.end(), size_t(0),
				[](size_t sum, const auto& s) { return sum + s.size(); });
			totalLength += syllablesVec.size(); // 分隔符数量
			correctRes.reserve(totalLength);

			// 拼接字符串
			correctRes += hlp::str2wstr(syllablesVec[0]);
			for (size_t i = 1; i < syllablesVec.size(); ++i)
			{
				correctRes += L'\'';
				correctRes += hlp::str2wstr(syllablesVec[i]);
			}
		}

		//类型标注
		correctRes += L':';
		correctRes += std::to_wstring(static_cast<int>(res.method.type));

		correction_res_.emplace_back(std::move(correctRes));
	}
}

void coral::phonetics::SyllableParser::recycle_sents_cache()
{
	// 池子只是摊掉稳态下的堆分配，留太多反而白占内存
	constexpr size_t kSolutionPoolCapacity = 512;
	for (coral::phonetics::CandidateResult& solution : sents_cache_)
	{
		if (solution_pool_.size() >= kSolutionPoolCapacity)
			break;
		// 判据只看两个 vector：move 过的 string 会退回 SSO，capacity 仍非 0，说明不了什么
		if (solution.pinyin.capacity() == 0 && solution.pinyinview.capacity() == 0)
			continue;
		solution.reset();
		solution_pool_.push_back(std::move(solution));
	}
	sents_cache_.clear();
}

void coral::phonetics::SyllableParser::clear_pinyin_model_cache()
{
	recycle_sents_cache();
	tail_exhausted_ = false;
}

void coral::phonetics::SyllableParser::add_english_lexicon_results(vector<coral::phonetics::CandidateResult>& sentsCache, const string& originalInput, bool /*sentHitDict*/, coral::phonetics::Score /*shortestPathCost*/, coral::phonetics::ParseTextSet& outputSentences)
{
	// 精确英文词条已参与格架。词补全消耗已输入的前缀，
	// 使用同样的长度归一化一元评分，并对未输入的字母显式罚分；
	// 不占用固定候选位置，也没有 UI 门控。
	coral::phonetics::ParsePhrases phrases;
	lexicon_manager_.get_english_phrases(originalInput, phrases, 10);
	const auto lower = hlp::toLower_Optimized(originalInput);
	const bool hasUpper = std::any_of(originalInput.begin(), originalInput.end(),
		[](unsigned char ch) { return ch >= 'A' && ch <= 'Z'; });
	for (const auto& phrase : phrases) {
		coral::phonetics::CandidateResult solution;
		solution.sentence = std::string(phrase.phrase);
		if (hasUpper) solution.sentence = adjust_candidate_to_boundaries(solution.sentence, originalInput);
		if (!outputSentences.insert(solution.sentence).second) continue;
		solution.pinyin.emplace_back(originalInput);
		solution.word_size = static_cast<int>(originalInput.size());
		solution.phrase_id = phrase.id;
		const bool exact = hlp::toLower_Optimized(std::string(phrase.phrase)) == lower;
		const size_t missing = phrase.phrase.size() > originalInput.size()
			? phrase.phrase.size() - originalInput.size() : 0;
		solution.probability = phrase.phrase_weight - (exact ? 0.0f : 2.0f + 0.3f * missing);
		solution.set_probability_with_weight(1.0f / (1.0f + static_cast<float>(originalInput.size())));
		solution.transform_flags = solution.transform_flags | MiddlewareType::NoStoreUserLexicon;
		const auto where = std::find_if(sentsCache.begin(), sentsCache.end(),
			[&](const coral::phonetics::CandidateResult& old) { return coral::phonetics::CandidateDecoder::comp(solution, old); });
		sentsCache.insert(where, std::move(solution));
	}
}

void coral::phonetics::SyllableParser::get_page_res(
	int solutionSize,
	vector<coral::phonetics::CandidateResult>& sents,
	vector<string>& res,
	vector<string>& pyRes,
	vector<int>& isFixedRes,
	int offset,
	ParseMode parseMode
) {
	int resCount = 0;
	if (offset < 0)
	{
		offset = 0;
	}
	last_candidates_.clear();
	last_candidate_pinyins_.clear();
	last_candidates_.reserve(solutionSize);
	last_candidate_pinyins_.reserve(solutionSize);
	res.reserve(solutionSize);
	pyRes.reserve(solutionSize);
	isFixedRes.reserve(solutionSize);

	// 多字符输入的 Reparse 是「放开候选上限把整份结果重算一遍」，查询预算与首解相同、
	// 结果确定，前 FIRST_PASS_DIRECT_CANDIDATE_BUDGET 条与首解逐条一致，所以整体替换缓存：
	// 用户已经翻过的位次不会变，后面的长尾保持完整解析时该有的顺序。
	// 单字符输入例外：它的 Reparse 还会放开词库查询条数（shouldLimit=false），重解析会取到
	// 首解没有的候选，前缀可能变化，不能整体替换，沿用追加去重保持已展示位次不变。
	if (parseMode != ParseMode::CacheOnly)
	{
		if (parseMode == ParseMode::Reparse && pre_input_string_.size() == 1 && !sents_cache_.empty())
		{
			coral::phonetics::ParseTextSet existingCache(sents_cache_.size());
			for (const auto& cachedSol : sents_cache_)
				existingCache.insert(cachedSol.sentence);
			sents_cache_.reserve(sents_cache_.size() + sents.size());
			for (auto& newSol : sents)
			{
				if (existingCache.find(newSol.sentence) == existingCache.end())
					sents_cache_.push_back(std::move(newSol));
			}
		}
		else
		{
			// 先把旧缓存连缓冲收进池子，再整体替换；直接赋值会把它们连同缓冲一起析构掉
			recycle_sents_cache();
			sents_cache_ = std::move(sents);
			sents.clear();
		}
		if (parseMode == ParseMode::Reparse)
		{
			// Reparse 用的就是不设上限的预算，同一输入重算只会得到完全相同的结果，
			// 缓存在此刻已经是引擎能产出的全集。直接标记长尾取尽：否则下一页 bound 超过
			// 预算会再触发一次产出完全相同的全量重解析，白白付出一次最大延迟。
			tail_exhausted_ = true;
		}
	}

	is_exist_next_page_ = false;
	for (size_t startIndex = offset; startIndex < sents_cache_.size(); ++startIndex)
	{

		string sentence = sents_cache_[startIndex].sentence;
		if (sentence.empty())
		{
			assert(false);	// 如果这里continue了，会导致下一页的起始位offset错误
			continue;
		}
		string candidatePinyin = hlp::stringJoinVec('\'', sents_cache_[startIndex].pinyin);
		res.push_back(sentence);
		pyRes.push_back(candidatePinyin);
		int fixedStatus = sents_cache_[startIndex].is_fixed_candidate;
		if (sents_cache_[startIndex].index >= 1 && sents_cache_[startIndex].is_fixed_candidate == 0)
			fixedStatus = 2;
		isFixedRes.push_back(fixedStatus);
		last_candidates_.push_back(sentence);
		last_candidate_pinyins_.push_back(candidatePinyin);
		if (++resCount >= solutionSize)
		{
			is_exist_next_page_ = (sents_cache_.size() > startIndex + 1);
			break;
		}
	}
}

// 处理大写字母边界
string coral::phonetics::SyllableParser::adjust_candidate_to_boundaries(const string& candidate, const string& originalInput) {
	string adjusted = candidate;
	// 对于每个大写字母位置，确保候选词在该位置也有相应的大写字母
	for (size_t i = 0; i < originalInput.length() && i < adjusted.length(); ++i) {
		if (isupper(originalInput[i])) {
			adjusted[i] = toupper(adjusted[i]);
		}
	}
	return adjusted;
}

bool coral::phonetics::SyllableParser::is_pinyin_full_match(const coral::phonetics::CandidateResult& solution, const string& originalPinyinNoSep) {
	string phrase_pinyin;
	phrase_pinyin.reserve(solution.pinyin.size() * kPinyinMaxLen);
	for (const auto& py : solution.pinyin)
		phrase_pinyin += py;
	return phrase_pinyin == originalPinyinNoSep;
}

void coral::phonetics::SyllableParser::adjust_custom_phrases_position(vector<coral::phonetics::CandidateResult>& candidates, const string& originalPinyin) {
    string inputKey = originalPinyin;
    inputKey.erase(std::remove(inputKey.begin(), inputKey.end(), '\''), inputKey.end());
    vector<coral::phonetics::CandidateResult> pinned, custom, normal;
    for (auto& candidate : candidates) {
        if (candidate.index == 0) continue; // 被当前输入规则禁用。
        if (candidate.index == 1) {
            if (!is_pinyin_full_match(candidate, inputKey)) continue;
            (candidate.is_fixed_candidate ? pinned : custom).push_back(std::move(candidate));
        } else {
            normal.push_back(std::move(candidate));
        }
    }
    candidates.clear();
    candidates.reserve(pinned.size() + custom.size() + normal.size());
    for (auto& candidate : pinned) candidates.push_back(std::move(candidate));
    for (auto& candidate : custom) candidates.push_back(std::move(candidate));
    for (auto& candidate : normal) candidates.push_back(std::move(candidate));
}

bool coral::phonetics::SyllableParser::update_user_lexicon(
	const string& py,
	const string& sent)
{
	return lexicon_manager_.update_user_lexicon(py, sent);
}

bool coral::phonetics::SyllableParser::process_candidate_action(
	const string& pinyin,
	const string& word,
    const string& fullPinyin,
    coral::CandidateAction action,
    coral::LanguageVariant variant)
{
	string finalPinyin = pinyin;
	string finalFullPinyin = fullPinyin;
    if (!coral::IsValidCandidateAction(action) || !coral::IsValidLanguageVariant(variant)) return false;
    if (action == coral::CandidateAction::PinFirst) {
        string candidatePinyin;
		bool found = false;
		for (size_t i = 0; i < last_candidates_.size(); ++i) {
			if (last_candidates_[i] == word) {
				candidatePinyin = last_candidate_pinyins_[i];
				found = true;
				break;
			}
		}
		if (!found) {
			LOG_ERROR("Langengine: Candidate word not found in recent results");
			return false;
		}
		string normalizedCandidatePinyin = candidatePinyin;
		string normalizedInputPinyin = pinyin;
		normalizedCandidatePinyin.erase(std::remove(normalizedCandidatePinyin.begin(), normalizedCandidatePinyin.end(), '\''), normalizedCandidatePinyin.end());
		normalizedInputPinyin.erase(std::remove(normalizedInputPinyin.begin(), normalizedInputPinyin.end(), '\''), normalizedInputPinyin.end());
		if (normalizedCandidatePinyin != normalizedInputPinyin) {
			LOG_ERROR("Langengine: Pinyin validation failed for fixed candidate - candidate: " + normalizedCandidatePinyin + ", input: " + normalizedInputPinyin);
			return false;
		}
		finalPinyin = candidatePinyin;
		finalFullPinyin = candidatePinyin;
	}
	finalPinyin.erase(std::remove(finalPinyin.begin(), finalPinyin.end(), '\''), finalPinyin.end());
    auto& userData = lexicon_manager_.get_user_lexicon();
    switch (action) {
        case coral::CandidateAction::PinFirst:
            return userData.pin_candidate_first(finalPinyin, word, finalFullPinyin, variant);
        case coral::CandidateAction::UnpinFirst:
            return userData.unpin_candidate_first(finalPinyin, word, variant);
        case coral::CandidateAction::Disable:
            return userData.disable_candidate(finalPinyin, word, finalFullPinyin, variant);
    }
    return false;
}

bool coral::phonetics::SyllableParser::store_lexicon() const
{
	return lexicon_manager_.store_lexicon();
}

bool coral::phonetics::SyllableParser::clear_lexicon()
{
	return lexicon_manager_.clear_lexicon();
}

bool coral::phonetics::SyllableParser::load_user_lexicon_by_line(const string& pinyinCode, const string& quanPyCode, const string& sent, coral::phonetics::Score dee, int tickTime)
{
	return lexicon_manager_.load_user_lexicon_by_line(
		pinyinCode, quanPyCode, sent, dee, tickTime, syllables_view_
	);
}

void coral::phonetics::SyllableParser::begin_load_user_lexicon_by_line()
{
	lexicon_manager_.begin_load_user_lexicon_by_line();
}

void coral::phonetics::SyllableParser::end_load_user_lexicon_by_line()
{
	lexicon_manager_.end_load_user_lexicon_by_line();
}

void coral::phonetics::SyllableParser::parse_post_middleware(
	vector<coral::phonetics::CandidateResult>& sents,
	coral::phonetics::ParseTextSet& outputedSentences,
	const string& pystring,
	coral::LanguageVariant mainType) {
	for (auto& middleware : middlewares_) {
		auto it = sents.begin();
		while (it != sents.end()) {
			// 非引擎原始解析的跳过
			if (it->transform_flags != MiddlewareType::Default) {
				++it;
				continue;
			}
			coral::phonetics::ParseStrings postStrings;
			bool shouldBreak = false;
			bool shouldContinue = false;
			int insertPosition = kMiddlewareDefaultInsertPosition;
			int maxInsertCount = kMiddlewareDefaultMaxInsertCount;
			MiddlewareType middlewareType = MiddlewareType::NeedConvert;
			bool shouldApply = middleware->process_middleware(
				pystring, it->sentence, postStrings, 
				shouldBreak, shouldContinue, insertPosition, maxInsertCount, middlewareType);

			if (!shouldApply) {
				if (shouldBreak)
					break;
				++it;
				continue;
			}


			int insertCount = 0;
			bool isInsert = false;
			for (const auto& postStr : postStrings) {
				if (outputedSentences.find(postStr) != outputedSentences.end()) {
					continue;
				}
				
				if (insertCount >= maxInsertCount) {
					break;
				}
				
				coral::phonetics::CandidateResult s;
				s.transform_flags = middlewareType;
				s.pinyin = it->pinyin;
				s.pinyinview = it->pinyinview;
				s.probability = it->probability;
				s.sentence = middleware->commit_text(pystring, postStr);
				if (s.sentence != postStr)
					s.display_sentence = postStr;
				s.word_size = it->word_size;
				s.is_corrected = it->is_corrected;

				// 标签去重：带注音的变体可能上屏同一个词。
				outputedSentences.insert(postStr);
				it = sents.insert(next(it, insertPosition), std::move(s));
				isInsert = true;
				insertCount++;
#ifdef _DEBUG
				std::cout << "[Middleware] Inserted: " << postStr << " at position " << insertPosition << std::endl;
#endif
			}
			if (shouldBreak) {
				break;
			} else if (shouldContinue) {
				++it;
				continue;
			} else if (!isInsert) {
				++it;
			}
		}
	}

	// 处理输入含有一些特殊符号的情况，确保Result的首位为原始输入
	if (pystring.find_first_of(kPinyinPassthroughDelimiters) != string::npos)
	{
		coral::phonetics::CandidateResult originalInputSolution;
		originalInputSolution.sentence = pystring;
		originalInputSolution.pinyin = {pystring};
		originalInputSolution.pinyinview.push_back(originalInputSolution.pinyin.back());
		outputedSentences.insert(originalInputSolution.sentence);
		sents.insert(sents.begin(), std::move(originalInputSolution));
	}

	//最后直接插入
	if (mainType == coral::LanguageVariant::Traditional)
	{
		vector<coral::phonetics::CandidateResult> result;
		outputedSentences.clear();
		coral::phonetics::ParseTextSet traditionSet = {};
		for (auto it = sents.begin(); it != sents.end(); ++it) {
			if (it->transform_flags == MiddlewareType::Default ||
				(it->transform_flags & MiddlewareType::NeedConvert) != MiddlewareType::Default) {
				std::wstring wtext = hlp::str2wstr(it->sentence);
				std::string newWord = coral::phonetics::ScriptConversion::instance().ConvertText(wtext);
				if (!it->display_sentence.empty())
					it->display_sentence = coral::phonetics::ScriptConversion::instance().ConvertText(hlp::str2wstr(it->display_sentence));
				const string& label = it->display_sentence.empty() ? newWord : it->display_sentence;
				auto iter = traditionSet.find(label);
				if (iter != traditionSet.end())
				{
					continue;
				}
				traditionSet.insert(label);
				outputedSentences.insert(newWord);
				it->sentence = newWord;
				result.push_back(std::move(*it));
			}
			else {
				outputedSentences.insert(it->sentence);
				// 例如emoji不需要转化为繁体，否则乱码
				result.push_back(std::move(*it));
			}
		}
		sents.swap(result);
	}
}

const string& coral::phonetics::SyllableParser::candidate_display_text(int index) const
{
	static const string empty;
	return index >= 0 && static_cast<size_t>(index) < sents_cache_.size()
		? sents_cache_[index].display_sentence : empty;
}

void coral::phonetics::SyllableParser::check_update_lexicon(
	const string& sent,
	string& updateSent,
	int& selectedIndex) {
	const string& text = sent;
	updateSent = text;
	selectedIndex = -1;
	for (int startIndex = 0; startIndex < static_cast<int>(sents_cache_.size()); ++startIndex)
	{
		if (text != sents_cache_[startIndex].sentence) {
			continue;
		}
		int index = startIndex;
		if (selectedIndex < 0)
			selectedIndex = index;

		if ((sents_cache_[index].transform_flags & MiddlewareType::NoStoreUserLexicon)!= MiddlewareType::Default) {
			// 不需要入库的中间件和补全的结果不入库
			updateSent = "";
			break;
		}

		while (sents_cache_[index].transform_flags != MiddlewareType::Default && index > 0) {
			index--;
		}
		if (index >= 0 && sents_cache_[index].transform_flags == MiddlewareType::Default) {
			updateSent = sents_cache_[index].sentence;
			break;
		}
	}
}

uint32_t coral::phonetics::SyllableParser::capture_condition_selection(int selectedIndex)
{
	if (selectedIndex < 0 || selectedIndex >= static_cast<int>(sents_cache_.size())) {
		return kInvalidPhraseId;
	}

	auto& solution = sents_cache_[selectedIndex];
#ifdef _DEBUG
	std::cout << "Condition id set to " << solution.phrase_id << " by CPinyinModel::captureConditionSelection" << std::endl;
#endif
	return solution.phrase_id;
}

bool coral::phonetics::SyllableParser::maintain_lexicon()
{
	return lexicon_manager_.maintain_lexicon(syllables_view_);
}

bool coral::phonetics::SyllableParser::revert_last_action_for_phrase(const std::string& delPhrase)
{
	return lexicon_manager_.revert_last_action_for_phrase(delPhrase);
}

void coral::phonetics::SyllableParser::stop_background_work()
{
    std::lock_guard<std::mutex> lock(load_mutex_);
    if (loading_pending_ && !finish_model_loading_locked())
        LOG_ERROR("Engine: a background model load failed");
    lexicon_manager_.shutdown_user_history();
}

hlp::ImportResult coral::phonetics::SyllableParser::import_user_lexicon(const filesystem::path& lexiconPath)
{
	return lexicon_manager_.import_user_lexicon(lexiconPath, syllables_view_);
}

bool coral::phonetics::SyllableParser::export_user_lexicon(const filesystem::path& lexiconPath)
{
	return lexicon_manager_.export_user_lexicon(lexiconPath);
}

coral::phonetics::SyllableParser::SyllableParser() :
	has_loaded_(false)
{
	for (auto s : fullCostSyllables) {
		syllables_view_.emplace(s, kFullCost);
	}
}

coral::phonetics::SyllableParser::~SyllableParser()
{
    // 加载可能创建学习工作线程，因此先排空加载再停止历史记录。
    stop_background_work();
}

void coral::phonetics::SyllableParser::is_exist_next_page(bool& existNextPage)
{
	existNextPage = is_exist_next_page_;
}

bool coral::phonetics::SyllableParser::process_mode_with_state_machine(const string& pystring, vector<coral::phonetics::CandidateResult>& sentsCache, const int& asciiMode)
{
    using coral::composition::SpecialMode;
    special_mode_unavailable_ = false;
    if (pystring.empty() || (asciiMode == 1 ? (pystring[0] != 'U' && pystring[0] != 'V') : (pystring[0] != 'u' && pystring[0] != 'v'))) return false;
    const auto& config = coral::Engine::compatInstance().config();
    const auto mode = coral::composition::specialMode(std::string_view(pystring), asciiMode == 1,
        config.isUMode(), config.isVMode());
    if (mode == SpecialMode::Normal) return false;
    const auto& path = all_lexicon_path_;
    if (mode == SpecialMode::Stroke && pystring.size() > 1) {
        if (!u_mode_) u_mode_ = std::make_unique<coral::assist::ShapeHandler>(
            coral::assist::ShapeHandler::File{path.strokeTrie.path, path.strokeTrie.identifier},
            coral::assist::ShapeHandler::File{path.strokeData.path, path.strokeData.identifier},
            coral::assist::ShapeHandler::File{path.bushouTrie.path, path.bushouTrie.identifier},
            coral::assist::ShapeHandler::File{path.bushouData.path, path.bushouData.identifier},
            path.lexiconBaseDir, path.versionId);
        u_mode_->parse(pystring, sentsCache);
        // 当笔画、部首词典都不提供候选时，全拼 U 触发不得掩盖
        // 错音纠正。显式双拼 U 与不完整前缀
        // 仍留在各自的特殊输入模式中。
        if (sentsCache.empty() && asciiMode == 0 && config.pinyin_correction())
            return false;
        special_mode_unavailable_ = sentsCache.empty() && u_mode_->unavailable();
    } else if (mode == SpecialMode::Symbols && pystring.size() > 2) {
        if (!uu_mode_) {
            uu_mode_ = std::make_unique<coral::assist::SymbolHandler>();
            uu_available_ = uu_mode_->load(path.lexiconBaseDir / "dw.coral");
        }
        if (uu_available_) uu_mode_->parse(pystring, sentsCache);
        else special_mode_unavailable_ = true;
    } else if (mode == SpecialMode::Convert && pystring.size() > 1) {
        coral::assist::ValueHandler::parse(pystring, sentsCache);
    }
    // 其他不完整/未匹配的特殊输入保留其字面候选。
    if (sentsCache.empty()) {
        coral::phonetics::CandidateResult raw;
        raw.sentence = pystring;
        raw.pinyin.push_back(pystring);
        sentsCache.push_back(std::move(raw));
    }
    for (auto& solution : sentsCache) solution.transform_flags = MiddlewareType::NoStoreUserLexicon;
    return true;
}

void coral::phonetics::SyllableParser::fuzzy_replace(coral::phonetics::ParseSequences& pinyinSequences, 
	coral::phonetics::CandidateLattice& wordLattice, 
	coral::phonetics::LexiconCatalog& lexiconmanager, 
	bool& sentHitDict, 
	const std::unordered_set<std::string_view>& fuzzyOnlySyllables, 
	const bool shouldLimit) const
{
	if (!is_enable_fuzzy_)
		return;
	coral::phonetics::ParseSequences tmp;
	for (const auto& pinyin_sequence : pinyinSequences)
	{
		if (pinyin_sequence.is_match_english_vocab_)	// 英语不模糊音匹配
			continue;
		if (pinyin_sequence.from_ != 0 && pinyin_sequence.sequences_.size() == 1)
			continue;
		// 当拼音的长度过长时不进行模糊匹配
		if (pinyin_sequence.sequences_.size() > 5)
			continue;
		coral::phonetics::ParseTextViews seq = pinyin_sequence.sequences_;
		std::vector<coral::phonetics::ParseTextViews> fuzzySequences;
		if (coral::phonetics::syntax::is_match_fuzzy_correct(seq, fuzzySequences, syllables_view_, fuzzy_initials_, fuzzy_finals_, fuzzy_alls_))
		{
			coral::phonetics::ParseTextViews subSequence;
			for (const auto& fuzzySequenceItem : fuzzySequences)
			{
				subSequence.clear();
				if (coral::phonetics::syntax::is_mix_pinyin(fuzzySequenceItem, 0))
				{
					for (const auto& sequenceItem : fuzzySequenceItem)
					{
						coral::phonetics::TextSlice jianPin;
						if (!coral::phonetics::syntax::is_sheng_mu(sequenceItem) && coral::phonetics::syntax::get_jian_pin_code(sequenceItem, jianPin)) {
							subSequence.push_back(jianPin);
						}
						else {
							subSequence.push_back(sequenceItem);
						}
					}
				}
				else
				{
					subSequence = {};
				}
				coral::phonetics::PhoneticSequence newSeq(
					fuzzySequenceItem,
					subSequence,
					seq,
					seq,
					pinyin_sequence.from_,
					pinyin_sequence.to_,
					pinyin_sequence.path_nodes_,
					false,
					true,
					false);
				tmp.push_back(std::move(newSeq));
				
				bool isHitEnglish = false;
				bool spanHitDictPhrases = false;	// 模糊音查询不参与全句命中剪枝
				wordLattice.process_sequence(tmp.back(),
					lexiconmanager,
					fuzzyOnlySyllables,
					shouldLimit,
					sentHitDict,
					isHitEnglish,
					spanHitDictPhrases);
			}
		}
	}

	// 批量插入新生成的序列，避免扩容导致原有引用失效
	pinyinSequences.insert(pinyinSequences.end(), tmp.begin(), tmp.end());
}

// 检测是否为全简拼（每个字符都是合法声母的组合）
inline bool isAllJianpin(const std::string &input)
{
	// 空字符串或单字符不是全简拼
	if (input.length() <= 1)
		return false;

	// 检查每个字符是否都是合法声母
	for (char c : input)
	{
		if (coral::phonetics::syntax::kSoftInitials.count(std::string(1, c)) == 0)
		{
			return false;
		}
	}
	return true;
}

inline double getBayesianParameter(const std::string_view &name)
/** 
	贝叶斯参数获取函数 
	- rapidjson解析JSON配置文件 :
	- 文件只在网格搜素的时候被创建，编译不生成参考文档:
		https://365.coral.cn/l/ch7VT6Cfaa6L
**/
{
	static std::unordered_map<std::string_view, double> parameters = []()
	{
		// 默认参数值 - 最优参数组合
		std::unordered_map<std::string_view, double> params = {{"initialThreshold", 0.7},  {"decayRate", 0.16},		   {"baselineThreshold", 0.06},
															   {"priorMean", 0.1},		   {"priorVar", 2.0},		   {"maxProbThreshold", 0.95},
															   {"minProbThreshold", 0.72}, {"baseUncertainty", 0.035}, {"lengthFactor", 0.06},
															   {"lengthOffset", 0.2},	   {"transitionCenter", 10.0},  {"transitionSteepness", 0.2}};

		const std::string configFileName = "bayesian_params.json";
		if (!std::filesystem::exists(configFileName))
		{
			return params;
		}
		std::ifstream file(configFileName);
		if (!file.is_open())
		{
			return params;
		}
		std::string jsonContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();
		rapidjson::Document doc;
		doc.Parse(jsonContent.c_str());

		if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("bayesian_parameters"))
		{
			std::ofstream outFile(configFileName);
			if (outFile.is_open())
			{
				outFile << "{\n";
				outFile << "  \"parse_error\": \"json format error or miss bayesian_parameters node\",\n";
				outFile << "}\n";
				outFile.close();
			}
			return params;
		}
		const auto &bayesianParams = doc["bayesian_parameters"];
		if (!bayesianParams.IsObject())
		{
			return params;
		}
		for (const auto &[key, defaultValue] : params)
		{
			std::string keyStr(key);
			if (bayesianParams.HasMember(keyStr.c_str()) && bayesianParams[keyStr.c_str()].IsNumber())
			{
				params[key] = bayesianParams[keyStr.c_str()].GetDouble();
			}
		}

		return params;
	}();

	auto it = parameters.find(name);
	return (it != parameters.end()) ? it->second : 0.0;
}

inline bool shouldUnacceptCorrectionByBayesian(const double improvementRatio, const string_view &pystring)
{
	// 获取可配置的贝叶斯参数值
	const double &initialThreshold = getBayesianParameter("initialThreshold");
	const double &decayRate = getBayesianParameter("decayRate");
	const double &baselineThreshold = getBayesianParameter("baselineThreshold");
	const double &priorMean = getBayesianParameter("priorMean");
	const double &priorVar = getBayesianParameter("priorVar");
	const double &maxProbThreshold = getBayesianParameter("maxProbThreshold");
	const double &minProbThreshold = getBayesianParameter("minProbThreshold");

	const double requiredThreshold = (initialThreshold - baselineThreshold) * std::exp(-decayRate * pystring.size()) + baselineThreshold;

	const double &baseUncertainty = getBayesianParameter("baseUncertainty");
	const double &lengthFactor = getBayesianParameter("lengthFactor");
	const double &lengthOffset = getBayesianParameter("lengthOffset");
	const double observationSE = baseUncertainty + lengthFactor / (pystring.size() + lengthOffset);

	// 贝叶斯更新计算
	const double likelihoodVar = observationSE * observationSE;
	const double posteriorVar = 1.0 / (1.0 / priorVar + 1.0 / likelihoodVar);
	const double posteriorMean = posteriorVar * (priorMean / priorVar + improvementRatio / likelihoodVar);
	const double posteriorSD = std::sqrt(posteriorVar);

	// 计算 P(真实提升 > requiredThreshold | 观测数据)
	const double zScore = (requiredThreshold - posteriorMean) / posteriorSD;
	const double acceptanceProbability = 1.0 - 0.5 * (1.0 + std::erf(zScore / std::sqrt(2.0)));	 // 1 - Φ(z)

	// 概率阈值过渡参数 - 优化后改为可配置
	const double &transitionCenter = getBayesianParameter("transitionCenter");
	const double &transitionSteepness = getBayesianParameter("transitionSteepness");
	const double probThreshold =
		minProbThreshold + (maxProbThreshold - minProbThreshold) / (1.0 + std::exp(transitionSteepness * (pystring.size() - transitionCenter)));

	return acceptanceProbability < probThreshold;
}

std::vector<coral::phonetics::syntax::CorrectionResult> coral::phonetics::SyllableParser::generate_correction_string(const std::string& pystring, const size_t limit)
{
	if (!coral::Engine::compatInstance().config().pinyin_correction() || pystring.empty())
		return {};

	bool isPinyin = std::all_of(pystring.begin(), pystring.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');});
	if (!isPinyin)
		return {};
	
	if (isAllJianpin(pystring))
		return {};
	if (validPinyinCombines.find(pystring) != validPinyinCombines.end())
		return {};
	
	const auto &[originalSequence, originalScore] = pinyin_corrector_.predict(pystring, coral::phonetics::syntax::CorrectionMethod{coral::phonetics::syntax::CorrectionType::NONE, 0, '\0', {}, {}, 0.0f});
	bool justUseRulesCorrection = originalSequence.size() == 1;	 // 对于只是一个音节,只使用规则纠错，防止误纠
	const auto res = pinyin_corrector_.generate_all_corrections_with_details(pystring, originalSequence, !justUseRulesCorrection);

	std::vector<coral::phonetics::syntax::CorrectionResult> allResAndScore;
	allResAndScore.reserve(res.size());

	for (auto& [generatePinyin, generateCorrectionMethod] : res)
	{
		const auto& [pinyin_sequence, score] = pinyin_corrector_.predict(generatePinyin, generateCorrectionMethod);

		if (pinyin_sequence.empty() || (originalSequence.size() && pinyin_sequence.size() > originalSequence.size()))
			continue;
		allResAndScore.emplace_back(coral::phonetics::syntax::CorrectionResult{pinyin_sequence, score, generateCorrectionMethod});
	}
	if (allResAndScore.empty())
		return {};
	std::sort(allResAndScore.begin(), allResAndScore.end(), [](const auto& a, const auto& b) { return a.score > b.score; });

	if(allResAndScore.size() > limit)
		allResAndScore.resize(limit);
	// 如果某一项和下一项的权重（分数）差大于5，则再次截断
	if (!allResAndScore.empty())
	{
		size_t cutoffIndex = allResAndScore.size();
		for (size_t i = 0; i + 1 < allResAndScore.size(); ++i)
		{	
			if (std::abs(allResAndScore[i].score - allResAndScore[i+1].score) > 5.0)
			{
				cutoffIndex = i + 1;
				break;
			}
		}

		if (cutoffIndex < allResAndScore.size())
			allResAndScore.resize(cutoffIndex);
	}
#ifdef _DEBUG
	std::cout << "[Correction] Source : " << pystring << std::endl;
	for (const auto& resItem : allResAndScore)
	{
		std::cout << "	[Correction] Pinyin: " << hlp::stringJoinVec('\'', resItem.result)
			<< ", Score: " << resItem.score << ", Method: " << static_cast<int>(resItem.method.type) << std::endl;
	}
#endif
	return allResAndScore;
}

bool coral::phonetics::SyllableParser::finish_model_loading_locked()
{
    if (loading_pending_) {
        has_loaded_ = loading_tasks_.finish();
        loading_pending_ = false;
    }
    return has_loaded_;
}

bool coral::phonetics::SyllableParser::ensure_model_loaded_locked()
{
    if (finish_model_loading_locked()) return true;
    if (!retry_load_until_success_) return false;
    return load_locked(all_lexicon_path_, true, true);
}

bool coral::phonetics::SyllableParser::export_pinyin_sequences_to_dot(
	const coral::phonetics::ParseSequences& pinyinSequences,
	const std::filesystem::path& filepath)
{
#ifdef _DEBUG
	std::error_code ec;
	std::filesystem::create_directories(filepath.parent_path(), ec);
	if (ec)
		return false;

	std::ostringstream buffer;
	buffer << "digraph PinyinSequences {\n";
	buffer << "    charset=\"UTF-8\";\n";
	buffer << "    rankdir=LR;\n";
	buffer << "    compound=true;\n";
	buffer << "    node [shape=box, fontname=\"Microsoft YaHei\"];\n";
	buffer << "    edge [fontname=\"Microsoft YaHei\", penwidth=2];\n\n";

	std::set<int> positions;
	for (const auto& seq : pinyinSequences)
	{
		positions.insert(seq.from_);
		positions.insert(seq.to_);
	}

	buffer << "    // Position nodes\n";
	for (int pos : positions)
	{
		buffer << "    P" << pos << " [label=\"" << pos << "\", shape=circle, style=filled, fillcolor=lightblue];\n";
	}
	buffer << "\n";

	// 输出每个拼音序列作为边
	buffer << "    // Pinyin sequences as edges\n";
	for (size_t i = 0; i < pinyinSequences.size(); ++i)
	{
		const auto& seq = pinyinSequences[i];
		
		// 优先使用子码，否则使用查询码
		const auto& queryPinyin = seq.sub_sequences_.empty() ? seq.sequences_ : seq.sub_sequences_;
		
		std::ostringstream label;
		for (size_t j = 0; j < queryPinyin.size(); ++j)
		{
			if (j > 0) label << "'";
			label << queryPinyin[j];
		}
		
		std::string color = "black";
		if (seq.is_fuzzy_match_)
			color = "orange";
		else if (!seq.sub_sequences_.empty())
			color = "blue";
		else if (seq.is_match_english_vocab_)
			color = "green";
		
		buffer << "    P" << seq.from_ << " -> P" << seq.to_
			   << " [label=\"" << label.str() << "\", color=" << color << "];\n";
	}

	// 图例
	buffer << "\n    // Legend\n";
	buffer << "    subgraph cluster_legend {\n";
	buffer << "        label=\"Legend\";\n";
	buffer << "        fontsize=12;\n";
	buffer << "        color=gray;\n";
	buffer << "        style=dashed;\n";
	buffer << "        node [shape=plaintext, fontsize=10];\n";
	buffer << "        legend_black  [label=\"Black = Normal\"];\n";
	buffer << "        legend_orange [label=\"Orange = Fuzzy Match\"];\n";
	buffer << "        legend_blue   [label=\"Blue = Mixed Pinyin (has sub)\"];\n";
	buffer << "        legend_green  [label=\"Green = English Vocab\"];\n";
	buffer << "    }\n";

	buffer << "}\n";

	std::ofstream file{filepath, std::ios::binary};
	if (!file.is_open())
		return false;

	file << buffer.str();
	return true;
#endif
	return false;
}

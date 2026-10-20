#pragma once
#ifndef CORAL_MODULES_PHONETICS_PHONETIC_TYPES_H_INCLUDED
#define CORAL_MODULES_PHONETICS_PHONETIC_TYPES_H_INCLUDED

#include <math.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include "support/parse_arena.hpp"
#include "support/arena_string_set.hpp"
namespace coral::phonetics {
using Score = float;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
using PersonalLexiconIndex = int32_t;
} // 命名空间 coral::phonetics 结束（批次 3）

// (from, edgeIndex)。音节切分的路径只在单次解析内存活（Yen 的候选集、shortestPaths
// 都是 getPinyinSequences 的局部量），走 arena 省掉 Yen 迭代里成串的短 vector 堆分配
namespace coral::phonetics {
using LatticePath = std::vector<std::pair<int, int>, coral::phonetics::support::PoolAllocator<std::pair<int, int>>>;
} // 命名空间 coral::phonetics 结束（批次 3）


constexpr auto kMaxWeight = std::numeric_limits<coral::phonetics::Score>::max();

inline const coral::phonetics::Score kMathE = static_cast<coral::phonetics::Score>(exp(static_cast<long double>(1)));
inline constexpr coral::phonetics::Score kLogInfiniteSimal = static_cast<coral::phonetics::Score>(-230.25851f);
inline constexpr coral::phonetics::Score kMinProb = static_cast<coral::phonetics::Score>(-8.4f);	// Unigram 最小的概率
inline constexpr coral::phonetics::Score kMinBackprob = static_cast<coral::phonetics::Score>(-3.1f);	// Unigram 最小的回退概率
inline constexpr coral::phonetics::Score kDefaultLexiconEdgeWeight = static_cast<coral::phonetics::Score>(-30.0);	//默认边权重，用于既不是拼音又不是英语单词的情况
inline constexpr int kMaxCount = 1000000000; ;	// 一个词出现的最大次数
inline constexpr coral::phonetics::Score kInvMaxcount = static_cast<coral::phonetics::Score>(1.f / kMaxCount);
inline constexpr coral::phonetics::Score kDeeMax = static_cast<coral::phonetics::Score>(10000.f);

inline constexpr int kBufferSize = 4096;

// 候选词组装时用来根据权重判断是否需要添加的阈值
inline const std::unordered_map<size_t, coral::phonetics::Score> kCandidateProbabilityThresholds = { 
	{2,static_cast<coral::phonetics::Score>(-6.44)}, 
	{3,static_cast<coral::phonetics::Score>(-7.2863)},
	{4,static_cast<coral::phonetics::Score>(-8.243)}};

inline constexpr int kPinyinMaxLen = 6;
inline constexpr int kMinEnglishLexiconLength = 3;
inline constexpr int kMaxEnglishLexiconLength = 20;

inline constexpr int kMinPinyinLengthForExtendedMatch = 4;
inline constexpr int kMaxSyllableCombineLength = 4;	// 检查音节组合时的最大字符长度
inline constexpr size_t kMaxPersistentStringsSize = 1000;	// 英语词典的字符串池的最大容量
inline constexpr size_t kMinCloudLexiconQueryLength = 2;	// 云词库查询的最小拼音序列长度（单字不查询）

inline constexpr uint32_t kLexiconMaxSize = 10000000;	// 每个词典的最大容量，1千万
inline constexpr uint32_t kCloudBaseOffset = kLexiconMaxSize * 10;	// 云词典的ID起始值，1亿

inline constexpr uint32_t kInvalidPhraseId = std::numeric_limits<uint32_t>::max();  // 标识那些主词典中没有的词
inline constexpr uint32_t kExtendedMatchId = std::numeric_limits<uint32_t>::max() - 17;	// 扩展短语ID
inline constexpr uint32_t kEnglishLexiconId = std::numeric_limits<uint32_t>::max() - 23;
// 首解时最多组装多少条主词典直连候选。实测一次解析要建 150~370 条，而用户一页只看 7~9 条，
// 绝大多数是永远不会被翻到的长尾。按 comp 的排序键取前 N 条，能保证候选列表的前 N 条与不设
// 上限时完全一致，约等于 7~9 页；用户真翻过这个深度时 getParseMode 会转入 Reparse，
// 用 UNLIMITED_DIRECT_CANDIDATES 重解析把剩下的长尾追加进缓存。
inline constexpr int kFirstPassDirectCandidateBudget = 64;
inline constexpr int kUnlimitedDirectCandidates = -1;
// 单个声母属于纯简拼查询。候选窗每页最多展示 7 个，预取 10 页即可覆盖常用字，
// 更深的低频长尾既没有实际展示价值，也会显著放大单键输入时的检索和排序开销。
inline constexpr int kSingleJianpinCandidateLimit = 7 * 10;

inline constexpr int kMiddlewareDefaultInsertPosition = 1;  // 默认插入位置
inline constexpr int kMiddlewareDefaultMaxInsertCount = 1;  // 默认最大插入数量
inline constexpr int kMiddlewareSymbolMaxInsertCount = 3;   // 符号中间件最大插入数量

inline constexpr coral::phonetics::Score kPredictThreshold = -14.03865f; // 输入预测时候选词词频权重小于这个值时认为不常见，采用二元模型代替
inline constexpr coral::phonetics::Score kConditionRatio = 0.33f; //条件输入的比率，比如条件是我的时候命中了词库"我感觉"，这时候推荐出来的"感觉"的权重是我感觉的权重乘以这个值
inline constexpr coral::phonetics::Score kAbbrPenaltyValue = -7.f;	//简拼惩罚，简拼和全拼一起出现时给简拼一个轻微的惩罚

inline constexpr coral::phonetics::Score kFuzzyPenaltyJian = -3.f;	//模糊音简拼惩罚，当简拼涉及到模糊音的时候给一个较大的惩罚，防止wuh 武汉被错误的模糊为无法
inline constexpr coral::phonetics::Score kFuzzyPenaltyQuan = -1.f;	//模糊音全拼惩罚，当全拼涉及到模糊音时给简拼一个轻微的惩罚，保证用户的模糊音不会因为惩罚而无法展示在首页

//拼音切分相关
inline constexpr int kMaxTopk = 3;
inline constexpr int kMaxPathDist = 133;
inline constexpr int kFullCost = 50;
inline constexpr int kJianCost = 111;
inline constexpr int kShenmuCost = 133;
inline constexpr int kEnglishCost = 70;
inline constexpr int kInvalidCost = 9999; //uvi 作为音节时取一个极大值

// ===== 方案B第一阶段：模糊音并入音节图 =====
// 模糊边 = 在原始合法音节边之上再挂一条平行边，phrase 为模糊替换后的查询音节。
// 惩罚叠加在原边权重之上，保证同等条件下模糊切分排在非模糊切分之后、但先于
// 无效切分。具体数值待 A/B 调参（需与 MAX_PATH_DIST 交互验证：惩罚过大时
// 含模糊边的路径可能被 Yen KSP 的 dist 上限直接排除）。
inline constexpr int kFuzzyEdgePenalty = 60;
// 默认在图内建模糊边（新路径）；CORAL_FUZZY_IN_GRAPH=0 回退到解析后 fuzzyReplace
// 的旧路径，便于 A/B 对比正确率与性能。
inline bool fuzzy_in_graph_enabled()
{
	static const bool enabled = [] {
		const char* env = std::getenv("CORAL_FUZZY_IN_GRAPH");
		return !(env && env[0] == '0');
	}();
	return enabled;
}

// ===== 方案B第二阶段：编辑距离纠错边 =====
// 编辑边 = 对原始输入窗口做单次编辑（swap/delete/insert/replace）后形成合法音节
// 的平行边。图代价惩罚叠加在音节基础 cost 之上：必须小到能让 afwo 的编辑路径
// fa'wo(50+P+50) 压过原始切分 a'f'wo(50+133+50=233)，即 P<133；同时越大越不易
// 误纠。与旧纠错同闸门：仅当输入不是合法拼音前缀（!isValidPinyinPrefix）时生成，
// 正常输入与打字中途状态零影响。
inline constexpr int kEditEdgePenalty = 133;
// 编辑纠错 span 命中词条后在词弧上加的权重惩罚（同 Fuzzy_PENALTY_* 的用法），
// 保证同等词频下精确匹配优先于纠错候选
inline constexpr coral::phonetics::Score kEditPenalty = -4.f;
// 默认开启图内编辑纠错；CORAL_EDIT_IN_GRAPH=0 回退到旧 generateCorrectionString 路径
inline bool edit_in_graph_enabled()
{
	static const bool enabled = [] {
		const char* env = std::getenv("CORAL_EDIT_IN_GRAPH");
		return !(env && env[0] == '0');
	}();
	return enabled;
}

// 含有这些字符的词语拼音会在首位添加原始拼音输入
// 其余符号会正常参与解析插入到候选词中间，或被TSF拦截
inline constexpr std::string_view kPinyinPassthroughDelimiters = "@#%-_;:/";

namespace coral::phonetics {
using LatticeIndex = int;
} // 命名空间 coral::phonetics 结束（批次 3）
  //后续需要改为全局的uint16_t

// 解析期临时容器：内存来自 ParseArena，随 ParseArenaScope 一并回收，
// 因此绝不能被跨解析存活的对象持有
namespace coral::phonetics {
using ParseTextViews = std::vector<std::string_view, coral::phonetics::support::PoolAllocator<std::string_view>>;
} // 命名空间 coral::phonetics 结束（批次 3）


// arena 分配的临时容器类型别名（单次解析生命周期，不可跨解析持有）
namespace coral::phonetics {
using ParseStrings = std::vector<std::string, coral::phonetics::support::PoolAllocator<std::string>>;
} // 命名空间 coral::phonetics 结束（批次 3）

// 句子去重集在解析期的插入/查询极频繁，用 arena 上的开放寻址表而非节点式容器
namespace coral::phonetics {
using ParseTextSet = coral::phonetics::support::ArenaStringSet;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
using ParseIntegers = std::vector<int, coral::phonetics::support::PoolAllocator<int>>;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
using ParseScoreMap = std::unordered_map<uint32_t, float, std::hash<uint32_t>, std::equal_to<uint32_t>, coral::phonetics::support::PoolAllocator<std::pair<const uint32_t, float>>>;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
using ParseIndexMap = std::unordered_map<uint32_t, std::pair<int, uint16_t>, std::hash<uint32_t>, std::equal_to<uint32_t>, coral::phonetics::support::PoolAllocator<std::pair<const uint32_t, std::pair<int, uint16_t>>>>;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
using ParseOccurrenceMap = std::map<std::string, std::vector<size_t>, std::less<std::string>, coral::phonetics::support::PoolAllocator<std::pair<const std::string, std::vector<size_t>>>>;
} // 命名空间 coral::phonetics 结束（批次 3）


// 与 stringViewVec 同构，但走默认分配器，供需要跨解析存活的成员使用
namespace coral::phonetics {
using PersistentTextViews = std::vector<std::string_view>;
} // 命名空间 coral::phonetics 结束（批次 3）


#endif // DEFINDES_H

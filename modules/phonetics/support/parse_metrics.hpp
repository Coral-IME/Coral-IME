#pragma once
#ifndef CORAL_MODULES_PHONETICS_SUPPORT_PARSE_METRICS_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_SUPPORT_PARSE_METRICS_HPP_INCLUDED

// 临时的工作量计数器：只统计"做了多少次"，不测时间。
// 用来对比不同实现的工作量级差，定位是"单次慢"还是"次数多"。
// 定为 0 时全部编译掉，不留任何运行时痕迹。
#ifndef LANGENGINE_PERF_COUNTERS
#define LANGENGINE_PERF_COUNTERS 0
#endif

#if LANGENGINE_PERF_COUNTERS

#include <cstdint>
#include <cstdio>

namespace coral::phonetics::support
{
	struct PerfCounters
	{
		uint64_t parseCalls = 0;
		uint64_t spanQueries = 0;		 // CLexiconManager::getPhrases 调用次数
		uint64_t spanQueriesPruned = 0;	// 全句命中剪枝跳过的 span 查询数
		uint64_t fuzzyEdgesAdded = 0;	// 方案B第一阶段：音节图新增的模糊平行边数
		uint64_t fuzzySpansBuilt = 0;	// 含模糊边产出的 span 序列数
		uint64_t editEdgesAdded = 0;	// 方案B第二阶段：音节图新增的编辑纠错边数
		uint64_t editSpansBuilt = 0;	// 含编辑边产出的 span 序列数
		uint64_t primaryLookups = 0;
		uint64_t primaryFilterPass = 0;
		uint64_t primaryHits = 0;
		uint64_t combinedLookups = 0;
		uint64_t combinedFilterPass = 0;
		uint64_t combinedHits = 0;
		uint64_t cloudLookups = 0;
		uint64_t cloudHits = 0;
		uint64_t predictiveCalls = 0;
		uint64_t phrasesEmitted = 0;	 // 各词典写入 phraseVec 的条目总数
		uint64_t phrasesAfterLimit = 0;	 // 排序截断后剩下的
		uint64_t arcsCreated = 0;
		uint64_t sequencesBuilt = 0;	 // getPinyinSequences 产出的 span 数
		uint64_t decoderPops = 0;
		uint64_t decoderPushes = 0;
		uint64_t decoderSolutions = 0;
		uint64_t sentHitDictTrue = 0;
		uint64_t spanUnlimited = 0;			 // limit < 0 的 span 数
		uint64_t phrasesFromUnlimited = 0;	 // 这些 span 产出的词条数
		uint64_t phrasesOver50 = 0;			 // 单 span 产出中超过第 50 条的部分
		uint64_t spanBucket[8] = { 0 };		 // 按产出条数分桶：0,1-4,5-16,17-64,65-256,257-1024,1025-4096,更多
		uint64_t arcDupSamePhrase = 0;		 // (from,to,phrase) 重复的弧数
		uint64_t arcDupSameAll = 0;			 // (from,to,phrase,weight) 也相同的弧数

		~PerfCounters()
		{
			const double n = parseCalls ? static_cast<double>(parseCalls) : 1.0;
			std::fprintf(stderr,
				"\n===== perf counters (total / per-parse) =====\n"
				"parseCalls          %12llu\n"
				"sequencesBuilt      %12llu  %10.2f\n"
				"spanQueries         %12llu  %10.2f\n"
				"spanQueriesPruned   %12llu  %10.2f\n"				"fuzzyEdgesAdded     %12llu  %10.2f   fuzzySpansBuilt %llu\n"				"editEdgesAdded      %12llu  %10.2f   editSpansBuilt %llu\n"
				"primaryLookups      %12llu  %10.2f   filterPass %llu  hits %llu\n"
				"combinedLookups     %12llu  %10.2f   filterPass %llu  hits %llu\n"
				"cloudLookups        %12llu  %10.2f   hits %llu\n"
				"predictiveCalls     %12llu  %10.2f\n"
				"phrasesEmitted      %12llu  %10.2f\n"
				"phrasesAfterLimit   %12llu  %10.2f\n"
				"arcsCreated         %12llu  %10.2f\n"
				"decoderPops         %12llu  %10.2f\n"
				"decoderPushes       %12llu  %10.2f\n"
				"decoderSolutions    %12llu  %10.2f\n"
				"sentHitDictTrue     %12llu  %10.2f%%\n"
				"spanUnlimited       %12llu  %10.2f\n"
				"phrasesFromUnlimited%12llu  %10.2f\n"
				"phrasesOver50       %12llu  %10.2f\n"
				"spanBuckets  0:%llu  1-4:%llu  5-16:%llu  17-64:%llu  65-256:%llu  257-1k:%llu  1k-4k:%llu  4k+:%llu\n"
				"arcDupSamePhrase    %12llu  %10.2f\n"
				"arcDupSameAll       %12llu  %10.2f\n"
				"=============================================\n",
				(unsigned long long)parseCalls,
				(unsigned long long)sequencesBuilt, sequencesBuilt / n,
				(unsigned long long)spanQueries, spanQueries / n,
				(unsigned long long)spanQueriesPruned, spanQueriesPruned / n,
				(unsigned long long)fuzzyEdgesAdded, fuzzyEdgesAdded / n,
				(unsigned long long)fuzzySpansBuilt,
				(unsigned long long)editEdgesAdded, editEdgesAdded / n,
				(unsigned long long)editSpansBuilt,
				(unsigned long long)primaryLookups, primaryLookups / n,
				(unsigned long long)primaryFilterPass, (unsigned long long)primaryHits,
				(unsigned long long)combinedLookups, combinedLookups / n,
				(unsigned long long)combinedFilterPass, (unsigned long long)combinedHits,
				(unsigned long long)cloudLookups, cloudLookups / n, (unsigned long long)cloudHits,
				(unsigned long long)predictiveCalls, predictiveCalls / n,
				(unsigned long long)phrasesEmitted, phrasesEmitted / n,
				(unsigned long long)phrasesAfterLimit, phrasesAfterLimit / n,
				(unsigned long long)arcsCreated, arcsCreated / n,
				(unsigned long long)decoderPops, decoderPops / n,
				(unsigned long long)decoderPushes, decoderPushes / n,
				(unsigned long long)decoderSolutions, decoderSolutions / n,
				(unsigned long long)sentHitDictTrue, 100.0 * sentHitDictTrue / n,
				(unsigned long long)spanUnlimited, spanUnlimited / n,
				(unsigned long long)phrasesFromUnlimited, phrasesFromUnlimited / n,
				(unsigned long long)phrasesOver50, phrasesOver50 / n,
				(unsigned long long)spanBucket[0], (unsigned long long)spanBucket[1],
				(unsigned long long)spanBucket[2], (unsigned long long)spanBucket[3],
				(unsigned long long)spanBucket[4], (unsigned long long)spanBucket[5],
				(unsigned long long)spanBucket[6], (unsigned long long)spanBucket[7],
				(unsigned long long)arcDupSamePhrase, arcDupSamePhrase / n,
				(unsigned long long)arcDupSameAll, arcDupSameAll / n);
			std::fflush(stderr);
		}
	};

	inline PerfCounters& perf()
	{
		static PerfCounters counters;
		return counters;
	}
}

#define PERF_ADD(field, n) (coral::phonetics::support::perf().field += (n))
#define PERF_INC(field) (++coral::phonetics::support::perf().field)

#else

#define PERF_ADD(field, n) ((void)0)
#define PERF_INC(field) ((void)0)

#endif // LANGENGINE_PERF_COUNTERS（性能计数器宏）

#endif // PERFCOUNTERS_HPP（性能计数器头文件）

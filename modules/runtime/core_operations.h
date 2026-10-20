/*
 * @Autor: huangzhishou
 * @Date: 2021-05-18 14:32:45
 */
#pragma once
#ifndef CORAL_MODULES_RUNTIME_CORE_OPERATIONS_H_INCLUDED
#define CORAL_MODULES_RUNTIME_CORE_OPERATIONS_H_INCLUDED

#include <string>
#include <vector>
#include "../phonetics/phonetic_types.h"
#include "coral/vocabulary/lexicon_types.h"
#include "coral/vocabulary/lexicon_import.h"
#include "coral/vocabulary/phrase_override.h"
#include "filesystem"
#include "../phonetics/pronunciation_labeler.h"

namespace coral::runtime {
using StringOutputIterator = std::back_insert_iterator<std::vector<std::string>>;
	/*
		需求：初始化引擎，指定日志目录，用户词典目录
		coralDir: CORAL文件数据目录
		bSync: 加载词库是否同步; false 启动线程加载词库
	*/
	bool start_runtime(const char* const coralDir, bool bSync = false);
	void stop_runtime();
	// 进程内会话适配器的就绪探测。
	bool runtime_ready();
    // 进程内单调递增标识，在调用方的运行时门内读取。
    std::uint64_t runtime_generation();

	bool compile_resource_files(
		// 主词库 (Primary Pinyin)
		const char* primaryInputPath,
		const char* primaryTriePath,
		const char* primaryInfoPath,

		// U模式词库1 (Bushou / Radicals)
		const char* bushouInputPath,
		const char* bushouTriePath,
		const char* bushouInfoPath,

		// U模式词库2 (Stroke)
		const char* strokeInputPath,
		const char* strokeTriePath,
		const char* strokeInfoPath,

		// 英文词典 (English Lexicon)
		const char* englishInputPath,
		const char* englishTriePath,
		const char* englishInfoPath,

		const char* modelUnigramFile,
		const char* modelBigramFile,
		const char* langModelOutputDir,

		//用于预测的中文词典
		const char* cnPrimaryInputPath,
		const char* cnPrimaryTriePath,
		const char* cnPrimaryInfoPath,
		const char* originalPrimaryPath
	);

	/*
		需求：查询 “我” 前置条件下 "shishui" 拼音串的前 2 的结果
		condition:  我
		str: shishui
		output: ["是谁", "是水"] # 返回结果的存储首地址
		pyOutput: ["shi'shui", "shi'shui"] #拼音拆分结果的存储首地址, 与 output 一一对应
		offset:获取候选词开始索引位置,比如一次获取候选词个数比如是7个,第一次是0, 下一页是7
		size: 一次显示候选词个数
		rowlen: 100 # 每一个结果最多可存储的宽字符数
		counts: 2 # 实际返回多少个结果
		说明：内部适配保留既有解析参数，不构成 DLL ABI。
		      旧签名中的 conditionPinyin / isFixedOutput / infos 三个参数已屏蔽，
		      依赖它们的功能（条件拼音候选、固定候选位、词语来源信息输出）暂不开放。
		asciiMode: 0是全拼 1是双拼
	*/
	bool parse_candidates(
		const char* const condition,
		const char* const str,
		StringOutputIterator output,
		StringOutputIterator pyOutput,
		int offset,
		int size,
		int rowlen,
		int* counts,
		int asciiMode = 2
	);

	void clear_parse_cache();
	// 最近一次解析中绝对候选索引的可选标签。
	// 为空表示上屏文本同时就是显示文本。
	std::string candidate_display_label(int index);

	/*
		功能：更新用户自定义词典
		py:  wo'shi'shui #解析的拼音
		sent: 我'是'谁 #选定的句子
		为了参数对齐使用string传参，如果后续需要跨编译器或者debug release混用可以考虑改为char*传参
	*/
	// conditionId 出参：返回被选句子的phraseId（无有效选中时为INVALID_PHRASE_ID），供TSF连续输入时作为条件输入前置词
	void remember_candidate(const std::string& py, const std::string& sent, uint32_t& conditionId);

	/*
		功能：将内存中的用户词典落盘
	*/
	bool flush_user_lexicon();

	bool maintain_lexicon();

	/**
	 *  清空用户词典
	 */
	bool clear_user_lexicon();

	/**
	 *  根据目录重新构建聚合的细胞词库
	 */
	bool compile_lexicon_directory(const char* const dirPath);

	/*
		需求：云引擎动态插入词条
		pinyinCode: wo
		quanPyCode: wo
		sent: 我
		dee: 1.0
		tickTime: 153535 # 词条入库秒时间戳
	*/
	bool append_external_phrase(
		const char* const pyCode,
		const char* const quanPyCode,
		const char* const sent,
		coral::phonetics::Score dee,
		int tickTime);

	void begin_external_phrases();
	void end_external_phrases();

	//将主程序的设置类加载到引擎


	/*
		功能：更新云词库状态，加载、卸载云词库
		调用时机：每次焦点切换到输入进程时
	*/
	void refresh_lexicon_state();

	/*
		需求：查询 “我” 前置条件下 "shishui" 拼音串的前 2 的结果
		condition:  我
		str: shishui
		output: ["是谁", "是水"] # 返回结果的存储首地址
		pyOutput: ["shi'shui", "shi'shui"] #拼音拆分结果的存储首地址, 与 output 一一对应
		offset:获取候选词开始索引位置,比如一次获取候选词个数比如是7个,第一次是0, 下一页是7
		size: 一次显示候选词个数
		rowlen: 100 # 每一个结果最多可存储的宽字符数
		counts: 2 # 实际返回多少个结果
		说明：内部行缓冲适配保留既有 9 个参数，
		      旧签名中的 conditionPinyin / infos 两个参数已屏蔽。
		asciiMode: 0是全拼 1是双拼
	*/
	bool parse_candidate_rows(
		const char* const condition,
		const char* const str,
		char** output,
		char** pyOutput,
		int offset,
		int size,
		int rowlen,
		int* counts,
		int asciiMode
	);

	// 获取所有完整拼音划分结果
	int read_pinyin_splits(
		const char* const str,
		char* pyOutput,
		int outputSize
	);
	
	void read_next_page_state(bool& existNextPage);

	// 撤销上一次加入用户词典的词语
	bool revert_phrase_learning(const std::string& delPhrase);
	// 导入用户词典
	hlp::ImportResult import_user_file(const std::filesystem::path& file);
	// 导出用户词典
	bool export_user_file(const std::filesystem::path& file);
	// 导入预览：规范化/自动生成拼音并查重，结果写回 items；词库未装载完成时返回 false
	bool prepare_user_import(std::vector<UserLexiconImportItem>& items, const UserLexiconImportOptions& options);
	// 写入 status 为 Ready 的词条并立即落盘
	UserLexiconImportReport commit_user_import(const std::vector<UserLexiconImportItem>& items);
	// 撤销最近一次导入新增的词条
	bool revert_user_import();
	// 导出纠错串
	bool write_correction_trace(
        const char* const pystring,
        unsigned int limit);
	int read_correction_suggestions(const char *const pinyin, int maxSuggestions, char *output, int outputSize);
    // 不进行纠错的拼音解析接口
	bool parse_uncorrected_rows(const char *const condition, const char *const str, char **output, char **pyOutput, int offset,
														int size, int rowlen, int *counts, int asciiMode);
	bool apply_candidate_rule(
		const std::string& pinyin,
		const std::string& word,
        coral::CandidateAction action,
        coral::LanguageVariant variant
	);

	// 常用语：code 为小写字母输入码，命中后排在候选首位；简繁模式共用
	bool add_phrase_shortcut(const std::string& code, const std::string& phrase);
	bool remove_phrase_shortcut(const std::string& code, const std::string& phrase);
	// 按添加时间从新到旧返回；词库未装载完成时返回 false
	bool list_phrase_shortcuts(std::vector<CustomPhraseEntry>& phrases);

	//接口开放给android
	void set_runtime_traditional(bool mode);
	bool runtime_traditional();

	// 全半角设置（运行态同步）
	void set_runtime_full_width(bool mode);
	bool runtime_full_width();
	// SessionPort 使用的整包配置入口；在引擎边界一次性替换快照。
	void apply_runtime_settings(
		bool isUMode, bool isVMode, bool pinyin_correction,
		bool isFullWidth, bool isTraditionalChinese, bool isShowInfos,
		int limit_count, int combined_lexicon_version,
		const int* fuzzy_choices, int fuzzyChoiceCount, bool smart_prediction = true);
	void refresh_runtime_settings();

	// 解析拼音，保存拼音拆分结果到文件
	bool write_parse_trace(const char *str, const char *saveDir);

	
	// 注音功能相关接口
	// 初始化注音器
	int load_pronunciation_index(const char* const lSortFile);
	
	// 为词语注音，返回所有可能的读音结果
	// annotateOnFullMatch: 完全命中时是否直接注音，true表示直接注音，false表示继续处理
	// 返回结果：所有结果序列化到 fullPinyinOutput 中（用换行符分隔，每个结果用制表符分隔字段）
	bool read_pronunciation_rows(
		const char* const word,
		char* fullPinyinOutput,          // 所有结果的序列化字符串（主要输出）
		int fullPinyinOutputSize,
		bool annotateOnFullMatch
	);
	
	// 批量注音，支持1.传递一个文件名；2.传递一个vector数组
	// annotateOnFullMatch: 完全命中时是否直接注音，true表示直接注音，false表示继续处理
	bool annotate_word_batch(
		const char* const inputCsvFile,
		const char* const outputCsvFile,
		const std::vector<std::string>* words = nullptr,
		std::vector<coral::phonetics::PronunciationResult>* results = nullptr,
		bool annotateOnFullMatch = true
	);
}

#endif // CORAL_MODULES_RUNTIME_CORE_OPERATIONS_H_INCLUDED

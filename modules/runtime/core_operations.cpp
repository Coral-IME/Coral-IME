/*
 * @Autor: huangzhishou
 * @Date: 2021-05-18 14:22:12
 */
#include "pch.h"
#include "core_operations.h"
#include "runtime_context.h"
#include "mode_selection.h"
#include "../phonetics/latin_lexicon.h"
#include "../phonetics/context_scorer.h"
#include "../phonetics/syllable_parser.h"
#include "../phonetics/system_lexicon.h"
#include "script_conversion.h"
#include "ini_store.h"
#include "preference_registry.h"
#include "shape_lexicon.h"
#include "../phonetics/lexicon_importer.h"

namespace {
bool start_runtime_impl(const char* coralDir, bool bSync)
{
	LOG_INFO("Langengine: Engine initialization started");
	// Windows：系统词库从安装目录读取；用户数据从 coralDir(LocalLow) 读取
	// macOS/Android/其他：coralDir 由调用方传入，保持旧逻辑不依赖 getInstallPath()
#ifdef _WIN32
	filesystem::path installPath = hlp::getInstallPath();
	// 测试/性能测量用：设置 CORAL_LANGENGINE_DATA_DIR（指向含 data/ 的运行资源目录）时
	// 覆盖注册表安装路径，避免引擎读到正式安装目录导致 A/B 对比失效。
	if (const char* envDataDir = std::getenv("CORAL_LANGENGINE_DATA_DIR"); envDataDir && *envDataDir)
	{
		installPath = filesystem::path(envDataDir);
		LOG_INFO("Langengine: CORAL_LANGENGINE_DATA_DIR override active: " + installPath.string());
	}
	filesystem::path userDataPath = coralDir;
	filesystem::path dataDirPath = installPath / "data";
	filesystem::path configiniPath = userDataPath / "config" / "coralpinyinConfig.ini";
#else
	filesystem::path coralDirPath = coralDir;
	filesystem::path dataDirPath = coralDirPath / "data";
	filesystem::path configiniPath = coralDirPath / "config" / "coralpinyinConfig.ini";
#endif

#if defined(__ANDROID__)
	hlp::setLocalLowCoralPinyinPathForAndroid(coralDirPath);
#endif
	// tsf在这里不会重复初始化日志，tsf+Langengine共用一个loginit
	// 这里是预留给coralpinyin和demo链接的Langengine进行初始化的地方
	hlp::initCoralPinyinLog(hlp::LogPathType::AppType);

	// 必须先于任何 getSettingStore()/getCloudLexiconPath() 调用初始化 SettingsConfig，
	// 否则 m_configStore 会以空 m_iniFilePath 提前创建（对照 wpstsf 版 start_runtime 的顺序）。
	sConfig->prepare_runtime_settings(configiniPath.string());

	// 用户词典路径：
	// macOS：固定放在用户目录
	// Windows：放在用户数据目录（coralDir/LocalLow）
	// Android/其他：旧逻辑（Android没有用户词典，逻辑暂时保留）
#if defined(__APPLE__)
	filesystem::path userLexiconFile = hlp::getLocalLowCoralPinyinPath() / "userlexicon.coral";
#elif defined(_WIN32)
	filesystem::path userLexiconFile = userDataPath / "userlexicon.coral";
#else
	filesystem::path userLexiconFile = coralDirPath / "userlexicon.coral";
#endif

	// 平台路径解析完成后，加载编排交由 coral::Engine（Phase 3 双轨：
	// 旧 API 兼容单例持有 Engine，后续逐步由宿主显式构造注入）
	coral::EngineOptions engineOptions{dataDirPath, configiniPath, userLexiconFile, bSync};
#if defined(_WIN32) || defined(_MSC_VER)
	// 词库装载策略由边界按运行模式决定（引擎核心保持平台无关）：
	// - UWP 服务模式（宿主只开共享内存）：同步装载 + 失败后在解析入口重试直至成功
	// - UWP 独立模式：后台异步装载
	// - 非 UWP：沿用调用方传入的 bSync
	const hlp::UWPRunMode uwpMode = hlp::getUWPRunMode();
	if (uwpMode == hlp::UWPRunMode::ServiceAvailable) {
		engineOptions.syncLoad = true;
		engineOptions.retryModelLoadUntilSuccess = true;
	} else if (uwpMode == hlp::UWPRunMode::Standalone) {
		engineOptions.syncLoad = false;
	}
#endif
	return coral::Engine::compatInstance().init(engineOptions);
}
} // namespace

bool coral::runtime::start_runtime(const char* const user_data_path, bool synchronous_load)
{
    // 在更改设置或日志后端之前，先拒绝重复启动。
    if (!user_data_path || !*user_data_path || runtime_ready()) return false;
    try { return start_runtime_impl(user_data_path, synchronous_load); }
    catch (...) { stop_runtime(); return false; }
}

void coral::runtime::stop_runtime()
{
	// 调用方停止请求后，销毁模型并回收加载、学习及日志工作
	coral::Engine::compatInstance().shutdown();
}

bool coral::runtime::runtime_ready()
{
	return coral::Engine::compatInstance().initialized();
}

std::uint64_t coral::runtime::runtime_generation()
{
    return coral::Engine::compatInstance().generation();
}

// 初始化设置项参数

bool coral::runtime::parse_candidates(
	const char *const condition,
	const char *const str,
	StringOutputIterator output,
	StringOutputIterator pyOutput,
	int offset,
	int size,
	int rowlen,
	int *counts,
	int asciiMode)
{
#if defined(__ANDROID__)
	hlp::initCoralPinyinLog(hlp::LogPathType::AppType);
#endif
	// 边界统一推送配置快照（覆盖跨进程设置变更；等价旧的 sConfig 读穿透）
	coral::Engine::compatInstance().refreshConfig();
	if (!str || rowlen < 1 || !counts) {
		return false;
	}
	bool isok = false;
	string strPinyin = str;
	if (strPinyin.empty())
		return isok;
	vector<string> res;
	vector<string> pyRes;
	// 旧签名参数已屏蔽：固定候选位(isFixedOutput)与词语来源信息(infos)不再对外输出，
	// 内部仍以局部变量传给 parsePinyinString 后丢弃
	vector<int> isFixedRes;
	int row = 0;

	*counts = 0;
	vector<coral::phonetics::CandidateResult> sentsCache;
	if (!coral::phonetics::SyllableParser::instance().parse_input(strPinyin, res, pyRes, isFixedRes, offset, size, asciiMode, kInvalidPhraseId, condition ? condition : ""))
		return isok;
	for (auto it = res.begin(); it != res.end(); ++it)
	{
		if (row >= size)
		{
			break;
		}
		if (static_cast<int>(it->size()) >= rowlen)
			return isok;
		*output = *it;
		output++;
		*pyOutput = pyRes[row];
		pyOutput++;
		if (!it->empty())
		{
			*counts += 1;
		}
		row++;
	}
	return true;
}

std::string coral::runtime::candidate_display_label(int index)
{
	return coral::phonetics::SyllableParser::instance().candidate_display_text(index);
}

void coral::runtime::clear_parse_cache()
{
	coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();
}

void coral::runtime::remember_candidate(const string &py, const string &sent, uint32_t& conditionId)
{
	string updateSent;
	conditionId = kInvalidPhraseId;
	if (!sent.empty())
	{
		updateSent = sent;
		// vector<string> resSequences; // 去除结果中的分隔符"'"
		// hlp::splitString(updateSent, "'", resSequences);
		// string res;
		// res = hlp::stringJoinVec("", resSequences);
		int selectedIndex = -1;
		coral::phonetics::SyllableParser::instance().check_update_lexicon(sent, updateSent, selectedIndex);
		// 条件输入：从本次解析缓存中取被选句子的phraseId，TSF在连续输入时回传作为前置条件
		conditionId = coral::phonetics::SyllableParser::instance().capture_condition_selection(selectedIndex);
	}
	coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();
	if (!updateSent.empty())
		coral::phonetics::SyllableParser::instance().update_user_lexicon(py, updateSent);
}

bool coral::runtime::maintain_lexicon()
{
	return coral::phonetics::SyllableParser::instance().maintain_lexicon();
}

bool coral::runtime::flush_user_lexicon()
{
	return coral::phonetics::SyllableParser::instance().store_lexicon();
}

bool coral::runtime::clear_user_lexicon()
{
	return coral::phonetics::SyllableParser::instance().clear_lexicon();
}

bool coral::runtime::compile_lexicon_directory(const char* const dirPath)
{
	if (!dirPath)
		return false;

	std::filesystem::path fsPath;
#if defined(_WIN32) || defined(_MSC_VER)
	// Windows下需要转换为宽字符，再转换为std::filesystem::path
	try
	{
		std::wstring w = hlp::str2wstr(std::string(dirPath));
		fsPath = std::filesystem::path(w);
	}
	catch (...)
	{
		return false;
	}
#else
	fsPath = std::filesystem::path(dirPath);
#endif

	coral::phonetics::SystemLexicon builder("combinedLexicon");
	if (builder.compile_lexicon_directory(fsPath))
	{
		// 组合词库版本推进：持久化 + 快照推送（等价旧 advanceCombinedLexiconVersion）
		sConfig->setCombinedLexiconVersion(sConfig->getCombinedLexiconVersion() + 1);
		coral::Engine::compatInstance().refreshConfig();
		return true;
	}
	return false;
}

bool coral::runtime::append_external_phrase(const char *const pyCode, const char *const quanPyCode, const char *const sent,
														  coral::phonetics::Score dee, int tickTime)
{
	bool isok = false;
	RECORD_TRACE(&isok, hlp::formatString("pinyinCode=%s, quanPyCode=%s, sent=%s", pyCode ? pyCode : "null", quanPyCode ? quanPyCode : "null",
										  sent ? sent : "null"));
	if (!(pyCode && quanPyCode && sent && dee >= 0.0f && tickTime > 0))
		return isok;
	if (!coral::phonetics::SyllableParser::instance().load_user_lexicon_by_line(pyCode, quanPyCode, sent, dee, tickTime))
		return isok;
	return true;
}

void coral::runtime::begin_external_phrases()
{
	coral::phonetics::SyllableParser::instance().begin_load_user_lexicon_by_line();
}

void coral::runtime::end_external_phrases()
{
	coral::phonetics::SyllableParser::instance().end_load_user_lexicon_by_line();
}


void coral::runtime::refresh_lexicon_state()
{
	LOG_TRACE("Focus changed, Updating Cloud Lexicons");
	// 焦点切换：重建配置快照（覆盖跨进程设置变更），云词库配置由边界读出后注入
	coral::Engine::compatInstance().refreshConfig();
}

bool coral::runtime::parse_candidate_rows(const char *const condition
	, const char *const str
	, char **output
	, char **pyOutput
	, int offset
	, int size
	, int rowlen
	, int* counts
	, int asciiMode)
{
	bool isok = false;
	RECORD_TRACE(&isok, hlp::formatString("prev_pinyin=%s, next_pinyin=%s", condition ? condition : "null", str ? str : "null"));
	// 边界统一推送配置快照（覆盖跨进程设置变更；等价旧的 sConfig 读穿透）
	coral::Engine::compatInstance().refreshConfig();
	{
		if (!str || rowlen < 1 || !counts) {
			return false;
		}
		string strPinyin = str;
		if (strPinyin.empty())
			return isok;
		vector<string> res;
		vector<string> pyRes;
		vector<int> isFixedRes;
		int row = 0;

		*counts = 0;
		vector<coral::phonetics::CandidateResult> sentsCache;
		if (!coral::phonetics::SyllableParser::instance().parse_input(strPinyin, res, pyRes, isFixedRes, offset, size, asciiMode, kInvalidPhraseId, condition ? condition : ""))
			return isok;
		for (auto it = res.begin(); it != res.end(); ++it)
		{
			if (size != -1 && row >= size)
			{
				break;
			}
			if (!it->empty())
			{
				*counts += 1;
			}

			int len = std::min(static_cast<int>(it->size()), rowlen - 1);
			errno_t err = memcpy_s(output[row], rowlen, it->c_str(), len);
			if (err != 0)
			{
				return false;
			}
			output[row][len] = '\0';

			len = std::min(static_cast<int>(pyRes[row].size()), rowlen - 1);
			err = memcpy_s(pyOutput[row], rowlen, pyRes[row].c_str(), len);
			if (err != 0)
			{
				return false;
			}
			pyOutput[row][len] = '\0';

			row++;
		}
	}
	return true;
}

int read_pinyin_splits(const char* const str,
	char* pyOutput,
	int outputSize)
{
	if (!str || !pyOutput || outputSize <= 0)
	{
		return 0;
	}

	// 边界统一推送配置快照（覆盖跨进程设置变更；等价旧的 sConfig 读穿透）
	coral::Engine::compatInstance().refreshConfig();
	// 与 parsePinyinString 同理：声明在 context 之前，出作用域时回收本次用到的 arena 内存
	coral::phonetics::support::ParseArenaScope arenaScope;

	PinyinParseContext context;
	context.pinyin = str;
	coral::phonetics::SyllableParser::instance().get_pinyin_sequence(context);
	const coral::phonetics::ParseSequences& pinyinSequences = context.pinyinSequences;

	const int targetTo = context.syllableGraph.get_vertex_count() - 1;
	std::vector<std::string> splits;

	// 收集所有完整划分
	for (const auto& pinyinSequence : pinyinSequences)
	{
		if (pinyinSequence.from_ == 0 && pinyinSequence.to_ == targetTo)
		{
			// 格式化单个拼音序列（用单引号连接）
			std::string splitStr;
			if (!pinyinSequence.sequences_.empty())
			{
				splitStr = pinyinSequence.sequences_[0];
				for (size_t i = 1; i < pinyinSequence.sequences_.size(); ++i)
				{
					splitStr += "'";
					splitStr += pinyinSequence.sequences_[i];
				}
			}
			splits.push_back(splitStr);
		}
	}

	if (splits.empty())
	{
		pyOutput[0] = '\0';
		return 0;
	}

	// 用逗号连接所有划分结果
	std::string result;
	result.reserve(256);
	result = splits[0];
	for (size_t i = 1; i < splits.size(); ++i)
	{
		result += ",";
		result += splits[i];
	}

	// 确保不超出输出缓冲区大小
	const size_t maxCopySize = static_cast<size_t>(outputSize) - 1;
	if (result.length() > maxCopySize)
	{
		result.resize(maxCopySize);
	}

#if defined(_WIN32) || defined(_MSC_VER)
	strncpy_s(pyOutput, static_cast<size_t>(outputSize), result.c_str(), _TRUNCATE);
#else
	result.copy(pyOutput, result.size());
	pyOutput[result.size()] = '\0';
#endif

	return static_cast<int>(splits.size());
}

bool coral::runtime::compile_resource_files(const char *primaryInputPath, const char *primaryTriePath, const char *primaryInfoPath,
														  const char *bushouInputPath, const char *bushouTriePath, const char *bushouInfoPath,
														  const char *strokeInputPath, const char *strokeTriePath, const char *strokeInfoPath,
														  const char *englishInputPath, const char *englishTriePath, const char *englishInfoPath,
														  const char *modelUnigramFile,
														  const char *modelBigramFile, const char *langModelOutputDir, const char *cnPrimaryInputPath,
														  const char *cnPrimaryTriePath, const char *cnPrimaryInfoPath,
														  const char *originalPrimaryPath)
{
	// --- 步骤 1: 生成主词库 (Primary Lexicon) ---
	std::cout << "Step 1: Generating Primary Lexicon..." << std::endl;
	coral::phonetics::SystemLexicon primaryLexicon;
	if (!primaryLexicon.build_lexicon(primaryInputPath, primaryTriePath, primaryInfoPath))
	{
		std::cerr << "Error: Failed to generate Primary Lexicon." << std::endl;
		return false;
	}
	std::cout << "Primary Lexicon generated successfully." << std::endl;
	// 从同一份统一索引词表生成运行时拼音索引。
	// 第 8 步可能把中文 trie 写入旧的 marisa/dat 路径。
	if (!build_lexicon_table_from_csv(primaryInputPath,
		std::filesystem::path(primaryTriePath).parent_path() / "mainlexicon.coral", true))
		return false;

	// --- 步骤 2: 生成部首U模式词库 (Bushou U-Mode Lexicon) ---
	std::cout << "\nStep 2: Generating Bushou (Radical) U-Mode Lexicon..." << std::endl;
	coral::assist::ShapeLexicon bushouLexicon;
	if (!bushouLexicon.build_lexicon(bushouInputPath, bushouTriePath, bushouInfoPath))
	{
		std::cerr << "Error: Failed to generate Bushou U-Mode Lexicon." << std::endl;
		return false;
	}
	std::cout << "Bushou U-Mode Lexicon generated successfully." << std::endl;

	// --- 步骤 3: 生成笔画U模式词库 (Stroke U-Mode Lexicon) ---
	std::cout << "\nStep 3: Generating Stroke U-Mode Lexicon..." << std::endl;
	coral::assist::ShapeLexicon strokeLexicon;
	if (!strokeLexicon.build_lexicon(strokeInputPath, strokeTriePath, strokeInfoPath))
	{
		std::cerr << "Error: Failed to generate Stroke U-Mode Lexicon." << std::endl;
		return false;
	}
	std::cout << "Stroke U-Mode Lexicon generated successfully." << std::endl;

	// --- 步骤 4: 生成英文词典 (English Lexicon) ---
	std::cout << "\nStep 4: Generating English Lexicon..." << std::endl;
	coral::phonetics::LatinLexicon englishLexicon;
	if (!englishLexicon.build_lexicon(englishInputPath, englishTriePath, englishInfoPath))
	{
		std::cerr << "Error: Failed to generate English Lexicon." << std::endl;
		return false;
	}
	std::cout << "English Lexicon generated successfully." << std::endl;

	// --- 步骤 5: 生成云词库文件 (Cloud Lexicons) ---
	/*if (std::filesystem::exists(cloudInputDir) && std::filesystem::exists(cloudOutputDir))
	{
		std::cout << "\nStep 5: Generating Cloud Lexicons..." << std::endl;
		CCloudLexicon cloudLexicon(0, std::string(), "", 0); // 构造一个临时的 CloudLexicon 对象
		for (const auto &entry : std::filesystem::directory_iterator(cloudInputDir))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".csv")
			{
				std::cout << "Generating Cloud Lexicons for " << entry.path().filename() << "..." << std::endl;
				std::filesystem::path trieOut = cloudOutputDir / entry.path().stem();
				trieOut.replace_extension(".marisa");
				std::filesystem::path dataOut = cloudOutputDir / entry.path().stem();
				dataOut.replace_extension(".dat");
				if (!cloudLexicon.buildLexicon(entry, trieOut, dataOut))
				{
					std::cerr << "Error: Generate Cloud Lexicon Failed." << std::endl;
					return false;
				}
			}
		}
		std::cout << "Cloud Lexicons generated successfully." << std::endl;
	}*/

	// --- 步骤 6: 生成语言模型文件（仅Bigram，Unigram已合并到主词典） ---
	std::cout << "\nStep 6: Generating Language Model files (Bigram only)..." << std::endl;
	if (!std::filesystem::exists(modelBigramFile))
	{
		std::cerr << "Error: Bigram model source CSV not found." << std::endl;
		return false;
	}
	// 保存原始工作目录
	auto original_path = std::filesystem::current_path();
	try
	{
		std::filesystem::create_directories(langModelOutputDir);
		std::filesystem::current_path(langModelOutputDir);

		coral::phonetics::ContextScorer languageModel;
		// 一元概率从主词典源 CSV 并入 languagemodel.coral（与步骤 8 的 buildLexiconForCN 同源同口径）
		if (!languageModel.build_g_bin_only(modelBigramFile, cnPrimaryInputPath))
		{
			std::cerr << "Error: Failed to build bigram language model." << std::endl;
			// 切换回原始目录
			std::filesystem::current_path(original_path);
			return false;
		}
	}
	catch (const std::exception &e)
	{
		std::cerr << "An exception occurred during language model build: " << e.what() << std::endl;
		std::filesystem::current_path(original_path);
		return false;
	}
	std::filesystem::current_path(original_path);
	std::cout << "Bigram Language Model files generated successfully in " << langModelOutputDir << std::endl;

	// --- 步骤 8: 生成中文词典 ---
	std::cout << "\nStep 8: Generating Chinese Trie Lexicon  ..." << std::endl;
	if (!primaryLexicon.build_lexicon_for_cn(cnPrimaryInputPath, cnPrimaryTriePath, cnPrimaryInfoPath))
	{
		std::cerr << "Error: Failed to generate Chinese Trie Tree Lexicon." << std::endl;
		return false;
	}
	std::cout << "Chinese Trie Tree Lexicon generated successfully." << std::endl;

	// --- 全部完成 ---
	std::cout << "\nAll lexicons have been generated successfully." << std::endl;
	return true;
}

void coral::runtime::read_next_page_state(bool &existNextPage)
{
	coral::phonetics::SyllableParser::instance().is_exist_next_page(existNextPage);
}

hlp::ImportResult coral::runtime::import_user_file(const std::filesystem::path &file)
{
	return coral::phonetics::SyllableParser::instance().import_user_lexicon(file);
}

bool coral::runtime::export_user_file(const std::filesystem::path &file)
{
	return coral::phonetics::SyllableParser::instance().export_user_lexicon(file);
}

bool coral::runtime::prepare_user_import(std::vector<UserLexiconImportItem> &items, const UserLexiconImportOptions &options)
{
	return coral::phonetics::LexiconImporter::instance().prepare(items, options);
}

UserLexiconImportReport coral::runtime::commit_user_import(const std::vector<UserLexiconImportItem> &items)
{
	return coral::phonetics::LexiconImporter::instance().commit(items);
}

bool coral::runtime::revert_user_import()
{
	return coral::phonetics::LexiconImporter::instance().revert_last();
}

bool coral::runtime::revert_phrase_learning(const std::string &delPhrase)
{
	return coral::phonetics::SyllableParser::instance().revert_last_action_for_phrase(delPhrase);
}

void coral::runtime::set_runtime_traditional(bool mode)
{
	sConfig->set_runtime_traditional(mode);
	// 同步当前生效值（旧行为：setter 同时更新内存缓存，保持等价）
	coral::inputModeState().setTraditionalChinese(mode);
}

bool coral::runtime::runtime_traditional()
{
	return coral::inputModeState().isTraditionalChinese();
}

void coral::runtime::set_runtime_full_width(bool mode)
{
	sConfig->set_runtime_full_width(mode);
	coral::inputModeState().setFullWidth(mode);
}

bool coral::runtime::runtime_full_width()
{
	return coral::inputModeState().isFullWidth();
}

void coral::runtime::apply_runtime_settings(
	bool isUMode, bool isVMode, bool pinyin_correction,
	bool isFullWidth, bool isTraditionalChinese, bool isShowInfos,
	int limit_count, int combined_lexicon_version,
	const int* fuzzy_choices, int fuzzyChoiceCount, bool smart_prediction)
{
	auto config = std::make_shared<coral::EngineConfig>();
	config->smart_prediction = smart_prediction;
	config->isUMode = isUMode;
	config->isVMode = isVMode;
	config->pinyin_correction = pinyin_correction;
	if (fuzzy_choices && fuzzyChoiceCount > 0)
		config->fuzzy_choices.assign(fuzzy_choices, fuzzy_choices + fuzzyChoiceCount);
	config->limit_count = limit_count;
	config->isShowInfos = isShowInfos;
	config->combined_lexicon_version = combined_lexicon_version;
	coral::Engine::compatInstance().config().update(std::move(config));
	// 会话模式是临时的。持久默认值只由设置界面写入。
	coral::inputModeState().setFullWidth(isFullWidth);
	coral::inputModeState().setTraditionalChinese(isTraditionalChinese);
}

void coral::runtime::refresh_runtime_settings()
{
	coral::Engine::compatInstance().refreshConfig();
}

bool coral::runtime::apply_candidate_rule(
    const std::string& pinyin, const std::string& word,
    coral::CandidateAction action, coral::LanguageVariant variant)
{
    return coral::phonetics::SyllableParser::instance().process_candidate_action(pinyin, word, pinyin, action, variant);
}

bool coral::runtime::add_phrase_shortcut(const std::string &code, const std::string &phrase)
{
	return coral::phonetics::SyllableParser::instance().get_lexicon_manager().get_user_lexicon().add_phrase_shortcut(code, phrase);
}

bool coral::runtime::remove_phrase_shortcut(const std::string &code, const std::string &phrase)
{
	return coral::phonetics::SyllableParser::instance().get_lexicon_manager().get_user_lexicon().remove_phrase_shortcut(code, phrase);
}

bool coral::runtime::list_phrase_shortcuts(std::vector<CustomPhraseEntry> &phrases)
{
	auto &lexicon = coral::phonetics::SyllableParser::instance().get_lexicon_manager().get_user_lexicon();
	if (!lexicon.is_alive())
		return false;
	phrases = lexicon.list_phrase_shortcuts();
	return true;
}

bool writePinyinSequencesToFile(const coral::phonetics::ParseSequences &pinyinSequences, const std::filesystem::path &filepath)
{
	std::error_code ec;
	std::filesystem::create_directories(filepath.parent_path(), ec);
	if (ec)
		return false;

	std::ostringstream buffer;

	buffer << "[BASIC_INFO]\n";
	buffer << "sequence_count=" << pinyinSequences.size() << '\n';
	buffer << '\n';

	buffer << "[PINYIN_SEQUENCES]\n";
	for (size_t i = 0; i < pinyinSequences.size(); ++i)
	{
		const auto &seq = pinyinSequences[i];

		buffer << "sequence_" << i << ": from=" << seq.from_ << ", to=" << seq.to_ << ", nodes=" << seq.path_nodes_
			   << ", english=" << (seq.is_match_english_vocab_ ? 1 : 0)
			   << ", fuzzy=" << (seq.is_fuzzy_match_ ? 1 : 0) << '\n';

		auto writeSequenceList = [&buffer](const std::string &name, const coral::phonetics::ParseTextViews& sequences)
		{
			buffer << "  " << name << ": [";
			for (size_t j = 0; j < sequences.size(); ++j)
			{
				if (j > 0)
					buffer << ", ";
				buffer << "\"" << sequences[j] << "\"";
			}
			buffer << "]\n";
		};

		writeSequenceList("sequences", seq.sequences_);
		writeSequenceList("sub_sequences", seq.sub_sequences_);
		writeSequenceList("show_sequences", seq.show_sequences_);
		writeSequenceList("original_sequences", seq.original_sequences_);
		buffer << '\n';
	}

	std::ofstream file{filepath};
	if (!file.is_open())
		return false;

	file << buffer.str();
	return true;
}

bool writeSolutionsToFile(const std::vector<coral::phonetics::CandidateResult> &solutions, const std::filesystem::path &filepath)
{
	std::error_code ec;
	std::filesystem::create_directories(filepath.parent_path(), ec);
	if (ec)
		return false;

	std::ostringstream buffer;
	buffer << std::fixed << std::setprecision(7);

	buffer << "[BASIC_INFO]\n";
	buffer << "solution_count=" << solutions.size() << '\n';
	buffer << '\n';

	buffer << "[SOLUTIONS]\n";
	for (size_t i = 0; i < solutions.size(); ++i)
	{
		const auto &solution = solutions[i];

		buffer << "solution_" << i << ": sentence=\"" << solution.sentence << "\", wordSize=" << solution.word_size
			   << ", probability=" << solution.probability << ", middlewareAdd=" << static_cast<unsigned int>(solution.transform_flags) << '\n';

		buffer << "  pinyin: [";
		for (size_t j = 0; j < solution.pinyin.size(); ++j)
		{
			if (j > 0)
				buffer << ", ";
			buffer << "\"" << solution.pinyin[j] << "\"";
		}
		buffer << "]\n";

		buffer << '\n';
	}

	std::ofstream file{filepath};
	if (!file.is_open())
		return false;

	file << buffer.str();
	return true;
}

bool coral::runtime::write_parse_trace(const char *str, const char *saveDir)
{
	if (!str || !saveDir)
		return false;
	// 边界统一推送配置快照（覆盖跨进程设置变更；等价旧的 sConfig 读穿透）
	coral::Engine::compatInstance().refreshConfig();
	// 开始解析
	// 边界统一推送配置快照（覆盖跨进程设置变更；等价旧的 sConfig 读穿透）
	coral::Engine::compatInstance().refreshConfig();
	// 与 parsePinyinString 同理：声明在 context 之前，出作用域时回收本次用到的 arena 内存
	coral::phonetics::support::ParseArenaScope arenaScope;

	PinyinParseContext context;
	context.pinyin = str;
	context.englishPinyin = str;
	context.isDoublepinyin = false;
	// 导出工具要的是完整解析结果：放开直连候选的组装上限，否则 solutions.txt 会被首屏预算截断
	context.directCandidateBudgetOverride = kUnlimitedDirectCandidates;

	// 1. 获取拼音序列
	coral::phonetics::SyllableParser::instance().get_pinyin_sequence(context);
	coral::phonetics::ParseSequences pinyinSequences = context.pinyinSequences;

	// 2. 构建模糊音匹配
	// 与主解析流程同口径：图内模糊开启时不再走后置展开，保证导出的
	// fuzzy_pinyin_sequences.txt 反映的是当前生效路径（图内模糊时该文件与
	// pinyin_sequences.txt 内容一致，模糊序列带 m_isFuzzyMatch 标记）。
	if (!fuzzy_in_graph_enabled())
		coral::phonetics::SyllableParser::instance().apply_fuzzy_replace(context);

	// 3. 初始化惰性语言模型解码器
	coral::phonetics::SyllableParser::instance().init_language_decoder(context);

	// 4. 获取解析结果
	coral::phonetics::SyllableParser::instance().get_parsed_result(context);

	string saveFileDir = saveDir;
	string pinyinSequencesFilePath = saveFileDir + "/pinyin_sequences.txt";
	string fuzzyPinyinSequencesFilePath = saveFileDir + "/fuzzy_pinyin_sequences.txt";
	string syllableGraphFilePath = saveFileDir + "/syllable_graph.txt";
	string lexiconGraphFilePath = saveFileDir + "/lexicon_graph.txt";
	string solutionFilePath = saveFileDir + "/solutions.txt";

	writePinyinSequencesToFile(pinyinSequences, pinyinSequencesFilePath);
	context.syllableGraph.write_to_file(syllableGraphFilePath);						   // 验证：1. 获取拼音序列 = pinyinSequences + syllableGraph
	writePinyinSequencesToFile(context.pinyinSequences, fuzzyPinyinSequencesFilePath); // 验证：2. 构建模糊音匹配
	context.wordLattice.write_to_file(lexiconGraphFilePath);							   // 验证：3. 词典图构建
	writeSolutionsToFile(context.sents, solutionFilePath);							   // 验证：5. 获取解析结果

	return true;
}

bool coral::runtime::write_correction_trace(
	const char *const pystring,
	unsigned int limit)
{
	if (!pystring)
	{
		return false;
	}
	// 边界统一推送配置快照（覆盖跨进程设置变更；等价旧的 sConfig 读穿透）
	coral::Engine::compatInstance().refreshConfig();

	auto correctionResults = coral::phonetics::SyllableParser::instance().generate_correction_string(pystring, limit);
	if (correctionResults.empty())
	{
		return false;
	}

	return true;
}

int coral::runtime::read_correction_suggestions(const char *pinyin, int maxSuggestions, char *output, int outputSize)
{
	if (!pinyin || !output || outputSize <= 0 || maxSuggestions <= 0)
	{
		return 0;
	}
	// 边界统一推送配置快照后再判断纠错开关
	coral::Engine::compatInstance().refreshConfig();
	if (coral::Engine::compatInstance().config().pinyin_correction() == false)
	{
		// 旧行为：纠错关闭时为生成建议强制开启（持久化，不恢复）
		sConfig->setPinyinCorrection(true);
		coral::Engine::compatInstance().refreshConfig();
		coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();
	}

	std::string pinyinStr(pinyin);

	// 调用CPinyinModel的generateCorrectionString方法
	auto correctionResults = coral::phonetics::SyllableParser::instance().generate_correction_string(pinyinStr, maxSuggestions);

	if (correctionResults.empty())
	{
		output[0] = '\0';
		return 0;
	}

	// 格式化输出结果：每个建议格式为 "corrected_pinyin|score"，多个建议用逗号分隔
	std::string result;
	int count = 0;

	for (const auto &correction : correctionResults)
	{
		if (count >= maxSuggestions)
			break;

		if (!correction.result.empty())
		{
			if (!result.empty())
			{
				result += ",";
			}

			// 将拼音序列连接成字符串
			std::string correctedPinyin;
			for (size_t i = 0; i < correction.result.size(); ++i)
			{
				if (i > 0)
					correctedPinyin += "'";
				correctedPinyin += correction.result[i];
			}

			result += correctedPinyin + "|" + std::to_string(correction.score);
			count++;
		}
	}

	// 确保不超出输出缓冲区大小
	if (result.length() >= static_cast<size_t>(outputSize))
	{
		result.resize(static_cast<size_t>(outputSize) - 1);
	}

#if defined(_WIN32) || defined(_MSC_VER)
	strncpy_s(output, static_cast<size_t>(outputSize), result.c_str(), _TRUNCATE);
#else
	if (output && outputSize > 0)
	{
		size_t toCopy = result.size();
		if (toCopy >= static_cast<size_t>(outputSize))
		{
			toCopy = static_cast<size_t>(outputSize) - 1;
		}
		result.copy(output, toCopy);
		output[toCopy] = '\0';
	}
#endif

	return count;
}

bool coral::runtime::parse_uncorrected_rows(const char *const condition, const char *const str, char **output, char **pyOutput,
																int offset, int size, int rowlen, int *counts, int asciiMode)
{
	// 先对齐快照与持久值，再保存原始的纠错设置
	coral::Engine::compatInstance().refreshConfig();
	const bool originalCorrection = coral::Engine::compatInstance().config().pinyin_correction();

	// 禁用拼音纠错（持久值 + 快照同步推送）
	sConfig->setPinyinCorrection(false);
	coral::Engine::compatInstance().refreshConfig();

	// 清理缓存以确保设置生效
	coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();

	// 直接调用现有的 parse_candidate_rows 函数（其入口会再次刷新快照，读到的仍是禁用后的持久值）
	bool result = parse_candidate_rows(condition, str, output, pyOutput, offset, size, rowlen, counts, asciiMode);

	// 恢复原始设置
	sConfig->setPinyinCorrection(originalCorrection);
	coral::Engine::compatInstance().refreshConfig();

	// 再次清理缓存
	coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();

	return result;
}

// 注音器单例
static coral::phonetics::PronunciationLabeler& getPinyinAnnotator()
{
	static coral::phonetics::PronunciationLabeler instance;
	return instance;
}

int coral::runtime::load_pronunciation_index(const char* const lSortFile)
{
	if (!lSortFile) {
		LOG_ERROR("load_pronunciation_index: lSortFile is null");
		return 0;
	}
	
	bool result = getPinyinAnnotator().initialize(lSortFile);
	return result ? 1 : 0;
}

bool coral::runtime::read_pronunciation_rows(
	const char* const word,
	char* fullPinyinOutput,
	int fullPinyinOutputSize,
	bool annotateOnFullMatch)
{
	if (!word || !fullPinyinOutput) {
		LOG_ERROR("read_pronunciation_rows: invalid parameters");
		return false;
	}
	
	coral::phonetics::PronunciationLabeler& annotator = getPinyinAnnotator();
	if (!annotator.is_initialized()) {
		LOG_ERROR("read_pronunciation_rows: PinyinAnnotator not initialized");
		return false;
	}
	
	std::string wordStr(word);
	std::vector<coral::phonetics::PronunciationResult> results = annotator.annotate_word(wordStr, annotateOnFullMatch);
	
	// 需要至少一条结果
	if (results.empty()) {
		return false;
	}

	// 辅助函数：安全复制字符串到缓冲区
	auto copyStringToBuffer = [](const std::string& source, char* buffer, int bufferSize) {
		if (!buffer || bufferSize <= 0) {
			return;
		}
#if defined(_WIN32) || defined(_MSC_VER)
		strncpy_s(buffer, bufferSize, source.c_str(), _TRUNCATE);
#else
		size_t toCopy = source.size();
		if (toCopy >= static_cast<size_t>(bufferSize)) {
			toCopy = static_cast<size_t>(bufferSize - 1);
		}
		std::copy(source.begin(), source.begin() + toCopy, buffer);
		buffer[toCopy] = '\0';
#endif
	};
	
	// 将所有结果序列化到字符串中（用换行符分隔，每个结果用制表符分隔字段）
	// 格式：index\tpinyin\tfrequency\tword\tfullPinyin\tbackprob\n
	std::string allResultsStr;
	for (size_t i = 0; i < results.size(); ++i) {
		const coral::phonetics::PronunciationResult& result = results[i];
		if (i > 0) {
			allResultsStr += "\n";
		}
		allResultsStr += std::to_string(result.index) + "\t";
		allResultsStr += result.pinyin + "\t";
		allResultsStr += std::to_string(result.frequency) + "\t";
		allResultsStr += result.word + "\t";
		allResultsStr += result.fullPinyin + "\t";
		allResultsStr += std::to_string(result.backprob);
	}
	
	// 将序列化的结果复制到 fullPinyinOutput 缓冲区（所有结果信息都在这里）
	copyStringToBuffer(allResultsStr, fullPinyinOutput, fullPinyinOutputSize);
	
	return true;
}

bool coral::runtime::annotate_word_batch(
	const char* const inputCsvFile,
	const char* const outputCsvFile,
	const std::vector<std::string>* words,
	std::vector<coral::phonetics::PronunciationResult>* results,
	bool annotateOnFullMatch)
{
	coral::phonetics::PronunciationLabeler& annotator = getPinyinAnnotator();
	if (!annotator.is_initialized()) {
		LOG_ERROR("annotate_word_batch: PinyinAnnotator not initialized");
		return false;
	}
	
	// 判断使用哪种模式：如果提供了words和results，使用vector模式；否则使用文件模式
	if (words != nullptr && results != nullptr) {
		// Vector模式
		results->clear();
		results->reserve(words->size());
		
		for (const auto& word : *words) {
			if (word.empty()) {
				continue;
			}
			
			// 为词语注音
			std::vector<coral::phonetics::PronunciationResult> wordResults = annotator.annotate_word(word, annotateOnFullMatch);
			
			// 将结果添加到输出vector中
			results->insert(results->end(), wordResults.begin(), wordResults.end());
		}
		
		LOG_INFO("Batch annotation from vector completed. Processed " + std::to_string(words->size()) + " words, generated " + std::to_string(results->size()) + " results");
		return true;
	}
	
	// 文件模式
	if (!inputCsvFile || !outputCsvFile) {
		LOG_ERROR("annotate_word_batch: invalid parameters for file mode");
		return false;
	}
	
	std::ifstream inputFile(inputCsvFile, std::ios::binary);
	if (!inputFile.is_open()) {
		LOG_ERROR("annotate_word_batch: failed to open input file: " + std::string(inputCsvFile));
		return false;
	}
	
	std::ofstream outputFile(outputCsvFile, std::ios::binary);
	if (!outputFile.is_open()) {
		LOG_ERROR("annotate_word_batch: failed to open output file: " + std::string(outputCsvFile));
		return false;
	}
	
	// 写入CSV表头
	outputFile << "index,pinyin,frequency,word,fullPinyin,backprob\n";
	
	// 辅助函数：解析CSV行
	auto parseCsvLine = [](const std::string& line) -> std::vector<std::string> {
		std::vector<std::string> fields;
		std::string field;
		bool inQuotes = false;
		for (size_t i = 0; i < line.length(); ++i) {
			char c = line[i];
			if (c == '"') {
				inQuotes = !inQuotes;
			} else if (c == ',' && !inQuotes) {
				fields.push_back(field);
				field.clear();
			} else {
				field += c;
			}
		}
		fields.push_back(field);
		return fields;
	};
	
	// 读取输入CSV文件
	std::string line;
	int lineCount = 0;
	int wordColumnIndex = 0; // 默认使用第一列
	
	// 首先读取表头，确定"词语"列的索引
	if (std::getline(inputFile, line)) {
		if (!line.empty()) {
			// 检查BOM标记
			if (line.length() >= 3 &&
				static_cast<unsigned char>(line[0]) == 0xEF &&
				static_cast<unsigned char>(line[1]) == 0xBB &&
				static_cast<unsigned char>(line[2]) == 0xBF) {
				line = line.substr(3);
			}
			
			// 解析表头
			std::vector<std::string> headerFields = parseCsvLine(line);
			
			// 查找"词语"列的索引
			bool foundWordColumn = false;
			for (size_t i = 0; i < headerFields.size(); ++i) {
				std::string header = headerFields[i];
				// 去除引号和空格
				header.erase(std::remove(header.begin(), header.end(), '"'), header.end());
				header.erase(std::remove(header.begin(), header.end(), ' '), header.end());
				
				// 检查是否是"词语"列
				if (header.find("词语") != std::string::npos || 
					header.find("word") != std::string::npos ||
					header.find("Word") != std::string::npos) {
					wordColumnIndex = static_cast<int>(i);
					foundWordColumn = true;
					break;
				}
			}
			
			// 如果未找到"词语"列，记录错误日志
			if (!foundWordColumn) {
				LOG_ERROR("annotate_word_batch: word column not found in CSV header, will use first column as default");
			}
		}
	}
	
	// 处理数据行
	while (std::getline(inputFile, line)) {
		if (line.empty()) continue;
		
		// 解析CSV行
		std::vector<std::string> fields = parseCsvLine(line);
		
		// 只使用"词语"列的索引获取词语，忽略其他列（如"AI处理时间"）
		std::string word;
		if (wordColumnIndex >= 0 && wordColumnIndex < static_cast<int>(fields.size())) {
			word = fields[wordColumnIndex];
		} else if (!fields.empty()) {
			// 如果索引无效，回退到第一列
			word = fields[0];
		}
		
		if (word.empty()) continue;
		
		// 去除可能的引号和空格
		word.erase(std::remove(word.begin(), word.end(), '"'), word.end());
		word.erase(std::remove(word.begin(), word.end(), ' '), word.end());
		
		if (word.empty()) continue;
		
		// 为词语注音
		std::vector<coral::phonetics::PronunciationResult> wordResults = annotator.annotate_word(word, annotateOnFullMatch);
		
		for (const auto& result : wordResults) {
			outputFile << result.index << ","
				<< result.pinyin << ","
				<< result.frequency << ","
				<< result.word << ","
				<< result.fullPinyin << ","
				<< std::fixed << std::setprecision(6) << result.backprob << "\n";
		}
		
		lineCount++;
	}
	
	LOG_INFO("Batch annotation completed. Processed " + std::to_string(lineCount) + " words");
	return true;
}

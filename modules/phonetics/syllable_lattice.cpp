/*
* @author: huangzhishou
* @date: 2021-5-28 17:57:04
*/
#include "pch.h"
#include "syllable_lattice.h"
#include "lexicon_catalog.h"
#include "candidate_lattice.hpp"
#include "syllable_catalog.h"
#include "phonetic_types.h"

constexpr string_view PINYIN_SEPARATOR = "'";

int coral::phonetics::SyllablePathSearch::get_edge_weight(const coral::phonetics::SyllableEdge& edge) const
{
	return edge.data.weight;
}

bool coral::phonetics::SyllableLattice::make_graph(const string_view pinyinString
	, const syllable_strview_map& syllables
	, coral::phonetics::RankedPaths& shortestPaths
	, const coral::phonetics::FuzzySyllableViews& fuzzyMaps
	, const coral::phonetics::LexiconCatalog& lexicon)
{
	size_t pinyin_length = pinyinString.size();
	size_t vertexCount = std::count_if(pinyinString.begin(), pinyinString.end(), [](char c) {
		return c != '\'';
		}) + 1;
	if (1 == static_cast<int>(vertexCount)) {
		return false;
	}
	vertices_.resize(vertexCount);
	for(size_t i = 0; i < vertexCount; ++i)
		vertices_[i].id = (int)i;
	
	unordered_set<int> semicolonPositions;
	vector<int> positionSemicolonNum(pinyin_length + 1);
	for (size_t i = 1; i <= pinyin_length; ++i)
	{
		positionSemicolonNum[i] = positionSemicolonNum[i - 1];
		if (pinyinString[i - 1] == PINYIN_SEPARATOR[0])
		{
			positionSemicolonNum[i]++;
			semicolonPositions.insert(i);
		}
	}

	for (size_t i = 0; i < pinyin_length; ++i)
	{
		if (std::isupper(static_cast<unsigned char>(pinyinString[i])))
		{
			size_t j = i;
			while (j < pinyin_length && std::isupper(static_cast<unsigned char>(pinyinString[j]))) 
				++j;
			coral::phonetics::TextSlice upperSlice = pinyinString.substr(i, j - i);
			int fromIndex = (int)i - positionSemicolonNum[i];
			int toIndex = (int)(j)-positionSemicolonNum[j];
			int eIndex = add_edge(vertices_[fromIndex], vertices_[toIndex]);
			if (eIndex >= 0 && eIndex < static_cast<int>(edges_.size()))
			{
				edges_[eIndex].data.phrase = upperSlice;
				edges_[eIndex].data.weight = kEnglishCost; 
			}
			i = j - 1;
			continue;
		}
		bool foundValidSyllable = false;
		for (size_t j = 1; j <= kPinyinMaxLen && (i + j) <= pinyin_length; ++j)
		{
			coral::phonetics::TextSlice pyslice = pinyinString.substr(i, j);
			auto iter = syllables.find(pyslice);
			if (iter != syllables.end())
			{
				foundValidSyllable = true;
				auto w = iter->second;
				int fromIndex = (int)i - positionSemicolonNum[i];
				int toIndex = (int)(i + j) - positionSemicolonNum[i + j];
				int eIndex = add_edge(vertices_[fromIndex], vertices_[toIndex]);
				if (eIndex < 0 || eIndex >= static_cast<int>(edges_.size()))
				{
					assert(false);
					return false;
				}
				// 为了把全拼路径权重提上来，提升程序性能
				int weight = coral::phonetics::syntax::is_jian_pin(pyslice) ? kJianCost : w;
					/*
					考虑到“中国社会XXXXX”这样的常见输入的简拼同时包含了zh和sh，但是我们的最短路径查找是向后查找所以会优先保留sh的组合而把zh切分
					所以这里让声母是zh的拼音路径维持简拼的cost，副作用就是会导致sh有时候会无法分开，这种用例比较少所以暂时可以接受
					*/
				if (pyslice != "zh" && coral::phonetics::syntax::is_sheng_mu(pyslice)) 
				{
					weight = kShenmuCost;
				}
				edges_[eIndex].data.phrase = pyslice;
				edges_[eIndex].data.weight = weight;
#ifdef _DEBUG
				vertices_[toIndex].data.word = pyslice;  // 存储当前音节，用于启发式函数计算音节合理性
#endif
				// 方案B第一阶段：为这条合法切分补挂模糊变体平行边。
				// 模糊变体与原切分共享同一对顶点（同一输入区间），只替换查询音节，
				// 因此 span 的 from/to 语义与 wordLattice 布局完全不变。
				if (fuzzyMaps.enabled())
					add_fuzzy_variant_edges(pyslice, fromIndex, toIndex, fuzzyMaps, syllables);
			}
		}
		if (!foundValidSyllable)
		{
			if (pinyinString[i] == PINYIN_SEPARATOR[0]) {
				continue;
			}
			bool canCombine = false;
			if (i > 0) {
				for (size_t backLen = 1; backLen <= 2 && i > backLen; ++backLen) {
					string_view combinedSyllable = pinyinString.substr(i - backLen, backLen + 1);
					auto iter = syllables.find(combinedSyllable);
					if (iter != syllables.end()) {
						canCombine = true;
						break;
					}
				}
			}
			if (!canCombine)
			{
				// 一定是 U V I 之一，直接作为单个字符加入图中，权重设为一个极大值，确保不会被优先选中
				int fromIndex = (int)i - positionSemicolonNum[i];
				int toIndex = (int)(i + 1) - positionSemicolonNum[i + 1];
				int eIndex = add_edge(vertices_[fromIndex], vertices_[toIndex]);
				if (eIndex < 0 || eIndex >= static_cast<int>(edges_.size()))
				{
					assert(false);
					return false;
				}
				string_view originalChar = pinyinString.substr(i, 1);
				edges_[eIndex].data.phrase = originalChar;
				// 给原字符比合法音节低的权重
				edges_[eIndex].data.weight = kInvalidCost;
#ifdef _DEBUG
				vertices_[toIndex].data.word = originalChar;
#endif
			}
		}
	}
	// 方案B第二阶段：编辑距离纠错边。闸门 = 输入不存在"干净解析"：
	// 裸声母边只允许出现在句首（简拼头，如 mfa 的 m）或句尾（打字进行中，
	// 如 jiaos 的 s），句中裸声母（afwo 的 f）与无效字符（uvi）才是确定性
	// 错误信号。这样 mfa/jiaos/wji 等简拼输入、fawo/nihao 等正确输入完全
	// 不生成编辑边（零行为差异、零开销），只有 afwo/shenfme 这类才纠错。
	// 含分隔符/大写的输入不生成（旧纠错同样只处理纯字母串）。
	if (edit_in_graph_enabled()
		&& pinyinString.size() >= 2
		&& pinyinString.find(PINYIN_SEPARATOR) == string_view::npos
		&& std::all_of(pinyinString.begin(), pinyinString.end(),
			[](unsigned char c) { return std::islower(c); })
		&& !has_clean_full_pinyin_parse(static_cast<int>(pinyinString.size()) + 1))
	{
		add_edit_edges(pinyinString, syllables);
	}

	// 纠错后的结果已经分好音节，直接加进拼音图中即可
	return shrink_graph_by_yen_ksp(kMaxTopk, kMaxPathDist, shortestPaths, pinyinString, &lexicon);
}

// ===== 方案B第一阶段：模糊音平行边 =====
// 对一条已确认的合法音节切分（originalSlice，位于 [fromIndex, toIndex) 输入区间），
// 枚举其模糊音变体（声母/韵母/整音节替换，复用旧 fuzzyReplace 的
// hlp::getFuzzyAlternatives，保证候选集合与旧路径完全一致），为每个
// 仍是合法音节的变体挂一条平行边：
//   - phrase          = 模糊替换后的查询音节（视图指向 CPinyinModel 模糊表，跨解析稳定）
//   - originalPhrase  = 原始输入切分（视图指向本次输入串）
//   - weight          = 与原边同口径的基础权重 + FUZZY_EDGE_PENALTY
// 旧 fuzzyReplace 的行为差异（有意为之，详见设计文档）：
//   1. 旧路径对每条 ≤5 音节的序列无条件展开全部变体并逐个查词库；新路径由 Yen KSP
//      按路径代价筛选，只有整体代价仍进入 top-k 的模糊切分才会参与查询——
//      这是"解码统一排序"的核心，也是消掉重复查询的主要来源。
//   2. 旧路径的"英语序列不模糊 / 单音节 span 不模糊 / >5 音节不模糊"限制，
//      在图内分别由 ENGLISH_COST、代价竞争、路径长度天然覆盖；后续 A/B 如发现
//      行为回退再针对性补闸门。
bool coral::phonetics::SyllableLattice::add_fuzzy_variant_edges(
	const string_view originalSlice,
	int fromIndex,
	int toIndex,
	const coral::phonetics::FuzzySyllableViews& fuzzyMaps,
	const syllable_strview_map& syllables)
{
	coral::phonetics::ParseTextViews alternatives;
	coral::phonetics::syntax::get_fuzzy_alternatives(originalSlice, alternatives,
		*fuzzyMaps.initials, *fuzzyMaps.finals, *fuzzyMaps.alls, syllables);

	bool added = false;
	for (const auto& alt : alternatives)
	{
		if (alt == originalSlice)
			continue;
		// 同一 (from,to) 的变体去重：getFuzzyAlternatives 理论上不产重复，
		// 这里防御性地扫一遍出边（顶点出边数是个位数，开销可忽略）
		bool dup = false;
		for (const coral::phonetics::LatticeIndex ei : vertices_[fromIndex].outgoing_edges)
		{
			if (edges_[ei].to_vertex_index == toIndex && edges_[ei].data.phrase == alt)
			{
				dup = true;
				break;
			}
		}
		if (dup)
			continue;

		const int eIndex = add_edge(vertices_[fromIndex], vertices_[toIndex]);
		edges_[eIndex].data.phrase = alt;
		edges_[eIndex].data.original_phrase = originalSlice;
		edges_[eIndex].data.is_fuzzy_edge = true;
		// 与正常边同口径：简拼/声母变体维持对应 cost，再叠加模糊惩罚
		int weight = coral::phonetics::syntax::is_jian_pin(alt) ? kJianCost : kFullCost;
		if (alt != "zh" && coral::phonetics::syntax::is_sheng_mu(alt))
			weight = kShenmuCost;
		edges_[eIndex].data.weight = weight + kFuzzyEdgePenalty;
		PERF_INC(fuzzyEdgesAdded);
		added = true;
	}
	return added;
}

// 方案B第二阶段：判定输入是否存在"干净解析"——全拼音节边组成、裸声母边
// 仅出现在句首（简拼头）或句尾（打字进行中）、无无效字符（uvi）边。
// 调用前提：图已按无分隔符的纯小写输入建好，顶点下标 == 字符位置。
// DP：clean[v] = 从 0 到 v 存在路径，且路径上没有句中裸声母/无效边。
// 句尾声母边不传递 clean 状态，但可由 clean[u] 直接完成整条路径。
bool coral::phonetics::SyllableLattice::has_clean_full_pinyin_parse(int vertexCount) const
{
	if (vertexCount <= 0 || static_cast<int>(vertices_.size()) < vertexCount)
		return false;
	std::vector<char> clean(vertexCount, 0);
	clean[0] = 1;
	bool cleanEnd = false;
	// backTail[v]：从顶点 v 到串尾的每个字符都能作为裸声母（"尾部简拼块"）。
	// 混拼输入的标准形态是 全拼音节 + 尾部简拼块（anpaiyx = an'pai'y'x），
	// 块内声母一律合法；句中孤立的裸声母（afwo 的 f）仍视为确定性错误。
	std::vector<char> backTail(vertexCount, 0);
	backTail[vertexCount - 1] = 1;
	for (int v = vertexCount - 2; v >= 0; --v)
	{
		bool shengmuEdge = false;
		for (const coral::phonetics::LatticeIndex ei : vertices_[v].outgoing_edges)
		{
			const auto& e = edges_[ei];
			if (e.to_vertex_index == v + 1 && coral::phonetics::syntax::is_sheng_mu(e.data.phrase)
				&& e.data.weight < kInvalidCost)
			{
				shengmuEdge = true;
				break;
			}
		}
		backTail[v] = static_cast<char>(shengmuEdge && backTail[v + 1]);
	}
	for (int v = 0; v < vertexCount; ++v)
	{
		if (!clean[v])
			continue;
		for (const coral::phonetics::LatticeIndex ei : vertices_[v].outgoing_edges)
		{
			const auto& e = edges_[ei];
			if (e.to_vertex_index < 0 || e.to_vertex_index >= vertexCount)
				continue;
			const bool isBareInitial = coral::phonetics::syntax::is_sheng_mu(e.data.phrase);
			const bool isInvalid = e.data.weight >= kInvalidCost;
			if (isBareInitial || isInvalid)
			{
				// 句尾裸声母可以收尾（jiaos 的 s）；句首裸声母允许传递（mfa 的 m）；
				// 尾部简拼块内的声母一律合法（anpaiyx 的 y、x）
				if (isBareInitial && !isInvalid && e.to_vertex_index == vertexCount - 1)
					cleanEnd = true;
				else if (isBareInitial && !isInvalid && (v == 0 || backTail[v]))
					clean[e.to_vertex_index] = 1;
				continue;
			}
			clean[e.to_vertex_index] = 1;
		}
	}
	return cleanEnd || clean[vertexCount - 1];
}

// ===== 方案B第二阶段：编辑距离纠错边 =====
// 窗口 [i, i+L) 上枚举单次编辑，结果为合法音节则挂平行边：
//   - swap：交换相邻两字符（af -> fa，afwo -> fa'wo 的关键路径）
//   - delete：删一个字符（af -> a，afwo -> a'wo）
//   - insert：插一个字符（窗口消耗 L 个原始字符，查询音节长 L+1）
//   - replace：换一个字符
// 每条编辑边带 EDIT_EDGE_PENALTY 图代价，由 Yen KSP 统一排序：编辑路径只有在
// 整体代价仍优于（或接近）原始切分时才会进入 top-k 路径参与查词，替代旧
// generateCorrectionString 的"全量枚举候选 + 字母trigram打分 + 门控裁决"三段式。
// 单路径多编辑的误纠由代价惩罚天然抑制（两条编辑边 +200，几乎不可能进 top-k），
// 硬约束（每路径至多一条编辑边）留待后续按需加。
int coral::phonetics::SyllableLattice::add_edit_edges(const string_view pinyinString, const syllable_strview_map& syllables)
{
	const size_t n = pinyinString.size();
	std::string candidate;
	int added = 0;

	// 逐编辑类型过一遍窗口，candidate 复用减少分配
	auto tryAddEdge = [&](size_t from, size_t spanLen, const std::string& cand,
		int kind, int editPos, const string_view window)
	{
		if (cand == window)
			return;
		const auto it = syllables.find(cand);
		if (it == syllables.end())
			return;
		const string_view phrase = it->first;
		// 与已有平行边去重（正常边 + 模糊边 + 编辑边共用同一出边表，顶点出边数很小）
		for (const coral::phonetics::LatticeIndex ei : vertices_[from].outgoing_edges)
		{
			if (edges_[ei].to_vertex_index == static_cast<coral::phonetics::LatticeIndex>(from + spanLen)
				&& edges_[ei].data.phrase == phrase)
				return;
		}
		const int eIndex = add_edge(vertices_[from], vertices_[from + spanLen]);
		int weight = coral::phonetics::syntax::is_jian_pin(phrase) ? kJianCost : kFullCost;
		if (phrase != "zh" && coral::phonetics::syntax::is_sheng_mu(phrase))
			weight = kShenmuCost;
		edges_[eIndex].data.phrase = phrase;
		edges_[eIndex].data.original_phrase = window;
		edges_[eIndex].data.is_edit_edge = true;
		edges_[eIndex].data.edit_kind = kind;
		edges_[eIndex].data.edit_index = editPos;
		edges_[eIndex].data.weight = weight + kEditEdgePenalty;
		++added;
	};

	for (size_t i = 0; i < n; ++i)
	{
		for (size_t L = 1; L <= kPinyinMaxLen + 1 && i + L <= n; ++L)
		{
			const string_view window = pinyinString.substr(i, L);

			// 混拼保护2：窗口内全部是声母字符（bb/bh/bj 这类简拼缩写）时不产生任何
			// 编辑边——用户打声母缩写是刻意行为，把 "bb" 改写成 "ba"（把你）几乎总是
			// 误纠。afwo 的关键窗口 "af" 含元音，不受影响。
			bool allShengMu = true;
			for (size_t q = 0; q < L; ++q)
			{
				if (!coral::phonetics::syntax::is_sheng_mu(window.substr(q, 1)))
				{
					allShengMu = false;
					break;
				}
			}
			if (allShengMu)
				continue;

			// swap 相邻两字符（结果长度 = L）
			if (L >= 2)
			{
				for (size_t p = 0; p + 1 < L; ++p)
				{
					candidate.assign(window);
					std::swap(candidate[p], candidate[p + 1]);
					tryAddEdge(i, L, candidate, static_cast<int>(coral::phonetics::syntax::CorrectionType::SWAP),
						static_cast<int>(i + p), window);
				}
			}
			// delete 一个字符（结果长度 = L-1）
			// 混拼保护：被删字符本身是合法声母时不产生删除边。
			// 输入串中间的孤立声母（aixbux 的 x、babde 的 b）是用户刻意的简拼缩写，
			// "删掉声母变成全拼"（aixbux→aibu）几乎总是误纠；afwo 类错误靠
			// swap/replace 边纠错，不依赖删除声母。
			if (L >= 2)
			{
				for (size_t p = 0; p < L; ++p)
				{
					if (coral::phonetics::syntax::is_sheng_mu(window.substr(p, 1)))
						continue;
					candidate.assign(window).erase(p, 1);
					tryAddEdge(i, L, candidate, static_cast<int>(coral::phonetics::syntax::CorrectionType::DELETION),
						static_cast<int>(i + p), window);
				}
			}
			// replace 一个字符（结果长度 = L）
			for (size_t p = 0; p < L; ++p)
			{
				for (char c = 'a'; c <= 'z'; ++c)
				{
					if (window[p] == c)
						continue;
					candidate.assign(window);
					candidate[p] = c;
					tryAddEdge(i, L, candidate, static_cast<int>(coral::phonetics::syntax::CorrectionType::REPLACEMENT),
						static_cast<int>(i + p), window);
				}
			}
			// insert 一个字符（结果长度 = L+1，窗口仍消耗 L 个原始字符）
			if (L <= kPinyinMaxLen)
			{
				for (size_t p = 0; p <= L; ++p)
				{
					for (char c = 'a'; c <= 'z'; ++c)
					{
						candidate.assign(window);
						candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(p), c);
						tryAddEdge(i, L, candidate, static_cast<int>(coral::phonetics::syntax::CorrectionType::INSERTION),
							static_cast<int>(i + p), window);
					}
				}
			}
		}
	}
	PERF_ADD(editEdgesAdded, added);
	return added;
}

//双拼输入的时候已经使用'切分 无需再进行拼音切分
bool coral::phonetics::SyllableLattice::make_doublepinyin_graph(const string_view pinyinString
	, const syllable_strview_map& syllables
	, coral::phonetics::RankedPaths& shortestPaths)
{
	vector<string_view> syllableList;
	size_t start = 0;
	size_t pos = 0;
	while ((pos = pinyinString.find(PINYIN_SEPARATOR, start)) != string::npos)
	{
		if (pos > start)
			syllableList.push_back(pinyinString.substr(start, pos - start));
		start = pos + 1;
	}
	if (start < pinyinString.length())
		syllableList.push_back(pinyinString.substr(start));
	if (syllableList.empty() && !pinyinString.empty()) {
		//不应该出现这种情况
		syllableList.push_back(pinyinString);
	}

	size_t vertexCount = syllableList.size() + 1;
	vertices_.resize(vertexCount);
	for (size_t i = 0; i < vertexCount; ++i)
		vertices_[i].id = static_cast<int>(i);

	for (size_t i = 0; i < syllableList.size(); ++i)
	{
		const string_view syllable = syllableList[i];
		auto iter = syllables.find(syllable);
		if (iter != syllables.end())
		{
			int eIndex = add_edge(vertices_[i], vertices_[i + 1]);
			edges_[eIndex].data.phrase = syllable;
			vertices_[i + 1].data.compact_id = eIndex + 1;
#ifdef _DEBUG
			vertices_[i + 1].data.word = syllable;
#endif
		}
		else
		{
			// 如果音节不在词典中，可能需要特殊处理
		}
	}
	coral::phonetics::SyllablePathSearch yenKSP;
	yenKSP.calculate_yen_ksp(*this, kMaxTopk, kMaxPathDist, shortestPaths);
	return true;
}

string coral::phonetics::SyllableLattice::format_pinyin(const string& pinyinString,
	const syllable_strview_map& syllables,
	unordered_set<string_view>& validSyllables, int& maxPinyinLen)
{
	string pinyin = pinyinString;
	size_t pinyin_length = pinyin.size();
	for (size_t i = 0; i < pinyin_length; ++i)
	{
		for (size_t j = 1; j <= kPinyinMaxLen && (i + j) <= pinyin_length; ++j)
		{
			string pyslice = pinyin.substr(i, j);
			if (syllables.find(pyslice) != syllables.end())
			{
				validSyllables.insert(pyslice);
				if ((int)pyslice.size() > maxPinyinLen)
					maxPinyinLen = (int)pyslice.size();
			}
		}
	}
	hlp::replaceString(pinyin, PINYIN_SEPARATOR, "");
	return pinyin;
}

void coral::phonetics::SyllableLattice::add_additional_path(coral::phonetics::RankedPaths& shortestPaths) const
{
	auto startPath = shortestPaths[0].path_[0];
	int eIndex = 0;
	for (const auto& edge : edges_)
	{
		if (edge.from_vertex_index == 0 && edge.data.weight % kFullCost == 0)
		{
			if (std::any_of(shortestPaths.begin(), shortestPaths.end(),
				[eIndex](const coral::phonetics::RankedPath& v) {
					if (v.path_.size() > 1) {
						return v.path_[1].second == eIndex;
					}
					else {
						return false;
					}
				})) {
				eIndex++;
				continue;
			}
			coral::phonetics::LatticePath innerPath;
			innerPath.emplace_back(startPath);
			innerPath.emplace_back(edge.to_vertex_index, eIndex);
			coral::phonetics::ParseIntegers innerCostPath;
			innerCostPath.emplace_back(edge.data.weight);
#ifdef _DEBUG
			shortestPaths.emplace_back(std::move(innerPath), std::move(innerCostPath), edges_, edge.data.weight);
#else
			shortestPaths.emplace_back(std::move(innerPath), std::move(innerCostPath), edge.data.weight);
#endif

		}
		else if (edge.from_vertex_index != 0)
		{
			break;
		}
		eIndex++;
	}
}

// 序列里是否一个完整音节都没有：每个音节都是声母或简拼前缀（如 j's'b'g、zho'g）。
// 这类序列视为简拼查询，不参与全句命中剪枝。
static bool isAbbrevOnlySpan(const coral::phonetics::ParseTextViews& syllables)
{
	for (const auto& syllable : syllables)
	{
		if (!coral::phonetics::syntax::is_sheng_mu(syllable) && !coral::phonetics::syntax::is_jian_pin(syllable))
			return false;
	}
	return true;
}

bool coral::phonetics::SyllableLattice::get_pinyin_sequences(
	const string& pinyinString,
	const syllable_strview_map& syllables,
	int maxLength,
	coral::phonetics::ParseSequences& pinyinSequences,
	bool& isFullPinyinMatch,
	bool isDoublepinyinMode,
	coral::phonetics::CandidateLattice& wordLattice,
	coral::phonetics::LexiconCatalog& lexicon,
	bool& sentHitDict,
	const std::unordered_set<std::string_view>& fuzzyOnlySyllables,
	const coral::phonetics::FuzzySyllableViews& fuzzyMaps,
	const bool shouldLimit,
    const bool allow_full_hit_pruning)
{
	coral::phonetics::RankedPaths shortestPaths;
	if (!isDoublepinyinMode)
	{
		if (!make_graph(string_view(pinyinString), syllables, shortestPaths, fuzzyMaps, lexicon))
			return false;
	}
	else
	{
		if (!make_doublepinyin_graph(string_view(pinyinString), syllables, shortestPaths))
			return false;
	}

	if (shortestPaths.empty())
		return false;
	add_additional_path(shortestPaths);
	isFullPinyinMatch = false;
	if (!shortestPaths.empty())
		isFullPinyinMatch = shortestPaths.begin()->is_full_path_;
	const int vertexCount = get_vertex_count();
	wordLattice.init_graph(vertexCount, pinyinString, lexicon);

	pySeqSearchSet pySeqSearchMap;
	pySeqSearchMap.reserve(vertices_.size() * vertices_.size());	// pinyinSeq的数量和顶点数的平方基本成正比

	// ===== 全句命中剪枝 =====
	// 大部分输入是全拼或开头为全拼的混拼（如 jinshanbangong / jinshanbang）。首条路径的
	// 全串查询（i=0、j=末顶点）命中词典完整词条（如 jinshanbangong → 珊瑚办公）后，
	// 语言模型中不存在两个词的二元组合权重能超过该完整词条，因此后续不从 0 开始的
	// span 查询（shanbangong / shanban / bangong …）对首屏排序没有贡献，直接跳过，
	// 只保留从 0 开始的前缀查询（jinshanban / jinshan / jin …，含其他路径切分的 ji 等）。
	// 全简拼（jsbg 这类每个音节都是声母/简拼的序列）不触发：这类查询本来就有严格的
	// limit 限制，剪枝收益小且简拼候选更依赖组合路径。可用 CORAL_SPAN_PRUNE=0 关闭，
	// 便于 A/B 对比正确率与性能。
	static const bool s_enableFullHitPrune = [] {
		const char* env = std::getenv("CORAL_SPAN_PRUNE");
		return !(env && env[0] == '0');
	}();
	bool pruneInnerSpans = false;

	// 每条最短路径的拼音串只拼一次，下面双重循环里任意区间的 key 都取它的子串。
	// 用 deque 而不是 vector：追加新路径不会让 pySeqSearchMap 里已存的 string_view 失效
	std::deque<std::string, coral::phonetics::support::PoolAllocator<std::string>> joinedPaths;

	bool hasFullpath = false;
	bool isAbbreviatehasFull = false;
	for (auto pathIter = shortestPaths.begin();
		pathIter != shortestPaths.end(); ++pathIter) 
	{
		//这里全拼只能是第一个，不然就是bug
		if (pathIter == shortestPaths.begin() && pathIter->is_almost_full_path_)
		{
			hasFullpath = true;
		}

		if (hasFullpath && pathIter != shortestPaths.begin() && !pathIter->check_is_a_full_path())
		{
			isAbbreviatehasFull = true;
		}

		coral::phonetics::ParseTextViews pinyin_sequence;
		// 方案B第一阶段：模糊边路径需要同时知道"查询音节串"与"原始输入切分串"，
		// 以及 span 内是否含模糊边（前缀计数，同 isShengMuPinyin 的用法）。
		// 第二阶段：编辑边同理（isEditPinyin）。
		coral::phonetics::ParseTextViews originalPinyinSequence;
		std::vector<int> isShengMuPinyin;
		std::vector<int> isFullPinyin;
		std::vector<int> isFuzzyPinyin;
		std::vector<int> isEditPinyin;

		int shengMuCount = 0;
		int fullPinyinCount = 0;
		int fuzzyEdgeCount = 0;
		int editEdgeCount = 0;
		std::vector<int> englishCounts{0};
		int englishCount = 0;
		isShengMuPinyin.push_back(shengMuCount);
		isFullPinyin.push_back(fullPinyinCount);
		isFuzzyPinyin.push_back(fuzzyEdgeCount);
		isEditPinyin.push_back(editEdgeCount);
		for (int i = 1; i < static_cast<int>(pathIter->path_.size()); i++)
		{
			auto [vIndex, eIndex] = pathIter->path_[i];
			if (!(eIndex >= 0 && eIndex < static_cast<int>(edges_.size()))) {
				LOG_ERROR("edge index out of range!");
				return false;
			}
			if (!(vIndex >= 0 && vIndex < static_cast<int>(vertices_.size()))) {
				LOG_ERROR("vertex index out of range!");
				return false;
			}

			const auto& edgeData = edges_[eIndex].data;
			if (edgeData.is_english_edge) ++englishCount;
			englishCounts.push_back(englishCount);
			pinyin_sequence.push_back(edgeData.phrase);
			originalPinyinSequence.push_back(
				(edgeData.is_fuzzy_edge || edgeData.is_edit_edge) ? edgeData.original_phrase : edgeData.phrase);
			if (edgeData.is_fuzzy_edge)
				fuzzyEdgeCount++;
			if (edgeData.is_edit_edge)
				editEdgeCount++;

			if (coral::phonetics::syntax::is_sheng_mu(edgeData.phrase))
				shengMuCount++;
			else if (!coral::phonetics::syntax::is_jian_pin(edgeData.phrase))
				fullPinyinCount++;
			isShengMuPinyin.push_back(shengMuCount);
			isFullPinyin.push_back(fullPinyinCount);
			isFuzzyPinyin.push_back(fuzzyEdgeCount);
			isEditPinyin.push_back(editEdgeCount);
		}

		// syllableStart[k] 是第 k 个音节在 joined 里的起始下标，末项按"总长 + 1 个分隔符"补齐，
		// 使区间 [i, j) 的长度统一为 syllableStart[j] - syllableStart[i] - 1
		const std::string& joined = joinedPaths.emplace_back(coral::phonetics::syntax::join_string_views('\'', pinyin_sequence));
		std::vector<size_t, coral::phonetics::support::PoolAllocator<size_t>> syllableStart;
		syllableStart.reserve(pinyin_sequence.size() + 1);
		size_t offset = 0;
		for (const auto& syllable : pinyin_sequence)
		{
			syllableStart.push_back(offset);
			offset += syllable.size() + 1;
		}
		syllableStart.push_back(offset);

		for (int i = 0; i < static_cast<int>(pathIter->path_.size()); i++) 
		{
			const int from = pathIter->path_.at(i).first;
			size_t pathSize = pathIter->path_.size();

			coral::phonetics::ParseTextViews pinyinSequenceCopy(pinyin_sequence.begin() + i, pinyin_sequence.end());

			for (int j = static_cast<int>(pathSize) - 1; j > i; j--)
			{
				auto [vIndex, eIndex] = pathIter->path_[j];
				const auto& vertexData = vertices_[vIndex].data;

				if (j - i <= maxLength)
				{
					// pinyinSequenceCopy 此刻正是 pinyinSequence 的 [i, j) 区间
					const std::string_view pyseq(joined.data() + syllableStart[i],
						syllableStart[j] - syllableStart[i] - 1);
					const int spanEnglishCount = englishCounts[j] - englishCounts[i];
					if (spanEnglishCount != 0 && (j - i != 1 || spanEnglishCount != 1))
					{
						// 英文是一个不透明词弧，绝不是中文键中的拼音音节。
					}
					else if (pruneInnerSpans && from != 0 && englishCount == 0)
					{
						// 全串已命中词典完整词条，这个不从 0 开始的区间查询直接剪掉。
						// 注意不能在这里 continue：循环尾还要 pop_back 维护 pinyinSequenceCopy。
						PERF_INC(spanQueriesPruned);
					}
					else if (pySeqSearchMap.insert(pySeqSearchInfo{ pyseq, from, vertexData.compact_id, spanEnglishCount != 0 }).second)
					{
						coral::phonetics::ParseTextViews subPinyinSequence;
						bool is_mix_pinyin = spanEnglishCount == 0 && (isShengMuPinyin[j] - isShengMuPinyin[i] != j - i) && (isFullPinyin[j] - isFullPinyin[i] != j - i);
						if (is_mix_pinyin)
							get_sub_pinyin_sequences(pinyinSequenceCopy, subPinyinSequence, 0);
						bool hasSubPinyin = !subPinyinSequence.empty();
						// 方案B第一阶段：含模糊边的 span 标记 m_isFuzzyMatch，并把
						// 原始输入切分存入 original/show 序列（与旧 fuzzyReplace 产物同构，
						// 打分与 UI 层无需感知来源差异）。沿用旧路径的两个闸门：
						// 非句首单音节 span 不模糊、超过 5 个音节的 span 不模糊。
						const bool spanIsFuzzy = isFuzzyPinyin[j] > isFuzzyPinyin[i]
							&& !(from != 0 && pinyinSequenceCopy.size() == 1)
							&& pinyinSequenceCopy.size() <= 5;
						// 方案B第二阶段：含编辑边的 span。无长度上限（纠错本来就要
						// 覆盖长输入），沿用"非句首单音节不生成"的降噪闸门
						const bool spanIsEdit = isEditPinyin[j] > isEditPinyin[i]
							&& !(from != 0 && pinyinSequenceCopy.size() == 1);
						if (spanIsFuzzy || spanIsEdit)
						{
							coral::phonetics::ParseTextViews originalSequenceCopy(originalPinyinSequence.begin() + i,
								originalPinyinSequence.begin() + i + pinyinSequenceCopy.size());
							coral::phonetics::PhoneticSequence pySequence(
								pinyinSequenceCopy,
								subPinyinSequence,
								originalSequenceCopy,
								originalSequenceCopy,
								from,
								vertexData.compact_id,
								(int)pathSize,
								false,
								spanIsFuzzy,
								isAbbreviatehasFull && hasSubPinyin);
							if (spanIsEdit)
								pySequence.is_edit_corrected_ = true;
							pinyinSequences.push_back(std::move(pySequence));
							if (spanIsFuzzy)
								PERF_INC(fuzzySpansBuilt);
							else
								PERF_INC(editSpansBuilt);
						}
						else
						{
						coral::phonetics::PhoneticSequence pySequence(
							pinyinSequenceCopy,
							subPinyinSequence,
							from,
							vertexData.compact_id,
							(int)pathSize,
							false,
							isAbbreviatehasFull && hasSubPinyin);
						pinyinSequences.push_back(std::move(pySequence));
						}

						bool isHitEnglish = false;
						pinyinSequences.back().is_match_english_vocab_ = spanEnglishCount != 0;
						bool spanHitDictPhrases = false;
						wordLattice.process_sequence(pinyinSequences.back(),
							lexicon,
							fuzzyOnlySyllables,
							shouldLimit,
							sentHitDict,
							isHitEnglish,
							spanHitDictPhrases,
							englishCount != 0);
						// 全串命中词典完整词条 → 激活剪枝。全简拼序列不激活（每个音节都是
						// 声母/简拼时认为没有完整音节，jsbg 类输入本来就有严格 limit）。
						if (!pruneInnerSpans && allow_full_hit_pruning && s_enableFullHitPrune && spanHitDictPhrases
							&& !isAbbrevOnlySpan(pinyinSequenceCopy))
						{
							pruneInnerSpans = true;
						}
						if (i != 0 && isHitEnglish && is_mix_pinyin)
						{
							i = j - 1;
							break;
						}
					}
				}

				if (!pinyinSequenceCopy.empty())
				{
					pinyinSequenceCopy.pop_back();
				}
			}
		}
	}
	if (!isDoublepinyinMode) {
		add_full_jianpin_sequences(pinyinString, shortestPaths, pinyinSequences, wordLattice, lexicon, sentHitDict, fuzzyOnlySyllables, shouldLimit);
	}

	return true;
}

bool coral::phonetics::SyllableLattice::get_sub_pinyin_sequences(
	const coral::phonetics::ParseTextViews& sequence,
	coral::phonetics::ParseTextViews& subSequence,
	int index,
	bool hasSubPinyin)
{
	if (index >= (int)sequence.size())
		return hasSubPinyin;

	coral::phonetics::TextSlice jianPin;
	// 非简拼的模式下，判断是否能够获得拼音的简拼子串
	bool isUseJianPin = (!coral::phonetics::syntax::is_sheng_mu(sequence[index]) && coral::phonetics::syntax::get_jian_pin_code(sequence[index], jianPin));
	if (isUseJianPin) {
		subSequence.push_back(jianPin);
		hasSubPinyin = true;
	}
	else {
		subSequence.push_back(sequence[index]);
	}
	return get_sub_pinyin_sequences(sequence, subSequence, index + 1, hasSubPinyin);
}

bool coral::phonetics::SyllableLattice::get_sub_pinyin_sequences(
	const coral::phonetics::ParseTextViews& sequence,
	coral::phonetics::ParseTextViews& subSequence,
	int index,
	const vector<int>& condition,
	bool hasSubPinyin) const
{
	if (index >= (int)sequence.size())
		return hasSubPinyin;

	coral::phonetics::TextSlice jianPin;
	// 满足condition的时候，插入原始串
	bool isMatchCondition = (!condition.empty() && index < static_cast<int>(condition.size()) && condition[index] == 1);
	// 非简拼的模式下，判断是否能够获得拼音的简拼子串
	bool isUseJianPin = (!coral::phonetics::syntax::is_sheng_mu(sequence[index]) && coral::phonetics::syntax::get_jian_pin_code(sequence[index], jianPin));
	if (!isMatchCondition && isUseJianPin)
	{    // 不满足condition条件 && 非简拼且能取到子码
		subSequence.push_back(jianPin);
		hasSubPinyin = true;
	}
	else {
		subSequence.push_back(sequence[index]);
	}
	return get_sub_pinyin_sequences(sequence, subSequence, index + 1, condition, hasSubPinyin);
}

void coral::phonetics::SyllableLattice::add_split_pinyin(coral::phonetics::RankedPaths& shortestPaths)
{
	// 检查第一条路径中是否有 an/ai 结尾的音节，并枚举所有拆分组合
	if (shortestPaths.empty()) {
		return;
	}
	coral::phonetics::RankedPath firstPath = shortestPaths[0];
	struct SplitInfo {
		int position = 0;
		int suffixLen = 0;
		SplitInfo(int pos, int len) : position(pos), suffixLen(len) {}
	};
	vector<SplitInfo> splittablePositions;
	for (int i = 1; i < static_cast<int>(firstPath.path_.size()); ++i) {
		int edgeIndex = firstPath.path_[i].second;
		if (edgeIndex >= 0 && edgeIndex < static_cast<int>(edges_.size())) {
			const coral::phonetics::TextSlice& phrase = edges_[edgeIndex].data.phrase;
			// 检查是否以 an 或 ai 结尾（且长度>3，确保可拆分）
			if (phrase.length() > 3 &&
				(phrase.substr(phrase.length() - 2) == "an" ||
					phrase.substr(phrase.length() - 2) == "ai" ||
					phrase.substr(phrase.length() - 2) == "ao")) {
				const coral::phonetics::TextSlice prefix = phrase.substr(0, phrase.length() - 2);
				if (coral::phonetics::syntax::is_valid_full_pinyin(prefix)) {
					splittablePositions.emplace_back(i, 2);
				}
			}
			else if (phrase.length() > 2 && phrase.substr(phrase.length() - 1) == "e") {
				const coral::phonetics::TextSlice prefix = phrase.substr(0, phrase.length() - 1);
				if (coral::phonetics::syntax::is_valid_full_pinyin(prefix)) {
					splittablePositions.emplace_back(i, 1);
				}
			}
		}
	}

	// 如果没有可拆分的音节，直接返回
	if (splittablePositions.empty()) {
		return;
	}

	int n = static_cast<int>(splittablePositions.size());
	int totalCombinations = (1 << n) - 1; // 2^n - 1 (排除全不拆分的情况)
	shortestPaths.reserve(shortestPaths.size() + totalCombinations);
	// 枚举所有拆分组合（二进制位表示是否拆分）
	for (int mask = 1; mask <= totalCombinations; ++mask) {
		coral::phonetics::LatticePath newPath;

		// 复制起始节点
		newPath.emplace_back(firstPath.path_[0]);

		// 遍历原路径，根据 mask 决定是否拆分
		for (size_t i = 1; i < firstPath.path_.size(); ++i) {
			auto [vertexIndex, edgeIndex] = firstPath.path_[i];

			// 检查当前位置是否在可拆分列表中
			auto it = std::find_if(splittablePositions.begin(), splittablePositions.end(),
				[i](const SplitInfo& info) { return info.position == static_cast<int>(i); });
			if (it != splittablePositions.end()) {
				int posIndex = static_cast<int>(std::distance(splittablePositions.begin(), it));
				bool shouldSplit = (mask & (1 << posIndex)) != 0;

				if (shouldSplit && edgeIndex >= 0 && edgeIndex < static_cast<int>(edges_.size())) {
					const coral::phonetics::TextSlice& phrase = edges_[edgeIndex].data.phrase;
					int suffixLen = it->suffixLen;
					const coral::phonetics::TextSlice suffix = phrase.substr(phrase.length() - suffixLen);
					const coral::phonetics::TextSlice prefix = phrase.substr(0, phrase.length() - suffixLen);

					// 尝试在图中找到对应的拆分边：从前一个节点出发，找 prefix 和 suffix 对应的边
					int fromVertex = firstPath.path_[i - 1].first;

					// 查找中间节点和对应边
					bool foundSplit = false;
					for (int midEdgeIdx : vertices_[fromVertex].outgoing_edges) {
						if (midEdgeIdx >= 0 && midEdgeIdx < static_cast<int>(edges_.size())) {
							const auto& midEdge = edges_[midEdgeIdx];
							if (midEdge.data.phrase == prefix) {
								int midVertex = midEdge.to_vertex_index;
								// 再找从中间节点到目标节点的 suffix 边
								for (int suffixEdgeIdx : vertices_[midVertex].outgoing_edges) {
									if (suffixEdgeIdx >= 0 && suffixEdgeIdx < static_cast<int>(edges_.size())) {
										const auto& suffixEdge = edges_[suffixEdgeIdx];
										if (suffixEdge.data.phrase == suffix && suffixEdge.to_vertex_index == vertexIndex) {
											// 找到了拆分路径，添加两条边
											newPath.emplace_back(midVertex, midEdgeIdx);
											newPath.emplace_back(vertexIndex, suffixEdgeIdx);
											foundSplit = true;
											break;
										}
									}
								}
								if (foundSplit) {
									break;
								}
							}
						}
					}

					// 如果没找到拆分路径，保持原样
					if (!foundSplit) {
						newPath.emplace_back(vertexIndex, edgeIndex);
					}
					continue;
				}
			}

			// 不拆分，保持原样
			newPath.emplace_back(vertexIndex, edgeIndex);
		}
		coral::phonetics::ParseIntegers newEdgeCosts;
#ifdef _DEBUG
		shortestPaths.emplace_back(std::move(newPath), std::move(newEdgeCosts), edges_, 0);
#else
		shortestPaths.emplace_back(std::move(newPath), std::move(newEdgeCosts), 0);
#endif // DEBUG
	}
}

void coral::phonetics::SyllableLattice::add_english_paths(std::string_view input, const coral::phonetics::LexiconCatalog& lexicon, coral::phonetics::RankedPaths& paths)
{
	// 保持原有中文 Top-K。收缩前单独保留一条经过每个英文匹配的
	// 可达路径；中文剪枝不能抹掉其边界。
	std::vector<int> positions(input.size() + 1, 0);
	for (size_t i = 0; i < input.size(); ++i)
		positions[i + 1] = positions[i] + (input[i] != '\'');
	const size_t firstEnglish = edges_.size();
	std::vector<size_t> lengths;
	for (size_t start = 0; start < input.size(); ++start) {
		lexicon.get_english_lexicon().common_prefix_lengths(input.substr(start), lengths);
		for (const auto length : lengths) {
			const int from = positions[start], to = positions[start + length];
			if ((from != 0 || to != get_vertex_count() - 1)
				&& !coral::phonetics::LatinLexicon::allows_mixed_word(input.substr(start, length)))
				continue;
			const int index = add_edge(vertices_[from], vertices_[to]);
			auto& data = edges_[index].data;
			data.phrase = input.substr(start, length);
			data.weight = kEnglishCost;
			data.is_english_edge = true;
		}
	}
	if (edges_.size() == firstEnglish) return;
	const int n = get_vertex_count();
	constexpr int inf = 1000000000;
	std::vector<int> prefix(n, inf), suffix(n, inf), prev(n, -1), next(n, -1);
	prefix[0] = 0;
	suffix[n - 1] = 0;
	for (int from = 0; from < n; ++from) {
		if (prefix[from] == inf) continue;
		for (const int index : vertices_[from].outgoing_edges) {
			const auto& edge = edges_[index];
			const int cost = prefix[from] + edge.data.weight;
			if (cost < prefix[edge.to_vertex_index]) {
				prefix[edge.to_vertex_index] = cost;
				prev[edge.to_vertex_index] = index;
			}
		}
	}
	for (int from = n - 2; from >= 0; --from) {
		for (const int index : vertices_[from].outgoing_edges) {
			const auto& edge = edges_[index];
			if (suffix[edge.to_vertex_index] == inf) continue;
			const int cost = edge.data.weight + suffix[edge.to_vertex_index];
			if (cost < suffix[from]) { suffix[from] = cost; next[from] = index; }
		}
	}
	for (size_t index = firstEnglish; index < edges_.size(); ++index) {
		const auto& english = edges_[index];
		if (prefix[english.from_vertex_index] == inf || suffix[english.to_vertex_index] == inf) continue;
		std::vector<int> indices;
		for (int at = english.from_vertex_index; at != 0;) {
			const int edgeIndex = prev[at];
			indices.push_back(edgeIndex);
			at = edges_[edgeIndex].from_vertex_index;
		}
		std::reverse(indices.begin(), indices.end());
		indices.push_back(static_cast<int>(index));
		for (int at = english.to_vertex_index; at != n - 1;) {
			const int edgeIndex = next[at];
			indices.push_back(edgeIndex);
			at = edges_[edgeIndex].to_vertex_index;
		}
		coral::phonetics::LatticePath path{{0, -1}};
		coral::phonetics::ParseIntegers costs;
		int cost = 0;
		for (const int edgeIndex : indices) {
			const auto& edge = edges_[edgeIndex];
			path.emplace_back(edge.to_vertex_index, edgeIndex);
			costs.push_back(edge.data.weight);
			cost += edge.data.weight;
		}
		if (std::any_of(paths.begin(), paths.end(), [&](const coral::phonetics::RankedPath& old) { return old.path_ == path; })) continue;
		paths.emplace_back(std::move(path), std::move(costs), cost);
	}
}

bool coral::phonetics::SyllableLattice::shrink_graph_by_yen_ksp(
	int k,
	int dist,
	coral::phonetics::RankedPaths& shortestPaths,
	std::string_view input,
	const coral::phonetics::LexiconCatalog* lexicon)
{
	coral::phonetics::SyllablePathSearch yenKSP;
	yenKSP.calculate_yen_ksp(*this, k, dist, shortestPaths);
	add_split_pinyin(shortestPaths);
	if (lexicon != nullptr) add_english_paths(input, *lexicon, shortestPaths);
	//todo shift输入大写字符会导致这里为空，这里直接return false,后续需要处理输入法应该可以输入大写单词
	if (shortestPaths.empty())
		return false;

	{
		set<int> vertices;
		set<int> validEdges;
		for (int i = 0; i < static_cast<int>(edges_.size()); ++i)
		{
			const auto& edge = edges_[i];
			if (edge.from_vertex_index != 0)
				break;
			validEdges.insert(static_cast<int>(i));
			vertices.insert(edge.to_vertex_index);
		}

		for (auto pathIter = shortestPaths.cbegin();
			pathIter != shortestPaths.cend(); ++pathIter)
		{
			for (const auto& [vIndex, eIndex] : pathIter->path_) {
				if (vIndex < 0 || vIndex >= static_cast<int>(vertices_.size()))
				{
					assert(false);
					return false;
				}
				//给节点排序
				if (eIndex >= 0) { // 排除起始边
					validEdges.insert(eIndex);
				}
				vertices.insert(vIndex);
			}
		}

		int validVertexCount = 0;
		for (auto it = vertices.cbegin(); it != vertices.cend(); ++it) {
			if (vertices_[*it].data.compact_id < 0)
				vertices_[*it].data.compact_id = validVertexCount++;
		}

		vector<coral::phonetics::SyllableVertex> shrinked;
		shrinked.resize(validVertexCount);
		for (int i = 0; i < validVertexCount; i++)
		{
			shrinked[i].id = static_cast<int>(i);
			shrinked[i].data.compact_id = static_cast<int>(i);
#ifdef _DEBUG
			shrinked[i].data.word = vertices_[(*std::next(vertices.cbegin(), i))].data.word;
#endif
		}
		// 待办：预留容量
		vector<coral::phonetics::SyllableEdge> shrinkedEdges;
		// 使用 vector 实现 O(1) 的边索引映射
		vector<int> oldEdgeToNewEdge(edges_.size(), -1);
		for (const int edgeId : validEdges) {
			if (edgeId < 0 || edgeId >= static_cast<int>(edges_.size()))
			{
				assert(false);
				return false;
			}
			const auto& edge = edges_[edgeId];
			int fromShrinkedId = vertices_[edge.from_vertex_index].data.compact_id;
			int toShrinkedId = vertices_[edge.to_vertex_index].data.compact_id;
			if (fromShrinkedId >= 0 && toShrinkedId >= 0) {
				int newEdgeIndex = add_temp_edge(shrinked[fromShrinkedId], shrinked[toShrinkedId], shrinkedEdges);
				if (newEdgeIndex < 0 || newEdgeIndex >= static_cast<int>(shrinkedEdges.size()))
				{
					assert(false);
					return false;
				}
				// 保存边索引映射关系
				oldEdgeToNewEdge[edgeId] = newEdgeIndex;
				
				// 设置边数据。方案B：模糊边/编辑边必须整份拷贝——isFuzzyEdge/isEditEdge
				// 标记、originalPhrase（原串视图）与已含惩罚的权重在缩图后仍要参与
				// 路径组装与 span 标记，只拷 phrase 会把它们全部丢掉
				shrinkedEdges[newEdgeIndex].data = edge.data;
				int weight = 0;
				if (edge.data.is_fuzzy_edge || edge.data.is_edit_edge || edge.data.is_english_edge || edge.data.weight < 0) {
					// 模糊/编辑边权重 = 基础 cost + 惩罚，直接保留
					weight = edge.data.weight;
				}
				else {
					weight = coral::phonetics::syntax::is_jian_pin(edge.data.phrase) ? kJianCost : kFullCost;
					if (coral::phonetics::syntax::is_sheng_mu(edge.data.phrase))
						weight = kShenmuCost;
				}
				shrinkedEdges[newEdgeIndex].data.weight = weight;
			}
		}
		// 更新 shortestPaths 中的索引为缩图后的新索引
		for (auto& pathInfo : shortestPaths) {
			for (auto& [vertexIndex, edgeIndex] : pathInfo.path_) {
				// 更新节点索引
				if (vertexIndex >= 0 && vertexIndex < static_cast<int>(vertices_.size())) {
					vertexIndex = vertices_[vertexIndex].data.compact_id;
				}
				// 更新边索引 (O(1) 直接数组访问)
				if (edgeIndex >= 0 && edgeIndex < static_cast<int>(oldEdgeToNewEdge.size())) {
					edgeIndex = oldEdgeToNewEdge[edgeIndex];
				}
			}
		}
		vertices_ = std::move(shrinked);
		edges_ = std::move(shrinkedEdges);
	}
	return true;
}


int coral::phonetics::SyllableLattice::add_temp_edge(coral::phonetics::SyllableVertex& from, coral::phonetics::SyllableVertex& to, vector<coral::phonetics::SyllableEdge>& edgesVec)
{
	coral::phonetics::SyllableEdge e(static_cast<coral::phonetics::LatticeIndex>(from.id), static_cast<coral::phonetics::LatticeIndex>(to.id));
	edgesVec.emplace_back(e);
	int eIndex = static_cast<int>(edgesVec.size()) - 1;
	from.outgoing_edges.push_back(static_cast<coral::phonetics::LatticeIndex>(eIndex));
	to.incoming_edges.push_back(static_cast<coral::phonetics::LatticeIndex>(eIndex));
	return eIndex;
};

void coral::phonetics::SyllableLattice::add_full_jianpin_sequences(const string& pinyinString,
	const coral::phonetics::RankedPaths& shortestPaths, 
	coral::phonetics::ParseSequences& pinyinSequences, 
	coral::phonetics::CandidateLattice& wordLattice, 
	coral::phonetics::LexiconCatalog& lexiconmanager, 
	bool& sentHitDict, 
	const std::unordered_set<std::string_view>& fuzzyOnlySyllables, 
	const bool shouldLimit)
{
	if (shortestPaths.empty() || shortestPaths[0].cost_ <= kFullCost || static_cast<int>(pinyinString.length()) <= 1)
		return;

	coral::phonetics::ParseTextViews pinyin_sequence;
	bool pathValid = true;
	for (int i = 0; i < static_cast<int>(pinyinString.size()); ++i)
	{
		string_view charView(pinyinString.data() + i, 1);
		if (coral::phonetics::syntax::is_fu_yuanyin(charView))
		{
			pathValid = false;
			break;
		}
		pinyin_sequence.push_back(charView);
	}

	if (!pathValid || pinyin_sequence.empty())
		return;

	int from = 0, to = static_cast<int>(vertices_.size()) - 1;
	coral::phonetics::PhoneticSequence pySequence(
		pinyin_sequence,
		coral::phonetics::ParseTextViews{},
		from,
		to,
		to - from + 1,
		false,
		false
	);
	pinyinSequences.push_back(std::move(pySequence));

	bool isHitEnglish = false;
	bool spanHitDictPhrases = false;
	wordLattice.process_sequence(pinyinSequences.back(),
		lexiconmanager,
		fuzzyOnlySyllables,
		shouldLimit,
		sentHitDict,
		isHitEnglish,
		spanHitDictPhrases);
}

bool coral::phonetics::SyllableLattice::write_to_file(const std::filesystem::path& filepath) const
{
	std::error_code ec;
	std::filesystem::create_directories(filepath.parent_path(), ec);
	if (ec)
		return false;

	std::ostringstream buffer;
	buffer << std::setprecision(std::numeric_limits<coral::phonetics::Score>::max_digits10);

	buffer << "[BASIC_INFO]\n";
	buffer << "vertex_count=" << vertices_.size() << '\n';
	buffer << "edge_count=" << edges_.size() << '\n';
	buffer << '\n';

	buffer << "[VERTICES]\n";
	for (size_t i = 0; i < vertices_.size(); ++i)
	{
		const auto& vertex = vertices_[i];
#ifdef _DEBUG
		buffer << "vertex_" << i << ": id=" << vertex.id << ", shrinked_id=" << vertex.data.compact_id << ", word=\"" << vertex.data.word << "\"";
#else
		buffer << "vertex_" << i << ": id=" << vertex.id << ", shrinked_id=" << vertex.data.compact_id;
#endif

		buffer << ", out_edges=[";
		for (size_t j = 0; j < vertex.outgoing_edges.size(); ++j)
		{
			if (j > 0)
				buffer << ',';
			buffer << vertex.outgoing_edges[j];
		}
		buffer << "], in_edges=[";
		for (size_t j = 0; j < vertex.incoming_edges.size(); ++j)
		{
			if (j > 0)
				buffer << ',';
			buffer << vertex.incoming_edges[j];
		}
		buffer << "]\n";
	}
	buffer << '\n';

	buffer << "[EDGES]\n";
	for (size_t i = 0; i < edges_.size(); ++i)
	{
		const auto& edge = edges_[i];
		buffer << "edge_" << i << ": from=" << edge.from_vertex_index << ", to=" << edge.to_vertex_index << ", phrase=\"" << edge.data.phrase << "\"" << ", weight=" << edge.data.weight << '\n';
	}

	std::ofstream file{filepath};
	if (!file.is_open())
		return false;

	file << buffer.str();
	return true;
}

bool coral::phonetics::SyllableLattice::export_to_dot(const std::filesystem::path& filepath) const
{
#ifdef _DEBUG
	std::error_code ec;
	std::filesystem::create_directories(filepath.parent_path(), ec);
	if (ec)
		return false;

	std::ostringstream buffer;
	buffer << std::setprecision(6);

	buffer << "digraph SyllableGraph {\n";
	buffer << "    charset=\"UTF-8\";\n";
	buffer << "    rankdir=LR;\n";
	buffer << "    node [shape=circle, fontname=\"Microsoft YaHei\"];\n";
	buffer << "    edge [fontname=\"Microsoft YaHei\"];\n\n";

	// 输出节点
	for (size_t i = 0; i < vertices_.size(); ++i)
	{
		const auto& vertex = vertices_[i];
		buffer << "    V" << vertex.id;
		buffer << " [label=\"" << vertex.id << "\"]";
		buffer << ";\n";
	}
	buffer << "\n";

	// 输出边
	for (const auto& edge : edges_)
	{
		buffer << "    V" << edge.from_vertex_index << " -> V" << edge.to_vertex_index
			   << " [label=\"" << edge.data.phrase << "\\n" << edge.data.weight << "\"];\n";
	}

	buffer << "}\n";

	std::ofstream file{filepath, std::ios::binary};
	if (!file.is_open())
		return false;

	file << buffer.str();
	return true;
#endif
	return false;
}

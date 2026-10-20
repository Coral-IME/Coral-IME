/*
* @brief: 从 coral_lexicon.csv + coral_languagemodel.csv 直接生成 mainlexicon.coral 与 languagemodel.coral
*         （替代 index.py → m2_hanzi_to_index.py → compile_resource_files → coral_lexicon_builder 的中间链路）
*
* 语义对齐：
*  - 词 index 分配：与 script/coral/index.py 一致，(word, fullPinyin) 首次出现递增分配；
*  - m2 词→index 解析：与 script/coral/m2_hanzi_to_index.py 一致，
*    优先 (word, fullPinyin) 精确匹配，失败回退该 word 首个 index，再失败跳过该行；
*  - mainlexicon.coral：与 CPrimaryBin_buildFromCsv 同一份口径（champion + m2 优先 + weight 降序）；
*  - languagemodel.coral：与 CGBinModel::buildFromCsv 同一份口径（uni 并入一元 log 概率、
*    PREDICT_THRESHOLD 过滤、二元对 last-wins、单级回退到 start）。
*/
#include "phonetic_types.h"
#include "lexicon_table.h"
#include "bigram_table.h"
#include "latin_identity.h"
#include "source_csv.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	uint32_t alignUpL(uint32_t x, uint32_t a)
	{
		return (x + (a - 1u)) & ~(a - 1u);
	}

	void stripEol(std::string& s)
	{
		while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
			s.pop_back();
	}

	std::string trimSpace(const std::string& s)
	{
		size_t b = s.find_first_not_of(" \t");
		if (b == std::string::npos)
			return {};
		size_t e = s.find_last_not_of(" \t");
		return s.substr(b, e - b + 1);
	}

	std::vector<std::string> splitCsv(const std::string& line)
	{
		std::vector<std::string> cols;
		std::stringstream ss(line);
		std::string seg;
		while (std::getline(ss, seg, ','))
			cols.push_back(seg);
		return cols;
	}

	struct LsortEntry
	{
		uint32_t index = 0;
		std::string pinyin;
		std::string word;
		std::string fullPinyin;
		long long frequency = 0;
		float backprob = 0.f;
		bool inM2 = false;
	};

	struct LsortData
	{
		std::vector<LsortEntry> entries;                     // 全部行（含重复 index 行）
		std::unordered_map<std::string, uint32_t> wordToFirstIndex; // legacy 回退：word 首个 index
	};

	std::string englishKey(const std::string& word)
	{
		return coral::english_identity_key(word);
	}

	bool mergeEnglish(LsortData& data, const std::filesystem::path& path)
	{
		if (path.empty()) return true;
		struct Identity { uint32_t id; long long frequency; };
		std::map<std::string, Identity> words;
		uint32_t nextId = 0;
		for (const auto& entry : data.entries) {
			nextId = std::max(nextId, entry.index + 1);
			const auto key = englishKey(entry.word);
			if (key.empty()) continue;
			auto [it, inserted] = words.emplace(key, Identity{entry.index, entry.frequency});
			if (!inserted) {
				it->second.id = std::min(it->second.id, entry.index);
				it->second.frequency = std::max(it->second.frequency, entry.frequency);
			}
		}
		std::ifstream file(path);
		if (!file) return false;
		std::unordered_set<std::string> mainWords;
		for (const auto& item : words) mainWords.insert(item.first);
		try {
		coral::data::read_source_csv_header(file, {"search_word", "display_word", "frequency", "can_split"});
		std::vector<std::string> fields;
		while (coral::data::read_source_csv_row(file, fields)) {
			if (fields.size() == 1 && fields[0].empty()) continue;
			if (fields.size() != 4) throw std::runtime_error("Expected four English CSV fields");
			const auto& spelling = fields[0];
			const auto& display = fields[1];
			size_t end = 0;
			const long long frequency = std::stoll(fields[2], &end);
			if (end != fields[2].size() || frequency <= 0 || frequency > kMaxCount
				|| englishKey(spelling).empty() || englishKey(display).empty() || (fields[3] != "0" && fields[3] != "1"))
				throw std::runtime_error("Invalid English CSV value");
			const auto key = englishKey(display);
			auto it = words.find(key);
			if (it == words.end()) {
				if (nextId >= kCloudBaseOffset) return false;
				it = words.emplace(key, Identity{nextId++, frequency}).first;
			}
			else if (mainWords.count(key) == 0) it->second.frequency = std::max(it->second.frequency, frequency);
			// 保留键：仅模型使用的词表绝不进入拼音音节表。
			data.entries.push_back(LsortEntry{it->second.id, "~en:" + englishKey(spelling), display,
				englishKey(spelling), it->second.frequency, 0.0f, false});
		}
		} catch (const std::exception& error) {
			std::cerr << "Error: invalid English CSV " << path.string() << ": " << error.what() << std::endl;
			return false;
		}
		for (auto& entry : data.entries) {
			const auto it = words.find(englishKey(entry.word));
			if (it == words.end()) continue;
			entry.index = it->second.id;
			entry.frequency = it->second.frequency;
			data.wordToFirstIndex[entry.word] = entry.index;
			data.wordToFirstIndex[englishKey(entry.word)] = entry.index;
		}
		return true;
	}

	// 读首行表头并剥掉 BOM
	bool readHeaderLine(std::ifstream& file, std::string& line)
	{
		if (!std::getline(file, line))
			return false;
		if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
			static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
			line.erase(0, 3);
		stripEol(line);
		return true;
	}

	// 解析 coral_lexicon.csv：pinyin,frequency,word,fullPinyin[,identifier[,isPolyphonic[,in_m2]]]，
	// 可选列按表头名定位（兼容未来加 backprob 列）
	bool parseLsort(const std::filesystem::path& path, LsortData& out)
	{
		std::ifstream file(path);
		if (!file.is_open())
		{
			std::cerr << "Error: coral_lexicon.csv not found: " << path.string() << std::endl;
			return false;
		}
		std::string line;
		if (!readHeaderLine(file, line) || line.empty())
		{
			std::cerr << "Error: coral_lexicon.csv header missing." << std::endl;
			return false;
		}
		const std::vector<std::string> header = splitCsv(line);
		auto colOf = [&header](const char* name) -> int {
			for (size_t i = 0; i < header.size(); ++i)
				if (trimSpace(header[i]) == name)
					return static_cast<int>(i);
			return -1;
		};
		const int cPinyin = colOf("pinyin");
		const int cFreq = colOf("frequency");
		const int cWord = colOf("word");
		const int cFull = colOf("fullPinyin");
		const int cBack = colOf("backprob"); // 可选
		const int cInM2 = colOf("in_m2");    // 可选
		if (cPinyin < 0 || cFreq < 0 || cWord < 0 || cFull < 0)
		{
			std::cerr << "Error: coral_lexicon.csv header must contain pinyin/frequency/word/fullPinyin columns." << std::endl;
			return false;
		}

		std::map<std::pair<std::string, std::string>, uint32_t> seen;
		std::unordered_map<std::string, uint32_t> englishFirst;
		bool hasEnglishVocabulary = false;
		uint32_t nextIndex = 0;
		uint64_t lineNo = 1;
		uint64_t badLines = 0;
		while (std::getline(file, line))
		{
			++lineNo;
			stripEol(line);
			if (line.empty())
				continue;
			const std::vector<std::string> cols = splitCsv(line);
			if (static_cast<int>(cols.size()) <= std::max({cPinyin, cFreq, cWord, cFull}))
			{
				++badLines;
				continue;
			}
			LsortEntry e;
			e.pinyin = cols[cPinyin];
			e.word = cols[cWord];
			e.fullPinyin = cols[cFull];
			try
			{
				e.frequency = std::stoll(cols[cFreq]);
				e.backprob = cBack >= 0 && static_cast<int>(cols.size()) > cBack && !cols[cBack].empty()
					? std::stof(cols[cBack]) : 0.f; // index.py 无 backprob 列时写 0.000000
				e.inM2 = cInM2 >= 0 && static_cast<int>(cols.size()) > cInM2 &&
					!cols[cInM2].empty() && std::stoi(cols[cInM2]) != 0;
			}
			catch (const std::exception&)
			{
				++badLines;
				continue;
			}
			if (e.pinyin.empty() || e.word.empty() || e.fullPinyin.empty())
			{
				++badLines;
				continue;
			}
			const auto identity = englishKey(e.word);
			const bool modelOnly = e.pinyin.compare(0, 4, "~en:") == 0 && !identity.empty();
			hasEnglishVocabulary |= modelOnly;
			const auto key = std::make_pair(e.word, e.fullPinyin);
			const auto it = seen.find(key);
			if (modelOnly && englishFirst.count(identity)) {
				e.index = englishFirst.at(identity);
			}
			else if (it != seen.end())
			{
				e.index = it->second;
			}
			else
			{
				e.index = nextIndex;
				seen.emplace(key, nextIndex);
				++nextIndex;
			}
			if (!identity.empty()) englishFirst.emplace(identity, e.index);
			out.wordToFirstIndex.emplace(e.word, e.index); // 首个出现的 index 生效
			out.entries.push_back(std::move(e));
		}
		if (hasEnglishVocabulary) {
			for (auto& entry : out.entries) {
				const auto identity = englishKey(entry.word);
				if (identity.empty()) continue;
				entry.index = englishFirst.at(identity);
				out.wordToFirstIndex[entry.word] = entry.index;
				out.wordToFirstIndex[identity] = entry.index;
			}
		}
		if (out.entries.empty())
		{
			std::cerr << "Error: no valid entries parsed from coral_lexicon.csv." << std::endl;
			return false;
		}
		if (badLines)
			std::cerr << "Warning: " << badLines << " malformed lines skipped in coral_lexicon.csv." << std::endl;
		std::cout << "coral_lexicon.csv parsed: entries=" << out.entries.size()
			<< " uniqueWords=" << nextIndex << std::endl;
		return true;
	}

	// 与 m2_hanzi_to_index.py 的 resolve_word_index 一致
	uint32_t resolveM2Word(const std::string& word, const std::string& fullPinyin,
		const std::map<std::pair<std::string, std::string>, uint32_t>& exact,
		const LsortData& lsort, bool& ok)
	{
		ok = false;
		const std::string fp = trimSpace(fullPinyin);
		if (!fp.empty())
		{
			const auto it = exact.find(std::make_pair(word, fp));
			if (it != exact.end())
			{
				ok = true;
				return it->second;
			}
		}
		auto lit = lsort.wordToFirstIndex.find(word);
		if (lit == lsort.wordToFirstIndex.end() && !englishKey(word).empty())
			lit = lsort.wordToFirstIndex.find(englishKey(word));
		if (lit != lsort.wordToFirstIndex.end())
		{
			ok = true;
			return lit->second;
		}
		return 0;
	}

	// ---- mainlexicon.coral：与 CPrimaryBin_buildFromCsv 同口径 ----
	struct BuildPhraseInfoL
	{
		std::string phrase;
		std::string pinyin;
		float phrase_weight = 0.f;
		uint32_t id = 0;
		float phrase_backprob = 0.f;
		bool b_in_m2 = false;
	};

	bool writePrimaryGBin(const LsortData& lsort, const std::filesystem::path& outPath)
	{
		std::map<std::string, std::vector<BuildPhraseInfoL>> raw_data;
		for (const LsortEntry& e : lsort.entries)
		{
			if (e.frequency <= 0)
				continue;
			BuildPhraseInfoL ph{ e.word, e.fullPinyin,
				static_cast<float>(log(static_cast<float>(e.frequency) * kInvMaxcount)),
				e.index, e.backprob, e.inM2 };
			raw_data[e.pinyin].push_back(std::move(ph));
		}
		if (raw_data.empty())
		{
			std::cerr << "Error: no valid primary lexicon entries for mainlexicon.coral." << std::endl;
			return false;
		}

		// 排序语义与 buildLexicon 一致：weight 降序 + champion/m2 优先
		std::vector<std::pair<std::string_view, std::vector<BuildPhraseInfoL>*>> sortedKeys;
		sortedKeys.reserve(raw_data.size());
		for (auto& pair : raw_data)
		{
			auto& phrases = pair.second;
			std::sort(phrases.begin(), phrases.end(),
				[](const BuildPhraseInfoL& a, const BuildPhraseInfoL& b) { return a.phrase_weight > b.phrase_weight; });
			if (phrases.size() > 1)
			{
				BuildPhraseInfoL champion = std::move(phrases[0]);
				std::vector<BuildPhraseInfoL> m2Items, normalItems;
				for (size_t i = 1; i < phrases.size(); ++i)
				{
					if (phrases[i].b_in_m2)
						m2Items.push_back(std::move(phrases[i]));
					else
						normalItems.push_back(std::move(phrases[i]));
				}
				phrases.clear();
				phrases.push_back(std::move(champion));
				phrases.insert(phrases.end(), std::make_move_iterator(m2Items.begin()),
					std::make_move_iterator(m2Items.end()));
				phrases.insert(phrases.end(), std::make_move_iterator(normalItems.begin()),
					std::make_move_iterator(normalItems.end()));
			}
			sortedKeys.emplace_back(pair.first, &pair.second);
		}

		std::string pool;
		pool.reserve(32u << 20);
		std::vector<coral::storage::LexiconKeyRecord> keys;
		std::vector<coral::storage::LexiconEntryRecord> entries;
		keys.reserve(sortedKeys.size());
		entries.reserve(1u << 20);

		for (const auto& [keyStr, phrasesPtr] : sortedKeys)
		{
			coral::storage::LexiconKeyRecord kr{};
			kr.key_prefix = primary_bin_key_prefix(keyStr.data(), keyStr.size());
			kr.key_offset = static_cast<uint32_t>(pool.size());
			kr.key_length = static_cast<uint32_t>(keyStr.size());
			pool.append(keyStr.data(), keyStr.size());
			pool.push_back('\0');
			kr.entry_offset = static_cast<uint32_t>(entries.size());
			kr.entry_count = static_cast<uint32_t>(phrasesPtr->size());
			for (const BuildPhraseInfoL& p : *phrasesPtr)
			{
				coral::storage::LexiconEntryRecord e{};
				e.word_offset = static_cast<uint32_t>(pool.size());
				e.word_length = static_cast<uint32_t>(p.phrase.size());
				pool.append(p.phrase.data(), p.phrase.size());
				pool.push_back('\0');
				e.pinyin_offset = static_cast<uint32_t>(pool.size());
				e.pinyin_length = static_cast<uint32_t>(p.pinyin.size());
				pool.append(p.pinyin.data(), p.pinyin.size());
				pool.push_back('\0');
				e.phrase_id = p.id;
				e.weight = p.phrase_weight;
				e.backprob = p.phrase_backprob;
				e.b_in_m2 = p.b_in_m2 ? 1u : 0u;
				entries.push_back(e);
			}
			keys.push_back(kr);
		}

		coral::storage::LexiconFileHeader hdr{};
		std::memcpy(hdr.magic, kPrimaryBinMagic, 8);
		hdr.version = kPrimaryBinVersion;
		hdr.key_count = static_cast<uint32_t>(keys.size());
		hdr.entry_count = static_cast<uint32_t>(entries.size());
		hdr.pool_size = static_cast<uint32_t>(pool.size());
		uint32_t off = sizeof(coral::storage::LexiconFileHeader);
		hdr.keys_offset = off;
		off += static_cast<uint32_t>(keys.size() * sizeof(coral::storage::LexiconKeyRecord));
		off = alignUpL(off, 64);
		hdr.entries_offset = off;
		off += static_cast<uint32_t>(entries.size() * sizeof(coral::storage::LexiconEntryRecord));
		off = alignUpL(off, 64);
		hdr.pool_offset = off;
		off += hdr.pool_size;
		hdr.file_size = off;

		std::vector<uint8_t> buf(off, 0);
		auto wr = [&buf](const uint32_t o, const void* d, const size_t n) { std::memcpy(buf.data() + o, d, n); };
		wr(hdr.keys_offset, keys.data(), keys.size() * sizeof(coral::storage::LexiconKeyRecord));
		wr(hdr.entries_offset, entries.data(), entries.size() * sizeof(coral::storage::LexiconEntryRecord));
		wr(hdr.pool_offset, pool.data(), pool.size());
		std::memcpy(buf.data(), &hdr, sizeof(hdr));

		std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
		{
			std::cerr << "Error: cannot write " << outPath.string() << std::endl;
			return false;
		}
		out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
		out.close();

		// bind 自校验
		coral::phonetics::LexiconTable bin;
		if (!bin.bind_data(buf.data(), buf.size()))
		{
			std::cerr << "Error: mainlexicon.coral self-verification (bindData) failed." << std::endl;
			return false;
		}
		std::cout << "mainlexicon.coral built: keys=" << hdr.key_count << " entries=" << hdr.entry_count
			<< " size=" << buf.size() << " (verified)" << std::endl;
		return true;
	}

	// ---- languagemodel.coral：与 CGBinModel::buildFromCsv 同口径 ----
	bool writeGBin(const LsortData& lsort,
		const std::map<std::pair<std::string, std::string>, uint32_t>& exact,
		const std::filesystem::path& m2Path,
		const std::filesystem::path& outPath)
	{
		// 1. 一元概率（PREDICT_THRESHOLD 过滤，与 buildFromCsv 一致）
		std::unordered_map<uint32_t, float> idToUniProb;
		std::unordered_set<uint32_t> englishIds;
		uint32_t maxUniId = 0;
		for (const LsortEntry& e : lsort.entries)
		{
			if (e.frequency <= 0 || e.index >= kCloudBaseOffset)
				continue;
			const float uniProb = std::log(static_cast<float>(e.frequency) * kInvMaxcount);
			const bool english = !englishKey(e.word).empty();
			if (uniProb < kPredictThreshold && !english)
				continue;
			if (english) englishIds.insert(e.index);
			idToUniProb[e.index] = uniProb;
			maxUniId = std::max(maxUniId, e.index);
		}
		if (idToUniProb.empty())
		{
			std::cerr << "Error: no valid unigram entries for languagemodel.coral." << std::endl;
			return false;
		}

		// 2. coral_languagemodel.csv → 二元对 (last-wins，与 converted_m2 + buildFromCsv 链路一致)
		std::unordered_map<uint64_t, float> pairToProb;
		pairToProb.reserve(1500000);
		{
			std::ifstream file(m2Path);
			if (!file.is_open())
			{
				std::cerr << "Error: coral_languagemodel.csv not found: " << m2Path.string() << std::endl;
				return false;
			}
			std::string line;
			if (!readHeaderLine(file, line))
			{
				std::cerr << "Error: coral_languagemodel.csv header missing." << std::endl;
				return false;
			}
			const std::vector<std::string> header = splitCsv(line);
			auto colOf = [&header](const char* name) -> int {
				for (size_t i = 0; i < header.size(); ++i)
					if (trimSpace(header[i]) == name)
						return static_cast<int>(i);
				return -1;
			};
			const int cProb = colOf("prob");
			const int cPrev = colOf("prev_word");
			const int cNext = colOf("next_word");
			const int cPrevPy = colOf("prev_fullPinyin");
			const int cNextPy = colOf("next_fullPinyin");
			if (cProb < 0 || cPrev < 0 || cNext < 0)
			{
				std::cerr << "Error: coral_languagemodel.csv header must contain prob/prev_word/next_word columns." << std::endl;
				return false;
			}
			uint64_t lineNo = 1;
			uint64_t skipped = 0;
			while (std::getline(file, line))
			{
				++lineNo;
				stripEol(line);
				if (line.empty())
					continue;
				const std::vector<std::string> cols = splitCsv(line);
				const int need = std::max({cProb, cPrev, cNext});
				if (static_cast<int>(cols.size()) <= need)
				{
					++skipped;
					continue;
				}
				float prob = 0.f;
				try
				{
					prob = std::stof(cols[cProb]);
				}
				catch (const std::exception&)
				{
					++skipped;
					continue;
				}
				const std::string prevWord = trimSpace(cols[cPrev]);
				const std::string nextWord = trimSpace(cols[cNext]);
				// 高频终止词使用一元评分，绝不使用二元加成。
				const auto isTerminalWord = [](const std::string& word) {
					return word == u8"的" || word == u8"了" || word == u8"是";
				};
				if (isTerminalWord(prevWord) || isTerminalWord(nextWord))
					continue;
				const std::string prevPy = cPrevPy >= 0 && static_cast<int>(cols.size()) > cPrevPy ? cols[cPrevPy] : "";
				const std::string nextPy = cNextPy >= 0 && static_cast<int>(cols.size()) > cNextPy ? cols[cNextPy] : "";
				bool okPrev = false, okNext = false;
				const uint32_t prevIndex = resolveM2Word(prevWord, prevPy, exact, lsort, okPrev);
				const uint32_t nextIndex = resolveM2Word(nextWord, nextPy, exact, lsort, okNext);
				if (!okPrev || !okNext || prevIndex >= kCloudBaseOffset || nextIndex >= kCloudBaseOffset)
				{
					++skipped;
					continue;
				}
				pairToProb[(static_cast<uint64_t>(prevIndex) << 32) | nextIndex] = prob;
			}
			if (skipped)
				std::cout << "coral_languagemodel.csv: skipped " << skipped << " unresolvable/malformed rows." << std::endl;
		}
		if (pairToProb.empty())
		{
			std::cerr << "Error: no valid bigram pairs parsed from coral_languagemodel.csv." << std::endl;
			return false;
		}

		// 3. 状态分配
		std::vector<uint64_t> keys;
		keys.reserve(pairToProb.size());
		for (const auto& pair : pairToProb)
			keys.push_back(pair.first);
		std::sort(keys.begin(), keys.end());

		std::unordered_map<uint32_t, int32_t> idToState;
		for (const uint64_t key : keys)
		{
			idToState.emplace(static_cast<uint32_t>(key >> 32), 0);
			idToState.emplace(static_cast<uint32_t>(key & 0xFFFFFFFFu), 0);
		}
		// 仅一元的词同样消耗自身状态，包括纯英文词元。
		for (const auto id : englishIds) idToState.emplace(id, 0);
		{
			std::vector<uint32_t> ids;
			ids.reserve(idToState.size());
			for (const auto& kv : idToState)
				ids.push_back(kv.first);
			std::sort(ids.begin(), ids.end());
			for (size_t i = 0; i < ids.size(); ++i)
				idToState[ids[i]] = static_cast<int32_t>(i + 1);
		}
		const uint32_t state_count = static_cast<uint32_t>(idToState.size()) + 1;

		// 4. uni 区
		const uint32_t uni_size = maxUniId + 1;
		std::vector<coral::storage::BigramUnigramRecord> uni(uni_size, coral::storage::BigramUnigramRecord{ kGBinNoState, 0.0f });
		for (const auto& [id, prob] : idToUniProb)
		{
			const auto it = idToState.find(id);
			uni[id] = coral::storage::BigramUnigramRecord{ it != idToState.end() ? it->second : kGBinNoState, prob };
		}

		// 5. 弧区 + 回退权重
		std::vector<coral::storage::BigramStateRecord> states(state_count);
		std::vector<coral::storage::BigramArcRecord> smallArcs;
		std::vector<coral::storage::BigramHashRecord> hashSlots;
		smallArcs.reserve(1u << 20);
		hashSlots.reserve(1u << 21);
		uint32_t nHeavy = 0;
		states[0] = coral::storage::BigramStateRecord{ 0, 0, kGBinNoState, 0.0f, 0.0f, 0 };

		size_t i = 0;
		while (i < keys.size())
		{
			const uint32_t prev = static_cast<uint32_t>(keys[i] >> 32);
			size_t j = i;
			while (j < keys.size() && static_cast<uint32_t>(keys[j] >> 32) == prev)
				++j;
			const int32_t sIdx = idToState.at(prev);

			std::vector<coral::storage::BigramArcRecord> arcs;
			arcs.reserve(j - i);
			double probSum = 0.0;
			for (size_t k = i; k < j; ++k)
			{
				const uint32_t succ = static_cast<uint32_t>(keys[k] & 0xFFFFFFFFu);
				const float prob = pairToProb[keys[k]];
				probSum += std::exp(static_cast<double>(prob));
				arcs.push_back(coral::storage::BigramArcRecord{ succ, idToState.at(succ), prob });
			}

			constexpr double kMinBackoffMass = 1e-6;
			const double mass = std::max(1.0 - probSum, kMinBackoffMass);
			const float backoff_weight = static_cast<float>(std::log(mass));

			uint32_t flags = kGBinFlagFinal;
			uint32_t off = 0;
			uint32_t count = 0;
			if (arcs.size() > kGBinHeavyThreshold)
			{
				uint32_t cap = 8;
				while (cap < arcs.size() * 2u)
				{
					cap <<= 1;
					if (cap == 0)
					{
						cap = 8;
						break;
					}
				}
				flags |= kGBinFlagHeavy;
				off = static_cast<uint32_t>(hashSlots.size());
				count = cap;
				const size_t base = hashSlots.size();
				hashSlots.resize(base + cap, coral::storage::BigramHashRecord{ kGBinEmptyLabel, kGBinNoState, 0.0f });
				const uint32_t mask = cap - 1;
				for (const coral::storage::BigramArcRecord& a : arcs)
				{
					uint32_t h = a.successor_id * 0x9e3779b9u;
					for (;;)
					{
						coral::storage::BigramHashRecord& sl = hashSlots[base + (h & mask)];
						if (sl.successor_id == kGBinEmptyLabel)
						{
							sl = coral::storage::BigramHashRecord{ static_cast<int32_t>(a.successor_id), a.next, a.weight };
							break;
						}
						++h;
					}
				}
				++nHeavy;
			}
			else
			{
				std::sort(arcs.begin(), arcs.end(), [](const coral::storage::BigramArcRecord& a, const coral::storage::BigramArcRecord& b) {
					return a.successor_id < b.successor_id;
				});
				off = static_cast<uint32_t>(smallArcs.size());
				count = static_cast<uint32_t>(arcs.size());
				smallArcs.insert(smallArcs.end(), arcs.begin(), arcs.end());
			}
			states[sIdx] = coral::storage::BigramStateRecord{ off, count, 0, backoff_weight, 0.0f, flags };
			i = j;
		}

		// 6. 摊平写盘
		coral::storage::BigramFileHeader hdr{};
		std::memcpy(hdr.magic, kGBinMagic, 8);
		hdr.version = kGBinVersion;
		hdr.state_count = state_count;
		hdr.start = 0;
		hdr.small_arc_count = static_cast<uint32_t>(smallArcs.size());
		hdr.uni_size = uni_size;
		hdr.hash_slot_count = static_cast<uint32_t>(hashSlots.size());
		hdr.heavy_threshold = kGBinHeavyThreshold;
		hdr.heavy_state_count = nHeavy;

		uint32_t off = sizeof(coral::storage::BigramFileHeader);
		hdr.states_offset = off;
		off += static_cast<uint32_t>(states.size() * sizeof(coral::storage::BigramStateRecord));
		off = alignUpL(off, 64);
		hdr.arcs_offset = off;
		off += static_cast<uint32_t>(smallArcs.size() * sizeof(coral::storage::BigramArcRecord));
		off = alignUpL(off, 64);
		hdr.unigrams_offset = off;
		off += static_cast<uint32_t>(uni.size() * sizeof(coral::storage::BigramUnigramRecord));
		off = alignUpL(off, 64);
		hdr.hash_offset = off;
		off += static_cast<uint32_t>(hashSlots.size() * sizeof(coral::storage::BigramHashRecord));
		hdr.file_size = off;

		std::vector<uint8_t> buf(off, 0);
		auto wr = [&buf](const uint32_t o, const void* d, const size_t n) { std::memcpy(buf.data() + o, d, n); };
		wr(hdr.states_offset, states.data(), states.size() * sizeof(coral::storage::BigramStateRecord));
		if (!smallArcs.empty())
			wr(hdr.arcs_offset, smallArcs.data(), smallArcs.size() * sizeof(coral::storage::BigramArcRecord));
		if (!uni.empty())
			wr(hdr.unigrams_offset, uni.data(), uni.size() * sizeof(coral::storage::BigramUnigramRecord));
		if (!hashSlots.empty())
			wr(hdr.hash_offset, hashSlots.data(), hashSlots.size() * sizeof(coral::storage::BigramHashRecord));
		std::memcpy(buf.data(), &hdr, sizeof(hdr));

		std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
		{
			std::cerr << "Error: cannot write " << outPath.string() << std::endl;
			return false;
		}
		out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
		out.close();

		// bind 自校验
		coral::phonetics::BigramTable g;
		if (!g.bind_data(buf.data(), buf.size()))
		{
			std::cerr << "Error: languagemodel.coral self-verification (bindData) failed." << std::endl;
			return false;
		}
		std::cout << "languagemodel.coral built: states=" << state_count << " pairs=" << pairToProb.size()
			<< " uniSize=" << uni_size << " heavy=" << nHeavy
			<< " size=" << buf.size() << " (verified)" << std::endl;
		return true;
	}
}

bool build_resource_tables(const std::filesystem::path& lsortPath,
						  const std::filesystem::path& m2Path,
						  const std::filesystem::path& primaryOutPath,
						  const std::filesystem::path& gOutPath,
						  const std::filesystem::path& englishPath)
{
	LsortData lsort;
	if (!parseLsort(lsortPath, lsort))
		return false;
	if (!mergeEnglish(lsort, englishPath)) return false;

	// 精确匹配表：(word, fullPinyin) → index（index 分配本身按该键去重，一一对应）
	std::map<std::pair<std::string, std::string>, uint32_t> exact;
	for (const LsortEntry& e : lsort.entries)
		exact.emplace(std::make_pair(e.word, e.fullPinyin), e.index);

	if (!writePrimaryGBin(lsort, primaryOutPath))
		return false;
	if (!writeGBin(lsort, exact, m2Path, gOutPath))
		return false;
	return true;
}

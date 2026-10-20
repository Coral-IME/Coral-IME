/*
* @brief: 主词典扁平排序布局实现（加载校验 + 离线构建）
* 构建输入与 CPrimaryLexicon::buildLexicon 同一份 CSV（id,pinyin,frequency,word,fullPinyin[,backprob[,inM2]]），
* 排序语义一致（champion + m2 优先 + weight 降序），保证与 marisa 路径对拍逐位一致。
*/
#include "phonetic_types.h"
#include "lexicon_table.h"

#include <marisa.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

namespace
{
	// 与 serialization.hpp 的 PhraseInfoD 字段语义一致（仅构建期使用，避免引入引擎头依赖）
	struct BuildPhraseInfo
	{
		std::string phrase;
		std::string pinyin;
		float phrase_weight = 0.f;
		uint32_t id = 0;
		float phrase_backprob = 0.f;
		bool b_in_m2 = false;
	};
}

namespace
{
	uint32_t alignUp(uint32_t x, uint32_t a)
	{
		return (x + (a - 1u)) & ~(a - 1u);
	}
}

bool coral::phonetics::LexiconTable::bind_data(const void* base, const size_t size)
{
	reset_data();
	if (base == nullptr || size < sizeof(coral::storage::LexiconFileHeader))
		return false;

	const auto* h = reinterpret_cast<const coral::storage::LexiconFileHeader*>(base);
	if (std::memcmp(h->magic, kPrimaryBinMagic, 8) != 0)
		return false;
	if (h->version != kPrimaryBinVersion)
		return false;
	if (h->file_size != size)
		return false;

	const auto inRange = [&](const uint32_t off, const uint64_t bytes) -> bool {
		if (bytes == 0)
			return true;
		if (off > size)
			return false;
		return static_cast<uint64_t>(off) + bytes <= size;
	};
	if (!inRange(h->keys_offset, static_cast<uint64_t>(h->key_count) * sizeof(coral::storage::LexiconKeyRecord)))
		return false;
	if (!inRange(h->entries_offset, static_cast<uint64_t>(h->entry_count) * sizeof(coral::storage::LexiconEntryRecord)))
		return false;
	if (!inRange(h->pool_offset, h->pool_size))
		return false;
	// pool 尾部必须恰好贴到文件末尾（无空洞，防越界读被截断的池）
	if (static_cast<uint64_t>(h->pool_offset) + h->pool_size != size)
		return false;

	header_ = h;
	const auto* b = static_cast<const uint8_t*>(base);
	keys_ = h->key_count > 0 ? reinterpret_cast<const coral::storage::LexiconKeyRecord*>(b + h->keys_offset) : nullptr;
	entries_ = h->entry_count > 0 ? reinterpret_cast<const coral::storage::LexiconEntryRecord*>(b + h->entries_offset) : nullptr;
	pool_ = h->pool_size > 0 ? reinterpret_cast<const char*>(b + h->pool_offset) : nullptr;

	// 规范布局校验：相邻 key 字典序严格递增、entry 区间不越界、pool 内偏移不越界
	for (uint32_t i = 0; i < h->key_count; ++i)
	{
		const coral::storage::LexiconKeyRecord& r = keys_[i];
		if (static_cast<uint64_t>(r.key_offset) + r.key_length > h->pool_size)
		{
			reset_data();
			return false;
		}
		if (i > 0)
		{
			const coral::storage::LexiconKeyRecord& prev = keys_[i - 1];
			const size_t cmpLen = prev.key_length < r.key_length ? prev.key_length : r.key_length;
			const int c = std::memcmp(pool_ + prev.key_offset, pool_ + r.key_offset, cmpLen);
			// 全等前缀时短的在前；否则按 memcmp。相等 key 不允许（构建器已去重）
			const bool ordered = (c < 0) || (c == 0 && prev.key_length < r.key_length);
			if (!ordered)
			{
				reset_data();
				return false;
			}
		}
		if (static_cast<uint64_t>(r.entry_offset) + r.entry_count > h->entry_count)
		{
			reset_data();
			return false;
		}
	}
	for (uint32_t i = 0; i < h->entry_count; ++i)
	{
		const coral::storage::LexiconEntryRecord& e = entries_[i];
		if (static_cast<uint64_t>(e.word_offset) + e.word_length > h->pool_size ||
			static_cast<uint64_t>(e.pinyin_offset) + e.pinyin_length > h->pool_size)
		{
			reset_data();
			return false;
		}
	}
	return true;
}

// 从已部署的 primary.marisa + primary.dat 生成 mainlexicon.coral（供缺失时现场生成/离线工具用）。
// 键集合来自 marisa 字典序枚举，条目/字段来自 dat 外层索引，构建后逐键逐条校验。
bool import_legacy_lexicon_index(const std::filesystem::path& marisaPath,
	const std::filesystem::path& datPath,
	const std::filesystem::path& outPath)
{
	// primary.dat 布局：FileHeader | OuterIndex[] | PackedInfo[] | 字符串池
#pragma pack(push, 1)
	struct DatFileHeader { uint32_t outer_size; };
	struct DatOuterIndex { uint32_t begin; uint32_t count; };
	struct DatPackedInfo { uint32_t phrase_off; uint32_t pinyin_off; float weight; uint32_t id; float backprob; bool b_in_m2; };
#pragma pack(pop)

	marisa::Trie trie;
	try
	{
		trie.load(marisaPath.string().c_str());
	}
	catch (const marisa::Exception&)
	{
		std::cerr << "Error: cannot load marisa trie: " << marisaPath.string() << std::endl;
		return false;
	}

	std::vector<uint8_t> dat;
	{
		std::ifstream f(datPath, std::ios::binary);
		if (!f.is_open())
		{
			std::cerr << "Error: cannot open primary.dat: " << datPath.string() << std::endl;
			return false;
		}
		f.seekg(0, std::ios::end);
		const auto sz = f.tellg();
		f.seekg(0, std::ios::beg);
		dat.resize(static_cast<size_t>(sz));
		if (!dat.empty())
			f.read(reinterpret_cast<char*>(dat.data()), sz);
		if (!f && !dat.empty())
		{
			std::cerr << "Error: read primary.dat failed: " << datPath.string() << std::endl;
			return false;
		}
	}
	if (dat.size() < sizeof(DatFileHeader))
		return false;
	const auto* dh = reinterpret_cast<const DatFileHeader*>(dat.data());
	const auto* doidx = reinterpret_cast<const DatOuterIndex*>(dat.data() + sizeof(DatFileHeader));
	const auto* dinfo = reinterpret_cast<const DatPackedInfo*>(dat.data() + sizeof(DatFileHeader)
		+ static_cast<size_t>(dh->outer_size) * sizeof(DatOuterIndex));
	uint32_t maxEnd = 0;
	for (uint32_t i = 0; i < dh->outer_size; ++i)
		maxEnd = std::max(maxEnd, doidx[i].begin + doidx[i].count);
	const char* dpool = reinterpret_cast<const char*>(dinfo + maxEnd);
	const size_t dpoolSize = dat.size() - (reinterpret_cast<const uint8_t*>(dpool) - dat.data());

	// 按字典序枚举 marisa 键
	std::vector<std::string> keys;
	{
		marisa::Agent agent;
		agent.set_query("");
		while (trie.predictive_search(agent))
			keys.emplace_back(agent.key().ptr(), agent.key().length());
	}

	std::string pool;
	pool.reserve(90u << 20);
	std::vector<coral::storage::LexiconKeyRecord> krs;
	std::vector<coral::storage::LexiconEntryRecord> ents;
	krs.reserve(keys.size());
	ents.reserve(maxEnd);

	marisa::Agent lookup;
	for (const std::string& k : keys)
	{
		lookup.set_query(k.c_str(), k.size());
		if (!trie.lookup(lookup))
			return false;
		const uint32_t ri = lookup.key().id();
		if (ri >= dh->outer_size)
			return false;
		const uint32_t begin = doidx[ri].begin, count = doidx[ri].count;

		coral::storage::LexiconKeyRecord kr{};
		kr.key_prefix = primary_bin_key_prefix(k.data(), k.size());
		kr.key_offset = static_cast<uint32_t>(pool.size());
		kr.key_length = static_cast<uint32_t>(k.size());
		pool.append(k);
		pool.push_back('\0');
		kr.entry_offset = static_cast<uint32_t>(ents.size());
		kr.entry_count = count;
		for (uint32_t i = 0; i < count; ++i)
		{
			const DatPackedInfo& pi = dinfo[begin + i];
			const char* w = dpool + pi.phrase_off;
			const char* py = dpool + pi.pinyin_off;
			const uint32_t wl = static_cast<uint32_t>(std::strlen(w));
			const uint32_t pl = static_cast<uint32_t>(std::strlen(py));
			if (pi.phrase_off >= dpoolSize || pi.pinyin_off >= dpoolSize ||
				pi.phrase_off + wl >= dpoolSize || pi.pinyin_off + pl >= dpoolSize)
				return false;
			coral::storage::LexiconEntryRecord e{};
			e.word_offset = static_cast<uint32_t>(pool.size());
			e.word_length = wl;
			pool.append(w, wl);
			pool.push_back('\0');
			e.pinyin_offset = static_cast<uint32_t>(pool.size());
			e.pinyin_length = pl;
			pool.append(py, pl);
			pool.push_back('\0');
			e.phrase_id = pi.id;
			e.weight = pi.weight;
			e.backprob = pi.backprob;
			e.b_in_m2 = pi.b_in_m2 ? 1u : 0u;
			ents.push_back(e);
		}
		krs.push_back(kr);
	}

	coral::storage::LexiconFileHeader hdr{};
	std::memcpy(hdr.magic, kPrimaryBinMagic, 8);
	hdr.version = kPrimaryBinVersion;
	hdr.key_count = static_cast<uint32_t>(krs.size());
	hdr.entry_count = static_cast<uint32_t>(ents.size());
	hdr.pool_size = static_cast<uint32_t>(pool.size());
	uint32_t off = sizeof(coral::storage::LexiconFileHeader);
	hdr.keys_offset = off;
	off += static_cast<uint32_t>(krs.size() * sizeof(coral::storage::LexiconKeyRecord));
	off = alignUp(off, 64);
	hdr.entries_offset = off;
	off += static_cast<uint32_t>(ents.size() * sizeof(coral::storage::LexiconEntryRecord));
	off = alignUp(off, 64);
	hdr.pool_offset = off;
	off += hdr.pool_size;
	hdr.file_size = off;

	std::vector<uint8_t> buf(off, 0);
	auto wr = [&buf](uint32_t o, const void* d, size_t n) { std::memcpy(buf.data() + o, d, n); };
	wr(hdr.keys_offset, krs.data(), krs.size() * sizeof(coral::storage::LexiconKeyRecord));
	wr(hdr.entries_offset, ents.data(), ents.size() * sizeof(coral::storage::LexiconEntryRecord));
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

	// 内嵌全量校验：重新 bind 后逐键逐条对比 dat
	coral::phonetics::LexiconTable bin;
	if (!bin.bind_data(buf.data(), buf.size()))
		return false;
	uint64_t mism = 0;
	for (const std::string& k : keys)
	{
		lookup.set_query(k.c_str(), k.size());
		trie.lookup(lookup);
		const uint32_t ri = lookup.key().id();
		uint32_t eo = 0, ec = 0;
		if (!bin.find_key(k.data(), k.size(), eo, ec) || ec != doidx[ri].count)
		{
			++mism;
			continue;
		}
		for (uint32_t i = 0; i < ec; ++i)
		{
			const DatPackedInfo& pi = dinfo[doidx[ri].begin + i];
			const coral::storage::LexiconEntryRecord& e = bin.entries()[eo + i];
			if (std::strcmp(bin.entry_word(e).data(), dpool + pi.phrase_off) != 0 ||
				std::strcmp(bin.entry_pinyin(e).data(), dpool + pi.pinyin_off) != 0 ||
				e.phrase_id != pi.id || e.weight != pi.weight ||
				e.backprob != pi.backprob || e.b_in_m2 != (pi.b_in_m2 ? 1u : 0u))
				++mism;
		}
	}
	if (mism)
	{
		std::cerr << "Error: mainlexicon.coral verification failed: " << mism << " mismatches over " << keys.size() << " keys" << std::endl;
		return false;
	}
	std::cout << "mainlexicon.coral built from marisa+dat: keys=" << hdr.key_count
		<< " entries=" << hdr.entry_count << " size=" << buf.size() << " (verified)" << std::endl;
	return true;
}

bool build_lexicon_table_from_csv(const std::filesystem::path& rawDataPath,
							  const std::filesystem::path& outPath,
							  bool enableInM2)
{
	std::ifstream file(rawDataPath);
	if (!file.is_open())
	{
		std::cerr << "Error: primary lexicon source not found: " << rawDataPath.string() << std::endl;
		return false;
	}

	std::string line;
	if (!std::getline(file, line))
	{
		std::cerr << "Error: empty primary lexicon source." << std::endl;
		return false;
	}

	int lineNumber = 1;
	std::map<std::string, std::vector<BuildPhraseInfo>> raw_data;
	while (std::getline(file, line))
	{
		++lineNumber; // 前两行跳过（与 buildLexicon 一致）
		if (line.empty() || lineNumber <= 3)
			continue;

		std::istringstream iss(line);
		std::string token;
		std::vector<std::string> columns;
		while (std::getline(iss, token, ','))
			columns.push_back(token);
		if (columns.size() < 5)
			continue;

		try
		{
			const uint32_t id = static_cast<uint32_t>(std::stoul(columns[0]));
			const int frequency = std::stoi(columns[2]);
			if (frequency <= 0)
				continue;
			coral::phonetics::Score backprob = kMinBackprob;
			if (columns.size() >= 6)
				backprob = std::stof(columns[5]);
			bool b_in_m2 = false;
			if (columns.size() >= 7)
				b_in_m2 = std::stoi(columns[6]) != 0;
			BuildPhraseInfo phinfo{ columns[3], columns[4],
				static_cast<coral::phonetics::Score>(log(static_cast<float>(frequency) * kInvMaxcount)), id,
				backprob, b_in_m2 };
			raw_data[columns[1]].push_back(std::move(phinfo));
		}
		catch (const std::exception&)
		{
			continue;
		}
	}
	file.close();
	if (raw_data.empty())
	{
		std::cerr << "Error: no valid primary lexicon entries parsed." << std::endl;
		return false;
	}

	// 排序语义与 buildLexicon 一致：weight 降序 + champion/m2 优先
	std::vector<std::pair<std::string_view, std::vector<BuildPhraseInfo>*>> sortedKeys;
	sortedKeys.reserve(raw_data.size());
	for (auto& pair : raw_data)
	{
		auto& phrases = pair.second;
		std::sort(phrases.begin(), phrases.end(),
			[](const BuildPhraseInfo& a, const BuildPhraseInfo& b) { return a.phrase_weight > b.phrase_weight; });
		// 复用 primarylexicon 的 m2 优先排序（同类静态函数不可见，这里等价重写）
		if (phrases.size() > 1)
		{
			BuildPhraseInfo champion = std::move(phrases[0]);
			std::vector<BuildPhraseInfo> m2Items, normalItems;
			m2Items.reserve(phrases.size());
			normalItems.reserve(phrases.size());
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
	// std::map 已按 key 升序，无需再排

	// --- 摊平写盘 ---
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
		for (const BuildPhraseInfo& p : *phrasesPtr)
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
	(void)enableInM2;

	coral::storage::LexiconFileHeader hdr{};
	std::memcpy(hdr.magic, kPrimaryBinMagic, 8);
	hdr.version = kPrimaryBinVersion;
	hdr.key_count = static_cast<uint32_t>(keys.size());
	hdr.entry_count = static_cast<uint32_t>(entries.size());
	hdr.pool_size = static_cast<uint32_t>(pool.size());

	uint32_t off = sizeof(coral::storage::LexiconFileHeader);
	hdr.keys_offset = off;
	off += static_cast<uint32_t>(keys.size() * sizeof(coral::storage::LexiconKeyRecord));
	off = alignUp(off, 64);
	hdr.entries_offset = off;
	off += static_cast<uint32_t>(entries.size() * sizeof(coral::storage::LexiconEntryRecord));
	off = alignUp(off, 64);
	hdr.pool_offset = off;
	off += hdr.pool_size;
	hdr.file_size = off;

	std::vector<uint8_t> buf(off, 0);
	auto writeRegion = [&buf](const uint32_t regionOff, const void* data, const size_t bytes) {
		std::memcpy(buf.data() + regionOff, data, bytes);
	};
	writeRegion(hdr.keys_offset, keys.data(), keys.size() * sizeof(coral::storage::LexiconKeyRecord));
	writeRegion(hdr.entries_offset, entries.data(), entries.size() * sizeof(coral::storage::LexiconEntryRecord));
	writeRegion(hdr.pool_offset, pool.data(), pool.size());
	std::memcpy(buf.data(), &hdr, sizeof(hdr));

	std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
	if (!out.is_open())
	{
		std::cerr << "Error: cannot open output file: " << outPath.string() << std::endl;
		return false;
	}
	out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
	out.close();

	std::cout << "mainlexicon.coral built: keys=" << hdr.key_count << " entries=" << hdr.entry_count
		<< " pool=" << hdr.pool_size << " size=" << buf.size() << std::endl;
	return true;
}

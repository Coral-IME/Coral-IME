/*
* @brief: languagemodel.coral 二元语言模型静态图实现（加载校验 + 离线构建）
*/
#include "phonetic_types.h"
#include "bigram_table.h"
#include "latin_identity.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace
{
	uint32_t nextPow2(uint32_t n)
	{
		uint32_t cap = 8;
		while (cap < n)
		{
			cap <<= 1;
			if (cap == 0)
				return 8;
		}
		return cap;
	}

	uint32_t alignUp(uint32_t x, uint32_t a)
	{
		return (x + (a - 1u)) & ~(a - 1u);
	}
}

bool coral::phonetics::BigramTable::bind_data(const void* base, const size_t size)
{
	reset_data();
	if (base == nullptr || size < sizeof(coral::storage::BigramFileHeader))
		return false;

	const auto* h = reinterpret_cast<const coral::storage::BigramFileHeader*>(base);
	if (std::memcmp(h->magic, kGBinMagic, 8) != 0)
		return false;
	if (h->version != kGBinVersion)
		return false;
	if (h->file_size != size)
		return false;
	if (h->state_count == 0 || h->start < 0 || static_cast<uint32_t>(h->start) >= h->state_count)
		return false;

	const auto inRange = [&](const uint32_t off, const uint64_t bytes) -> bool {
		if (bytes == 0)
			return true;
		if (off > size)
			return false;
		return static_cast<uint64_t>(off) + bytes <= size;
	};
	if (!inRange(h->states_offset, static_cast<uint64_t>(h->state_count) * sizeof(coral::storage::BigramStateRecord)))
		return false;
	if (!inRange(h->arcs_offset, static_cast<uint64_t>(h->small_arc_count) * sizeof(coral::storage::BigramArcRecord)))
		return false;
	if (!inRange(h->unigrams_offset, static_cast<uint64_t>(h->uni_size) * sizeof(coral::storage::BigramUnigramRecord)))
		return false;
	if (!inRange(h->hash_offset, static_cast<uint64_t>(h->hash_slot_count) * sizeof(coral::storage::BigramHashRecord)))
		return false;

	header_ = h;
	const auto* b = static_cast<const uint8_t*>(base);
	states_ = reinterpret_cast<const coral::storage::BigramStateRecord*>(b + h->states_offset);
	arcs_ = h->small_arc_count > 0 ? reinterpret_cast<const coral::storage::BigramArcRecord*>(b + h->arcs_offset) : nullptr;
	uni_ = h->uni_size > 0 ? reinterpret_cast<const coral::storage::BigramUnigramRecord*>(b + h->unigrams_offset) : nullptr;
	hash_ = h->hash_slot_count > 0 ? reinterpret_cast<const coral::storage::BigramHashRecord*>(b + h->hash_offset) : nullptr;

	// 规范布局校验：start 态无出弧与回退；重状态哈希容量为 2 的幂
	if (states_[h->start].backoff_next != kGBinNoState || states_[h->start].count != 0)
	{
		reset_data();
		return false;
	}
	for (uint32_t s = 0; s < h->state_count; ++s)
	{
		const coral::storage::BigramStateRecord& st = states_[s];
		if (st.flags & kGBinFlagHeavy)
		{
			if (st.count == 0 || (st.count & (st.count - 1)) != 0 || hash_ == nullptr)
			{
				reset_data();
				return false;
			}
			if (static_cast<uint64_t>(st.off) + st.count > h->hash_slot_count)
			{
				reset_data();
				return false;
			}
		}
		else if (static_cast<uint64_t>(st.off) + st.count > h->small_arc_count)
		{
			reset_data();
			return false;
		}
	}
	best_weights_.assign(h->uni_size, -std::numeric_limits<float>::infinity());
	for (uint32_t id = 0; id < h->uni_size; ++id)
		if (uni_[id].next >= 0) best_weights_[id] = uni_[id].weight;
	const auto include = [&](uint32_t id, float weight) {
		if (id < h->uni_size && uni_[id].next >= 0 && std::isfinite(weight))
			best_weights_[id] = std::max(best_weights_[id], weight);
	};
	for (uint32_t i = 0; i < h->small_arc_count; ++i) include(arcs_[i].successor_id, arcs_[i].weight);
	for (uint32_t i = 0; i < h->hash_slot_count; ++i)
		if (hash_[i].successor_id >= 0) include(static_cast<uint32_t>(hash_[i].successor_id), hash_[i].weight);
	ready_.store(true, std::memory_order_release);
	return true;
}

bool coral::phonetics::BigramTable::probe_hash(const uint32_t off, const uint32_t capacity, const uint32_t successor_id,
						   int32_t& next, float& weight) const
{
	if (capacity == 0 || hash_ == nullptr)
		return false;
	const uint32_t mask = capacity - 1;
	uint32_t h = successor_id * 0x9e3779b9u;
	const coral::storage::BigramHashRecord* slots = hash_ + off;
	for (;;)
	{
		const coral::storage::BigramHashRecord& sl = slots[h & mask];
		if (sl.successor_id == kGBinEmptyLabel)
			return false;
		if (static_cast<uint32_t>(sl.successor_id) == successor_id)
		{
			if (sl.next < 0)
				return false;
			next = sl.next;
			weight = sl.weight;
			return true;
		}
		++h;
	}
}

bool coral::phonetics::BigramTable::build_from_csv(const std::filesystem::path& bigramCsvPath,
							  const std::filesystem::path& unigramCsvPath,
							  const std::filesystem::path& outPath)
{
	// --- 1. 一元概率：主词典源 CSV(id,?,frequency,word)，与 buildLexiconForCN 同源同口径 ---
	std::unordered_map<uint32_t, float> idToUniProb;
	std::unordered_set<uint32_t> englishIds;
	uint32_t maxUniId = 0;
	{
		std::ifstream file(unigramCsvPath);
		if (!file.is_open())
		{
			std::cerr << "Error: Unigram source file not found: " << unigramCsvPath.string() << std::endl;
			return false;
		}
		std::string line;
		int lineNumber = 0;
		while (std::getline(file, line))
		{
			++lineNumber;
			if (line.empty() || lineNumber <= 3) // 首行 + 前导行，与 buildLexiconForCN 一致
				continue;
			std::stringstream ss(line);
			std::string segment;
			std::vector<std::string> columns;
			while (std::getline(ss, segment, ','))
				columns.push_back(segment);
			if (columns.size() < 4)
				continue;
			try
			{
				const uint32_t id = static_cast<uint32_t>(std::stoul(columns[0]));
				if (id >= kCloudBaseOffset)
					continue;
				const int frequency = std::stoi(columns[2]);
				if (frequency <= 0)
					continue;
				const float uniProb = std::log(static_cast<float>(frequency) * kInvMaxcount);
				// 与主词典构建同一筛选阈值，保证 uni 区与词格弧覆盖一致
				const bool english = !coral::english_identity_key(columns[3]).empty();
				if (uniProb < kPredictThreshold && !english)
					continue;
				if (english) englishIds.insert(id);
				idToUniProb[id] = uniProb;
				maxUniId = std::max(maxUniId, id);
			}
			catch (const std::exception&)
			{
				continue;
			}
		}
	}
	if (idToUniProb.empty())
	{
		std::cerr << "Error: No valid unigram entries parsed." << std::endl;
		return false;
	}

	// --- 2. 二元对：last-wins，与旧 wsfa 构建器一致 ---
	std::unordered_map<uint64_t, float> pairToProb;
	pairToProb.reserve(1500000);
	{
		std::ifstream file(bigramCsvPath);
		if (!file.is_open())
		{
			std::cerr << "Error: Bigram model file not found: " << bigramCsvPath.string() << std::endl;
			return false;
		}
		std::string line;
		if (!std::getline(file, line) || !std::getline(file, line)) // 总行数行 + 表头行
		{
			std::cerr << "Error: File header missing." << std::endl;
			return false;
		}
		int lineNumber = 2;
		while (std::getline(file, line))
		{
			++lineNumber;
			std::stringstream ss(line);
			try
			{
				std::string segment;
				uint32_t prevIndex = kInvalidPhraseId;
				uint32_t nextIndex = kInvalidPhraseId;
				float prob = 0.f;
				if (std::getline(ss, segment, ','))
					prevIndex = static_cast<uint32_t>(std::stoull(segment));
				if (std::getline(ss, segment, ','))
					nextIndex = static_cast<uint32_t>(std::stoull(segment));
				if (std::getline(ss, segment, ','))
					prob = std::stof(segment);
				if (prevIndex >= kCloudBaseOffset || nextIndex >= kCloudBaseOffset)
					throw std::out_of_range("phrase id outside primary lexicon range");
				pairToProb[(static_cast<uint64_t>(prevIndex) << 32) | nextIndex] = prob;
			}
			catch (const std::exception& e)
			{
				std::cerr << "Warning: Error parsing line " << lineNumber << ": '" << line
					<< "' - " << e.what() << ". Skipping line." << std::endl;
			}
		}
	}
	if (pairToProb.empty())
	{
		std::cerr << "Error: No valid bigram pairs parsed." << std::endl;
		return false;
	}

	// --- 3. 状态分配：prev ∪ succ 各一状态，start 为 0 ---
	std::vector<uint64_t> keys;
	keys.reserve(pairToProb.size());
	for (const auto& pair : pairToProb)
		keys.push_back(pair.first);
	std::sort(keys.begin(), keys.end()); // （前词升序，后词升序）

	std::unordered_map<uint32_t, int32_t> idToState;
	for (const uint64_t key : keys)
	{
		idToState.emplace(static_cast<uint32_t>(key >> 32), 0);
		idToState.emplace(static_cast<uint32_t>(key & 0xFFFFFFFFu), 0);
	}
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

	// --- 4. uni 区：phraseId 直挂一元权重 ---
	const uint32_t uni_size = maxUniId + 1;
	std::vector<coral::storage::BigramUnigramRecord> uni(uni_size, coral::storage::BigramUnigramRecord{ kGBinNoState, 0.0f });
	for (const auto& [id, prob] : idToUniProb)
	{
		const auto it = idToState.find(id);
		uni[id] = coral::storage::BigramUnigramRecord{ it != idToState.end() ? it->second : kGBinNoState, prob };
	}

	// --- 5. 状态弧区 + 回退权重 ---
	std::vector<coral::storage::BigramStateRecord> states(state_count);
	std::vector<coral::storage::BigramArcRecord> smallArcs;
	std::vector<coral::storage::BigramHashRecord> hashSlots;
	smallArcs.reserve(1u << 20);
	hashSlots.reserve(1u << 21);
	uint32_t nHeavy = 0;

	states[0] = coral::storage::BigramStateRecord{ 0, 0, kGBinNoState, 0.0f, 0.0f, 0 }; // start

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

		// 回退权重 = log(1 - Σ P(succ|prev))，概率和溢出/越界时夹紧到下限
		constexpr double kMinBackoffMass = 1e-6;
		const double mass = std::max(1.0 - probSum, kMinBackoffMass);
		const float backoff_weight = static_cast<float>(std::log(mass));

		uint32_t flags = kGBinFlagFinal;
		uint32_t off = 0;
		uint32_t count = 0;
		if (arcs.size() > kGBinHeavyThreshold)
		{
			const uint32_t cap = nextPow2(static_cast<uint32_t>(arcs.size()) * 2);
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
		// 二元模型：回退目标恒为 start(0)
		states[sIdx] = coral::storage::BigramStateRecord{ off, count, 0, backoff_weight, 0.0f, flags };
		i = j;
	}
	// 纯英文词状态为空，因此即使没有观测到的二元组也消耗上下文。
	// 旧版仅一元的中文状态覆盖保持不变。

	// --- 6. 摊平写盘 ---
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
	off = alignUp(off, 64);
	hdr.arcs_offset = off;
	off += static_cast<uint32_t>(smallArcs.size() * sizeof(coral::storage::BigramArcRecord));
	off = alignUp(off, 64);
	hdr.unigrams_offset = off;
	off += static_cast<uint32_t>(uni.size() * sizeof(coral::storage::BigramUnigramRecord));
	off = alignUp(off, 64);
	hdr.hash_offset = off;
	off += static_cast<uint32_t>(hashSlots.size() * sizeof(coral::storage::BigramHashRecord));
	hdr.file_size = off;

	std::vector<uint8_t> buf(off, 0);
	auto writeRegion = [&buf](const uint32_t regionOff, const void* data, const size_t bytes) {
		std::memcpy(buf.data() + regionOff, data, bytes);
	};
	writeRegion(hdr.states_offset, states.data(), states.size() * sizeof(coral::storage::BigramStateRecord));
	if (!smallArcs.empty())
		writeRegion(hdr.arcs_offset, smallArcs.data(), smallArcs.size() * sizeof(coral::storage::BigramArcRecord));
	if (!uni.empty())
		writeRegion(hdr.unigrams_offset, uni.data(), uni.size() * sizeof(coral::storage::BigramUnigramRecord));
	if (!hashSlots.empty())
		writeRegion(hdr.hash_offset, hashSlots.data(), hashSlots.size() * sizeof(coral::storage::BigramHashRecord));
	std::memcpy(buf.data(), &hdr, sizeof(hdr));

	std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
	if (!out.is_open())
	{
		std::cerr << "Error: Cannot open output file: " << outPath.string() << std::endl;
		return false;
	}
	out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
	out.close();

	std::cout << "languagemodel.coral built: states=" << state_count << " pairs=" << pairToProb.size()
		<< " uniSize=" << uni_size << " heavy=" << nHeavy
		<< " size=" << buf.size() << std::endl;
	return true;
}

/*
* @brief: languagemodel.coral 二元语言模型静态图（组合解码用）
*         布局: Header(64B) | States[] | SmallArcs[] | uni[label] | HashSlots[]
*
* 参考 proclass_coralpinyin/langengine 的 gbin，按本引擎语义适配:
*  - 状态 0 为 start；每个在 bigram 中作为前词出现的 phraseId 对应一个状态；
*  - uni 区按下标 phraseId 直挂一元权重（log 概率），仅 start 态查询走 uni；
*  - 非 start 态 miss 通过 backoff 弧（backoff_next/backoff_weight）回退到 start，
*    二元模型只有一级回退，backoff_next 恒为 0；
*  - 权重语义与 wsfa/主词典一致：log 概率（负值），解码侧取 cost = -weight；
*  - 出弧数超过 heavyThreshold 的状态走开放寻址哈希，其余按 succId 排序顺序扫描。
*
* 与 wsfa 点查模式的语义差异（预期内）:
*  - 一元权重并入模型 uni 区，与主词典 data.weight 同源同值（构建器读同一份 CSV）。
*  - miss 回退：backoffWeight 因 m2 的 prob 非归一化会被夹紧到 -13.82，对 miss 词误罚过重，
*    解码侧（languagedecoder / getBigram）已改为不加 backoffWeight、直接回退后继一元，
*    与旧 wsfa miss 语义对齐；文件内 backoffWeight 字段保留但不再参与解码打分。
*/
#pragma once
#ifndef CORAL_MODULES_STORAGE_BIGRAM_TABLE_H_INCLUDED
#define CORAL_MODULES_STORAGE_BIGRAM_TABLE_H_INCLUDED

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <vector>
#include <limits>
#include <atomic>

inline constexpr char kGBinMagic[8] = { 'K', 'N', 'A', 'G', 'B', 'I', 'N', '1' };
inline constexpr uint32_t kGBinVersion = 1;
// 出弧数超过该值的状态视为重状态(heavy)，走开放寻址哈希；否则弧按 succId 排序后顺序扫描
inline constexpr uint32_t kGBinHeavyThreshold = 16;
// 哈希槽空位标记
inline constexpr int32_t kGBinEmptyLabel = -1;
// 无后继状态标记（uni 项 / 弧）
inline constexpr int32_t kGBinNoState = -1;
// header.flags / state flags 位
inline constexpr uint32_t kGBinFlagFinal = 1u;
inline constexpr uint32_t kGBinFlagHeavy = 2u;

#pragma pack(push, 4)
namespace coral::storage {
struct BigramFileHeader
{
	char magic[8];
	uint32_t version;
	uint32_t state_count;        // 状态个数（含 start）
	int32_t start;           // 恒为 0
	uint32_t small_arc_count;     // 小状态排序弧区条目数（重状态弧不在此区存储）
	uint32_t uni_size;        // uni 区条目数 = maxPhraseId + 1
	uint32_t hash_slot_count;     // 哈希区条目数
	uint32_t heavy_threshold;
	uint32_t heavy_state_count;
	uint32_t states_offset;
	uint32_t arcs_offset;
	uint32_t unigrams_offset;
	uint32_t hash_offset;
	uint32_t file_size;
	uint32_t reserved;
};
} // 命名空间 coral::storage 结束（批次 3）


namespace coral::storage {
struct BigramStateRecord
{
	uint32_t off;            // 普通状态 -> GBinArc 下标；重状态 -> GBinHashSlot 下标
	uint32_t count;          // 普通状态 -> 弧数；重状态 -> 哈希槽容量（2 的幂）
	int32_t backoff_next;     // 回退目标状态，kGBinNoState 表示不可回退
	float backoff_weight;     // 回退权重（log 概率，<= 0）
	float final_weight;       // 预留，恒 0
	uint32_t flags;
};
} // 命名空间 coral::storage 结束（批次 3）


namespace coral::storage {
struct BigramArcRecord
{
	uint32_t successor_id;         // 弧标签 = 后继词 phraseId
	int32_t next;            // 目标状态
	float weight;            // log 概率
};
} // 命名空间 coral::storage 结束（批次 3）


namespace coral::storage {
struct BigramUnigramRecord
{
	int32_t next;            // 该词对应的状态；kGBinNoState 表示模型中无该词状态
	float weight;            // 一元 log 概率
};
} // 命名空间 coral::storage 结束（批次 3）


namespace coral::storage {
struct BigramHashRecord
{
	int32_t successor_id;          // kGBinEmptyLabel 为空槽
	int32_t next;
	float weight;
};
} // 命名空间 coral::storage 结束（批次 3）

#pragma pack(pop)

static_assert(sizeof(coral::storage::BigramFileHeader) == 64, "GBinHeader must be 64 bytes");
static_assert(sizeof(coral::storage::BigramStateRecord) == 24, "GBinState must be 24 bytes");
static_assert(sizeof(coral::storage::BigramArcRecord) == 12, "GBinArc must be 12 bytes");
static_assert(sizeof(coral::storage::BigramUnigramRecord) == 8, "GBinMove must be 8 bytes");
static_assert(sizeof(coral::storage::BigramHashRecord) == 12, "GBinHashSlot must be 12 bytes");

namespace coral::phonetics {
class BigramTable
{
public:
	BigramTable() = default;
	BigramTable(const BigramTable&) = delete;
	BigramTable& operator=(const BigramTable&) = delete;

	// 只读视图绑定：校验 header、区段边界与规范布局，失败返回 false 且不暴露任何数据
	bool bind_data(const void* base, size_t size);
	void reset_data()
	{
		ready_.store(false, std::memory_order_release);
		header_ = nullptr;
		states_ = nullptr;
		arcs_ = nullptr;
		uni_ = nullptr;
		hash_ = nullptr;
		best_weights_.clear();
	}
	bool valid() const { return ready_.load(std::memory_order_acquire); }

	int32_t start() const { return header_ ? header_->start : kGBinNoState; }
	uint32_t uni_size() const { return header_ ? header_->uni_size : 0; }
	const coral::storage::BigramUnigramRecord* uni() const { return uni_; }
	float best_weight(uint32_t id) const {
		return id < best_weights_.size() ? best_weights_[id] : -std::numeric_limits<float>::infinity();
	}
	uint32_t num_states() const { return header_ ? header_->state_count : 0; }

	// start 态一元查询：label < uniSize 且模型含该词时返回 true 并带出 (next, weight)
	bool find_uni(uint32_t label, int32_t& next, float& weight) const
	{
		if (!header_ || label >= header_->uni_size || !uni_)
			return false;
		const coral::storage::BigramUnigramRecord& m = uni_[label];
		if (m.next < 0)
			return false;
		next = m.next;
		weight = m.weight;
		return true;
	}

	// 组合解码转移：状态 s 下取标签 label 的弧。命中返回 true 并带出 (next, weight)。
	// start 态走 uni 区；重状态走开放寻址哈希；小状态按 succId 排序顺序扫描。
	bool find(int32_t s, uint32_t label, int32_t& next, float& weight) const
	{
		if (!header_ || s < 0 || static_cast<uint32_t>(s) >= header_->state_count)
			return false;
		if (s == header_->start)
			return find_uni(label, next, weight);
		const coral::storage::BigramStateRecord& st = states_[s];
		if (st.flags & kGBinFlagHeavy)
			return probe_hash(st.off, st.count, label, next, weight);
		const coral::storage::BigramArcRecord* a = arcs_ + st.off;
		const coral::storage::BigramArcRecord* end = a + st.count;
		for (; a != end; ++a)
		{
			if (a->successor_id == label)
			{
				if (a->next < 0)
					return false;
				next = a->next;
				weight = a->weight;
				return true;
			}
			if (a->successor_id > label)
				break;
		}
		return false;
	}

	// ε 回退弧：s 有回退时返回 true 并带出 (next, weight)。二元模型恒回退到 start。
	bool backoff(int32_t s, int32_t& next, float& weight) const
	{
		if (!header_ || s < 0 || static_cast<uint32_t>(s) >= header_->state_count)
			return false;
		const int32_t b = states_[s].backoff_next;
		if (b < 0)
			return false;
		next = b;
		weight = states_[s].backoff_weight;
		return true;
	}

	// 构建 languagemodel.coral：bigram CSV(prevIndex,nextIndex,prob，last-wins) +
	// 主词典源 CSV(id,?,frequency,word，与 buildLexiconForCN 同格式) 提供一元概率。
	static bool build_from_csv(const std::filesystem::path& bigramCsvPath,
							 const std::filesystem::path& unigramCsvPath,
							 const std::filesystem::path& outPath);

private:
	const coral::storage::BigramFileHeader* header_ = nullptr;
	const coral::storage::BigramStateRecord* states_ = nullptr;
	const coral::storage::BigramArcRecord* arcs_ = nullptr;
	const coral::storage::BigramUnigramRecord* uni_ = nullptr;
	const coral::storage::BigramHashRecord* hash_ = nullptr;
	std::vector<float> best_weights_;
	std::atomic<bool> ready_{false};

	bool probe_hash(uint32_t off, uint32_t capacity, uint32_t successor_id,
				   int32_t& next, float& weight) const;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // GBIN_H

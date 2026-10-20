/*
* @brief: 主词典扁平排序布局（mainlexicon.coral，参考 proclass LexiconBin）
*         布局: Header(64B) | KeyRec[] | Entry[] | pool
*
* 动机：marisa trie lookup 逐字符指针追逐、cache 不友好；每个词格 span 都要查一次，
*       长句数千 span 时词典查询成为热点。扁平布局一次二分 + 顺序读替代 trie 状态机。
* 语义:
*  - key = 拼音序列以 '\'' 连接（与 marisa key 完全一致，如 "zhong'guo"），按字节序升序；
*  - Entry 顺序 = 原 dumpLexicon 前的排序结果（champion + m2 优先 + weight 降序），
*    保证 getPhrases 对拍逐位一致；
*  - pool 中字符串以 NUL 结尾，word 与全拼 pinyin 共用一个池。
*/
#pragma once
#ifndef CORAL_MODULES_STORAGE_LEXICON_TABLE_H_INCLUDED
#define CORAL_MODULES_STORAGE_LEXICON_TABLE_H_INCLUDED

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string_view>

inline constexpr char kPrimaryBinMagic[8] = { 'K', 'N', 'A', 'P', 'R', 'G', 'B', '1' };
inline constexpr uint32_t kPrimaryBinVersion = 2;

// 取 key 前 8 字节组成大端序整数（不足补零）。
// 该整数的比较顺序与 memcmp 的字典序完全一致，二分时先用它比较，
// 仅在而8字节前缀相等时才退回 memcmp 比较完整字符串，避免每步解引用 pool。
inline uint64_t primary_bin_key_prefix(const char* key, const size_t len) noexcept
{
	uint64_t p = 0;
	const size_t n = len < 8 ? len : 8;
	for (size_t i = 0; i < n; ++i)
		p |= static_cast<uint64_t>(static_cast<uint8_t>(key[i])) << (56 - 8 * i);
	return p;
}

#pragma pack(push, 4)
namespace coral::storage {
struct LexiconFileHeader
{
	char magic[8];
	uint32_t version;
	uint32_t key_count;       // 去重后的拼音 key 数
	uint32_t entry_count;    // 词项总数
	uint32_t pool_size;    // 字符串池字节数
	uint32_t keys_offset;
	uint32_t entries_offset;
	uint32_t pool_offset;
	uint32_t file_size;
	uint32_t reserved[6];
};
} // 命名空间 coral::storage 结束（批次 3）


namespace coral::storage {
struct LexiconKeyRecord
{
	uint64_t key_prefix;   // key 前 8 字节大端序整数（二分快速比较用）
	uint32_t key_offset;      // pool 内偏移
	uint32_t key_length;
	uint32_t entry_offset;    // Entry 区下标
	uint32_t entry_count;
};
} // 命名空间 coral::storage 结束（批次 3）


namespace coral::storage {
struct LexiconEntryRecord
{
	uint32_t word_offset;     // pool 内偏移
	uint32_t word_length;
	uint32_t pinyin_offset;
	uint32_t pinyin_length;
	uint32_t phrase_id;    // 主词典 id（构建列0）
	float weight;         // log(freq * INV_MAXCOUNT)（频率对数化）
	float backprob;
	uint32_t b_in_m2;       // 0/1
};
} // 命名空间 coral::storage 结束（批次 3）

#pragma pack(pop)

static_assert(sizeof(coral::storage::LexiconFileHeader) == 64, "PrimaryBinHeader must be 64 bytes");
static_assert(sizeof(coral::storage::LexiconKeyRecord) == 24, "PrimaryBinKeyRec must be 24 bytes");
static_assert(sizeof(coral::storage::LexiconEntryRecord) == 32, "PrimaryBinEntry must be 32 bytes");

namespace coral::phonetics {
class LexiconTable
{
public:
	LexiconTable() = default;
	LexiconTable(const LexiconTable&) = delete;
	LexiconTable& operator=(const LexiconTable&) = delete;

	// 只读视图绑定：校验 header 与区段边界，失败返回 false 且不暴露任何数据
	bool bind_data(const void* base, size_t size);
	void reset_data()
	{
		header_ = nullptr;
		keys_ = nullptr;
		entries_ = nullptr;
		pool_ = nullptr;
	}
	bool valid() const { return header_ != nullptr; }

	uint32_t key_count() const { return header_ ? header_->key_count : 0; }
	uint32_t entry_count() const { return header_ ? header_->entry_count : 0; }

	// 二分查找拼音 key（与 marisa lookup 等价：完整 key 匹配）
	// 命中返回 true 并带出 entries 区间
	bool find_key(const char* key, size_t key_length, uint32_t& entry_offset, uint32_t& entry_count) const
	{
		if (!header_)
			return false;
		const uint64_t qp = primary_bin_key_prefix(key, key_length);
		uint32_t lo = 0, hi = header_->key_count;
		while (lo < hi)
		{
			const uint32_t mid = lo + (hi - lo) / 2;
			const coral::storage::LexiconKeyRecord& r = keys_[mid];
			int c = qp < r.key_prefix ? -1 : (qp > r.key_prefix ? 1 : 0);
			if (c == 0)
			{
				const size_t cmpLen = key_length < r.key_length ? key_length : r.key_length;
				c = std::memcmp(key, pool_ + r.key_offset, cmpLen);
				if (c == 0)
				c = static_cast<int>(key_length) - static_cast<int>(r.key_length);
			}
			if (c < 0)
				hi = mid;
			else if (c > 0)
				lo = mid + 1;
			else
			{
				entry_offset = r.entry_offset;
				entry_count = r.entry_count;
				return true;
			}
		}
		return false;
	}

	// 前缀扫描起点（给 predictiveSearch 用）：返回第一个 >= prefix 的 key 下标
	uint32_t lower_bound(const char* prefix, size_t prefixLen) const
	{
		if (!header_)
			return 0;
		const uint64_t qp = primary_bin_key_prefix(prefix, prefixLen);
		uint32_t lo = 0, hi = header_->key_count;
		while (lo < hi)
		{
			const uint32_t mid = lo + (hi - lo) / 2;
			const coral::storage::LexiconKeyRecord& r = keys_[mid];
			// key < prefix 时才右移：前缀整数严格更小可直接判定；相等时退回完整比较
			bool keyLess;
			if (r.key_prefix < qp)
				keyLess = true;
			else if (r.key_prefix > qp)
				keyLess = false;
			else
			{
				const size_t cmpLen = prefixLen < r.key_length ? prefixLen : r.key_length;
				const int c = std::memcmp(pool_ + r.key_offset, prefix, cmpLen);
				keyLess = c < 0 || (c == 0 && r.key_length < prefixLen);
			}
			if (keyLess)
				lo = mid + 1;
			else
				hi = mid;
		}
		return lo;
	}

	// key 是否以 prefix 开头（前缀连续区段内使用）
	bool key_has_prefix(uint32_t keyIdx, const char* prefix, size_t prefixLen) const
	{
		if (!header_ || keyIdx >= header_->key_count)
			return false;
		const coral::storage::LexiconKeyRecord& r = keys_[keyIdx];
		if (r.key_length < prefixLen)
			return false;
		return std::memcmp(pool_ + r.key_offset, prefix, prefixLen) == 0;
	}

	// 取指定 key 下标的 entries 区间（前缀扫描用）
	bool key_entries(uint32_t keyIdx, uint32_t& entry_offset, uint32_t& entry_count) const
	{
		if (!header_ || keyIdx >= header_->key_count)
			return false;
		const coral::storage::LexiconKeyRecord& r = keys_[keyIdx];
		entry_offset = r.entry_offset;
		entry_count = r.entry_count;
		return true;
	}

	// 取 key 原文（用于 predictiveSearch 跳过自身 key 的比较）
	std::string_view key_at(uint32_t keyIdx) const
	{
		if (!header_ || keyIdx >= header_->key_count)
			return {};
		const coral::storage::LexiconKeyRecord& r = keys_[keyIdx];
		return std::string_view(pool_ + r.key_offset, r.key_length);
	}

	// 访问 entry 字段
	std::string_view entry_word(const coral::storage::LexiconEntryRecord& e) const
	{ return std::string_view(pool_ + e.word_offset, e.word_length); }
	std::string_view entry_pinyin(const coral::storage::LexiconEntryRecord& e) const
	{ return std::string_view(pool_ + e.pinyin_offset, e.pinyin_length); }
	const coral::storage::LexiconEntryRecord* entries() const { return entries_; }

private:
	const coral::storage::LexiconFileHeader* header_ = nullptr;
	const coral::storage::LexiconKeyRecord* keys_ = nullptr;
	const coral::storage::LexiconEntryRecord* entries_ = nullptr;
	const char* pool_ = nullptr;
};
} // 命名空间 coral::phonetics 结束（批次 2）


// 从主词典源 CSV 构建 mainlexicon.coral（与 CPrimaryLexicon::buildLexicon 同输入同排序语义）
bool build_lexicon_table_from_csv(const std::filesystem::path& rawDataPath,
							  const std::filesystem::path& outPath,
							  bool enableInM2);

// 从已部署的 primary.marisa + primary.dat 生成 mainlexicon.coral，构建后全量校验。
// 供 load() 缺失时现场生成与离线工具（tool/coral_lexicon_builder）使用。
bool import_legacy_lexicon_index(const std::filesystem::path& marisaPath,
	const std::filesystem::path& datPath,
	const std::filesystem::path& outPath);

// 直接从 coral_lexicon.csv + coral_languagemodel.csv 生成 mainlexicon.coral 与 languagemodel.coral（跳过 index/converted/marisa 中间产物）。
// index 分配与 script/coral/index.py 一致；m2 词解析与 script/coral/m2_hanzi_to_index.py 一致。
// 实现见 lsort_pbin.cpp，供 tool/coral_lexicon_builder 使用。
bool build_resource_tables(const std::filesystem::path& lsortPath,
	const std::filesystem::path& m2Path,
	const std::filesystem::path& primaryOutPath,
	const std::filesystem::path& gOutPath,
	const std::filesystem::path& englishPath = {});

#endif // PRIMARYBIN_H

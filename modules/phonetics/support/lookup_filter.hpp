#pragma once
#ifndef CORAL_MODULES_PHONETICS_SUPPORT_LOOKUP_FILTER_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_SUPPORT_LOOKUP_FILTER_HPP_INCLUDED

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include <marisa.h>

namespace coral::phonetics::support
{
	// 词典键的存在性预过滤器（分块布隆过滤器）。
	//
	// 逐 span 取词时，绝大多数查询键在词典里根本不存在：实测主词典 599 万次查询里 401 万次落空，
	// 细胞词库 599 万次只命中 1 次，英文词典 376 万次只命中 4288 次。marisa 是 LOUDS trie，
	// 一次失败的 lookup 也要沿着 key 逐字符做 rank/select，成本和成功查询相当。
	//
	// 过滤器只有假阳性、没有假阴性：说「不在」就一定不在，可以完全跳过 trie；说「可能在」时
	// 照常走原来的 lookup。因此候选结果与不加过滤器时逐字节一致。
	//
	// 布局上每个键的所有位都落在同一个 64 字节块内，一次查询只碰一条 cache line。
	class KeyFilter
	{
	public:
		[[nodiscard]] bool ready() const noexcept { return !blocks_.empty(); }

		// 用 trie 里的全部键建表。marisa 的 predictive_search 从空前缀出发就是一次 DFS 全量枚举，
		// 比按 id 逐个 reverse_lookup 快得多。
		void build(const marisa::Trie& trie)
		{
			blocks_.clear();
			const size_t key_count = trie.num_keys();
			if (key_count == 0)
				return;

			// 每键 8 bit：66 万键约 1 MB，尽量留在 L2 里。每键置 2 位时假阳性约 5%，
			// 假阳性只是白走一次原来的 trie 查询，不影响结果，所以宁可要小表。
			size_t blockCount = 1;
			while (blockCount * kBitsPerBlock < key_count * kBitsPerKey)
				blockCount <<= 1;
			blocks_.assign(blockCount * kWordsPerBlock, 0);
			block_mask_ = blockCount - 1;

			marisa::Agent agent;
			agent.set_query("");
			while (trie.predictive_search(agent))
				add(std::string_view(agent.key().ptr(), agent.key().length()));
		}

		void clear() noexcept
		{
			blocks_.clear();
			block_mask_ = 0;
		}

		// 同一个查询键要过主词典、细胞词库、每本云词典的过滤器，哈希只算一次，
		// 各过滤器共用；这就是 KeyProbe 存在的理由
		[[nodiscard]] __forceinline static uint64_t hash_key(const std::string_view key) noexcept
		{
			return hash(key);
		}

		[[nodiscard]] __forceinline bool may_contain(const std::string_view key) const noexcept
		{
			return blocks_.empty() || may_contain_hash(hash(key));
		}

		[[nodiscard]] __forceinline bool may_contain_hash(const uint64_t h) const noexcept
		{
			if (blocks_.empty())
				return true;	// 没建表就退化成"全部放行"，行为等同于不加过滤
			const uint64_t* block = &blocks_[(h >> 40 & block_mask_) * kWordsPerBlock];
			for (int i = 0; i < kBitsPerKeyInBlock; ++i)
			{
				const uint32_t bit = static_cast<uint32_t>(h >> (i * 9)) & (kBitsPerBlock - 1);
				if ((block[bit >> 6] & (uint64_t{ 1 } << (bit & 63))) == 0)
					return false;
			}
			return true;
		}

	private:
		static constexpr uint32_t kBitsPerBlock = 512;	// 64 字节 = 一条 cache line
		static constexpr uint32_t kWordsPerBlock = kBitsPerBlock / 64;
		static constexpr uint32_t kBitsPerKey = 8;
		static constexpr int kBitsPerKeyInBlock = 2;

		__forceinline static uint64_t mix(uint64_t x) noexcept
		{
			x ^= x >> 30;
			x *= 0xbf58476d1ce4e5b9ULL;
			x ^= x >> 27;
			x *= 0x94d049bb133111ebULL;
			x ^= x >> 31;
			return x;
		}

		// 按 8 字节一步吃完整个键：查询键是拼音串，最长也就二三十字节，
		// 逐字节的 FNV 在这里会形成一条几十级的乘法依赖链，比整个查表还贵
		__forceinline static uint64_t hash(const std::string_view key) noexcept
		{
			const char* p = key.data();
			size_t remain = key.size();
			uint64_t h = 0x9e3779b97f4a7c15ULL ^ (static_cast<uint64_t>(remain) * 0xff51afd7ed558ccdULL);
			while (remain >= sizeof(uint64_t))
			{
				uint64_t chunk = 0;
				std::memcpy(&chunk, p, sizeof(chunk));
				h = mix(h ^ chunk);
				p += sizeof(uint64_t);
				remain -= sizeof(uint64_t);
			}
			if (remain != 0)
			{
				uint64_t tail = 0;
				std::memcpy(&tail, p, remain);	// 只读剩余字节，不会越过 key 末尾
				h = mix(h ^ tail);
			}
			return mix(h);
		}

		void add(const std::string_view key) noexcept
		{
			const uint64_t h = hash(key);
			uint64_t* block = &blocks_[(h >> 40 & block_mask_) * kWordsPerBlock];
			for (int i = 0; i < kBitsPerKeyInBlock; ++i)
			{
				const uint32_t bit = static_cast<uint32_t>(h >> (i * 9)) & (kBitsPerBlock - 1);
				block[bit >> 6] |= uint64_t{ 1 } << (bit & 63);
			}
		}

		std::vector<uint64_t> blocks_;
		uint64_t block_mask_ = 0;
	};

	// 一个查询键 + 它的过滤器哈希。同一次 span 查询会把它传给主词典、细胞词库和各本云词典。
	struct KeyProbe
	{
		std::string_view key;
		uint64_t hash;

		explicit KeyProbe(const std::string_view k) noexcept : key(k), hash(KeyFilter::hash_key(k)) {}

		operator std::string_view() const noexcept { return key; }
	};
}

#endif // KEYFILTER_HPP（按键过滤头文件）

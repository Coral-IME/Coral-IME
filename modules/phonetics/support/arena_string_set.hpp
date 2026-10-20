#pragma once
#ifndef CORAL_MODULES_PHONETICS_SUPPORT_ARENA_STRING_SET_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_SUPPORT_ARENA_STRING_SET_HPP_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "parse_arena.hpp"

namespace coral::phonetics::support {

	// 解析期的字符串去重集合。
	//
	// 相比 std::unordered_set<std::string, PoolAllocator<std::string>> 去掉了两处开销：
	// 一是节点式容器每次 insert 的桶节点分配，这里换成一次性的开放寻址槽位数组；
	// 二是 std::string 的堆分配，键的字节按块抄进 arena。
	//
	// 键的副本随 ParseArenaScope 一并失效，所以这个集合不能跨解析存活。插入是按值拷贝，
	// 因此调用方传进来的临时字符串可以立即销毁。
	//
	// 与 unordered_set 的两点行为差异：迭代顺序不保证，且 insert/erase 会失效已有迭代器。
	//
	// 与 IndexedStringSet 的分工：那个用在"字符串本来就存在宿主容器里"的场合，只记下标不存键；
	// 这个用在没有宿主容器、集合自己需要持有键副本的场合，并且支持按键删除。
	class ArenaStringSet {
	public:
		using key_type = std::string_view;
		using value_type = std::string_view;
		using const_iterator = const std::string_view*;
		using iterator = const_iterator;

		ArenaStringSet() noexcept = default;
		explicit ArenaStringSet(size_t expectedCount) { reserve(expectedCount); }
		~ArenaStringSet() { release_blocks(); }

		ArenaStringSet(const ArenaStringSet&) = delete;
		ArenaStringSet& operator=(const ArenaStringSet&) = delete;

		[[nodiscard]] const_iterator begin() const noexcept { return entries_.data(); }
		[[nodiscard]] const_iterator end() const noexcept { return entries_.data() + entries_.size(); }
		[[nodiscard]] size_t size() const noexcept { return entries_.size(); }
		[[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

		// 键字节留在池里，clear 之后的插入接着往后写
		void clear() noexcept
		{
			entries_.clear();
			std::fill(slots_.begin(), slots_.end(), kEmpty);
			occupied_ = 0;
		}

		void reserve(size_t expectedCount)
		{
			entries_.reserve(expectedCount);
			const size_t need = slots_for(std::max(expectedCount, entries_.size()));
			if (need > slots_.size())
				rehash(need);
		}

		[[nodiscard]] size_t count(std::string_view key) const noexcept
		{
			return lookup(key) == kNotFound ? 0u : 1u;
		}

		[[nodiscard]] const_iterator find(std::string_view key) const noexcept
		{
			const size_t index = lookup(key);
			return index == kNotFound ? end() : entries_.data() + index;
		}

		std::pair<const_iterator, bool> insert(std::string_view key)
		{
			if ((occupied_ + 1) * 4 > slots_.size() * 3)
				rehash(slots_for(entries_.size() + 1));

			const size_t mask = slots_.size() - 1;
			size_t probe = hash_of(key) & mask;
			size_t firstTombstone = kNotFound;
			for (;;)
			{
				const int32_t slot = slots_[probe];
				if (slot == kEmpty)
					break;
				if (slot == kDeleted)
				{
					if (firstTombstone == kNotFound)
						firstTombstone = probe;
				}
				else if (entries_[static_cast<size_t>(slot)] == key)
				{
					return { entries_.data() + slot, false };
				}
				probe = (probe + 1) & mask;
			}

			entries_.push_back(copy_key(key));
			const auto index = static_cast<int32_t>(entries_.size() - 1);
			if (firstTombstone != kNotFound)
			{
				slots_[firstTombstone] = index;	// 复用墓碑，占用数不变
			}
			else
			{
				slots_[probe] = index;
				++occupied_;
			}
			return { entries_.data() + index, true };
		}

		std::pair<const_iterator, bool> emplace(std::string_view key) { return insert(key); }

		size_t erase(std::string_view key)
		{
			const size_t index = lookup(key);
			if (index == kNotFound)
				return 0;

			slots_[slot_of(entries_[index], index)] = kDeleted;

			// 用末位条目填补空出来的位置，并把指向末位的槽改指过来，保持条目数组紧凑
			const size_t last = entries_.size() - 1;
			if (index != last)
			{
				const size_t movedSlot = slot_of(entries_[last], last);
				entries_[index] = entries_[last];
				slots_[movedSlot] = static_cast<int32_t>(index);
			}
			entries_.pop_back();
			return 1;
		}

	private:
		static constexpr int32_t kEmpty = -1;
		static constexpr int32_t kDeleted = -2;
		static constexpr size_t kNotFound = static_cast<size_t>(-1);
		static constexpr size_t kMinSlots = 16;
		static constexpr size_t kBlockBytes = 4096;

		struct Block {
			char* data = nullptr;
			size_t capacity = 0;
			size_t used = 0;
		};

		static size_t hash_of(std::string_view key) noexcept
		{
			return std::hash<std::string_view>{}(key);
		}

		// 装载因子压在 1/2，配合 3/4 的扩容阈值让填充过程不再触发 rehash
		static size_t slots_for(size_t entry_count) noexcept
		{
			// 变量名不能用 slots：Qt 的 qobjectdefs.h 会 #define slots 为空，被 coralpinyin 等含 Qt 的工程引用时会被宏展开掉
			size_t slotNum = kMinSlots;
			while (slotNum < (entry_count + 1) * 2)
				slotNum <<= 1;
			return slotNum;
		}

		[[nodiscard]] size_t lookup(std::string_view key) const noexcept
		{
			if (slots_.empty())
				return kNotFound;

			const size_t mask = slots_.size() - 1;
			size_t probe = hash_of(key) & mask;
			for (;;)
			{
				const int32_t slot = slots_[probe];
				if (slot == kEmpty)
					return kNotFound;
				if (slot >= 0 && entries_[static_cast<size_t>(slot)] == key)
					return static_cast<size_t>(slot);
				probe = (probe + 1) & mask;
			}
		}

		// 定位存放 index 号条目的槽位，要求该条目此刻仍在表内
		[[nodiscard]] size_t slot_of(std::string_view key, size_t index) const noexcept
		{
			const size_t mask = slots_.size() - 1;
			size_t probe = hash_of(key) & mask;
			while (slots_[probe] != static_cast<int32_t>(index))
				probe = (probe + 1) & mask;
			return probe;
		}

		void rehash(size_t slotCount)
		{
			slots_.assign(slotCount, kEmpty);
			const size_t mask = slotCount - 1;
			for (size_t index = 0; index < entries_.size(); ++index)
			{
				size_t probe = hash_of(entries_[index]) & mask;
				while (slots_[probe] != kEmpty)
					probe = (probe + 1) & mask;
				slots_[probe] = static_cast<int32_t>(index);
			}
			occupied_ = entries_.size();
		}

		std::string_view copy_key(std::string_view key)
		{
			if (key.empty())
				return std::string_view{};

			char* dst = nullptr;
			if (key.size() * 4 >= kBlockBytes)
			{
				// 超长键单独占一块，避免把共享块切得七零八落
				dst = static_cast<char*>(ParseArena::instance().allocate(key.size(), 1));
				blocks_.push_back(Block{ dst, key.size(), key.size() });
			}
			else
			{
				if (blocks_.empty() || blocks_.back().used + key.size() > blocks_.back().capacity)
				{
					auto* base = static_cast<char*>(ParseArena::instance().allocate(kBlockBytes, 1));
					blocks_.push_back(Block{ base, kBlockBytes, 0 });
				}
				Block& block = blocks_.back();
				dst = block.data + block.used;
				block.used += key.size();
			}
			std::memcpy(dst, key.data(), key.size());
			return std::string_view{ dst, key.size() };
		}

		// 逆序归还，让栈顶那几块真正被 arena 回收
		void release_blocks() noexcept
		{
			ParseArena& arena = ParseArena::instance();
			for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it)
				arena.deallocate(it->data, it->capacity);
			blocks_.clear();
		}

		std::vector<std::string_view, PoolAllocator<std::string_view>> entries_;
		std::vector<int32_t, PoolAllocator<int32_t>> slots_;
		std::vector<Block, PoolAllocator<Block>> blocks_;
		size_t occupied_ = 0;	// 已用槽位，含墓碑
	};

} // 命名空间 utils 结束

#endif // ARENASTRINGSET_HPP（arena 字符串集头文件）

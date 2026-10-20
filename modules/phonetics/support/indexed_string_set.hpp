#pragma once
#ifndef CORAL_MODULES_PHONETICS_SUPPORT_INDEXED_STRING_SET_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_SUPPORT_INDEXED_STRING_SET_HPP_INCLUDED

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

#include "parse_arena.hpp"

namespace coral::phonetics::support {

	// 索引式开放寻址字符串去重表。
	//
	// 槽位里只存宿主容器的下标，比较时通过 keyAt(index) 回宿主容器取字符串。相比
	// unordered_set<string> / unordered_map<string, T>：
	//   - 整张表是 arena 上的一块连续内存，没有"每元素一次"的节点分配；
	//   - 不复制 key；
	//   - 存下标而不是 string_view，所以宿主容器扩容搬迁元素之后依然有效
	//     （中文候选词都在 SSO 范围内，元素一移动 string_view 就会悬垂）。
	//
	// 用法分两步：probe() 拿到探测结果，宿主容器放好元素之后再 commit()。
	// 分两步是为了在确认它确实是新元素之后，才去构造代价较高的宿主元素。
	template <typename KeyAt>
	class IndexedStringSet {
	public:
		static constexpr int32_t kNotFound = -1;

		struct Probe {
			int32_t existing = kNotFound;	// 命中时是已有元素的下标
			uint32_t slot = 0;				// 未命中时是可写入的空槽位
		};

		IndexedStringSet(size_t expected, KeyAt key_at)
			: key_at_(std::move(key_at))
		{
			rehash(capacity_for(expected));
		}

		~IndexedStringSet() { release(); }

		IndexedStringSet(const IndexedStringSet&) = delete;
		IndexedStringSet& operator=(const IndexedStringSet&) = delete;

		[[nodiscard]] Probe probe(std::string_view key)
		{
			// 先保证有余量。若留到 commit 时才扩容，probe 记下的 slot 就失效了
			if ((size_ + 1) * 2 > capacity_)
				rehash(capacity_ * 2);

			uint32_t slot = slot_of(key);
			while (slots_[slot] != kNotFound) {
				if (key_at_(slots_[slot]) == key)
					return Probe{ slots_[slot], slot };
				slot = (slot + 1) & mask_;
			}
			return Probe{ kNotFound, slot };
		}

		// 调用方必须保证此刻 keyAt(index) 能取到与 probe() 时一致的字符串
		void commit(const Probe& p, int32_t index)
		{
			slots_[p.slot] = index;
			++size_;
		}

		[[nodiscard]] size_t size() const noexcept { return size_; }

	private:
		static size_t capacity_for(size_t expected)
		{
			size_t capacity = 16;
			while (capacity < (expected + 1) * 2)
				capacity <<= 1;
			return capacity;
		}

		[[nodiscard]] uint32_t slot_of(std::string_view key) const noexcept
		{
			return static_cast<uint32_t>(std::hash<std::string_view>{}(key)) & mask_;
		}

		void rehash(size_t newCapacity)
		{
			int32_t* oldSlots = slots_;
			const size_t oldCapacity = capacity_;

			auto& arena = ParseArena::instance();
			slots_ = static_cast<int32_t*>(
				arena.allocate(newCapacity * sizeof(int32_t), alignof(int32_t)));
			capacity_ = newCapacity;
			mask_ = static_cast<uint32_t>(newCapacity - 1);
			std::fill_n(slots_, newCapacity, kNotFound);

			if (!oldSlots)
				return;

			for (size_t i = 0; i < oldCapacity; ++i) {
				const int32_t index = oldSlots[i];
				if (index == kNotFound)
					continue;
				uint32_t slot = slot_of(key_at_(index));
				while (slots_[slot] != kNotFound)
					slot = (slot + 1) & mask_;
				slots_[slot] = index;
			}
			arena.deallocate(oldSlots, oldCapacity * sizeof(int32_t));
		}

		void release() noexcept
		{
			if (!slots_)
				return;
			ParseArena::instance().deallocate(slots_, capacity_ * sizeof(int32_t));
			slots_ = nullptr;
			capacity_ = 0;
			mask_ = 0;
			size_ = 0;
		}

		KeyAt key_at_;
		int32_t* slots_ = nullptr;
		size_t capacity_ = 0;
		uint32_t mask_ = 0;
		size_t size_ = 0;
	};

	template <typename KeyAt>
	IndexedStringSet<KeyAt> make_indexed_string_set(size_t expected, KeyAt key_at)
	{
		return IndexedStringSet<KeyAt>(expected, std::move(key_at));
	}

} // 命名空间 utils 结束

#endif // INDEXEDSTRINGSET_HPP（索引字符串集头文件）

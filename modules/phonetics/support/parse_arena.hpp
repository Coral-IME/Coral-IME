#pragma once
#ifndef CORAL_MODULES_PHONETICS_SUPPORT_PARSE_ARENA_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_SUPPORT_PARSE_ARENA_HPP_INCLUDED

#include <atomic>
#include <cstddef>
#include <vector>

namespace coral::phonetics::support {

	// 一次解析范围内的线性分配器（arena）。
	//
	// 生命周期约束：这里分配到的内存在 ParseArenaScope 析构时整体失效。跨解析存活的对象
	// （例如 CPinyinModel::m_sentsCache 里的 Solution）不得持有 arena 内存，否则会读到被
	// 后续解析覆写的数据。Debug 下 reset() 会断言此刻没有未归还的分配来兜住这条约束。
	//
	// 非线程安全，与改造前的按类型内存池保持同样的假设：一次解析在单线程内完成。
	class ParseArena {
	public:
		static constexpr size_t kDefaultChunkBytes = size_t{ 1 } << 20;	// 首段 1MB，够覆盖正常输入的一次解析
		static constexpr size_t kMaxChunkBytes = size_t{ 8 } << 20;		// 单段上限，避免异常输入把内存撑爆

		// 每次 allocate/deallocate 都要先取实例，而跨编译单元的函数调用无法内联，
		// 单次解析里这个调用会发生几十万次（VTune 里 instance 自身就占 1.5% CPU）。
		// 把实例指针提到类内 inline 变量后，常规路径只剩一次原子读 + 一次分支。
		static ParseArena& instance() noexcept
		{
			ParseArena* arena = s_instance.load(std::memory_order_acquire);
			if (arena == nullptr)
				arena = create_instance();
			return *arena;
		}

		[[nodiscard]] void* allocate(size_t bytes, size_t alignment);

		// 只有恰好位于栈顶的分配会被真正回收，其余为空操作。
		// 这已经能覆盖"临时容器建完即销毁"的绝大多数场景。
		void deallocate(void* p, size_t bytes) noexcept;

		// 回收全部分配。若上次解析发生过扩段，会把多段合并成一段更大的首段，
		// 使后续解析不再重复扩段。
		void reset() noexcept;

		void shutdown() noexcept;

		[[nodiscard]] size_t live_bytes() const noexcept;
		[[nodiscard]] size_t capacity_bytes() const noexcept { return capacity_bytes_; }
		[[nodiscard]] size_t peak_bytes() const noexcept { return peak_bytes_; }
		[[nodiscard]] size_t grow_count() const noexcept { return grow_count_; }
		[[nodiscard]] size_t chunk_count() const noexcept { return chunks_.size(); }
		[[nodiscard]] size_t live_allocations() const noexcept { return live_allocations_; }

	private:
		ParseArena() = default;
		~ParseArena();
		ParseArena(const ParseArena&) = delete;
		ParseArena& operator=(const ParseArena&) = delete;

		static ParseArena* create_instance() noexcept;

		inline static std::atomic<ParseArena*> s_instance{ nullptr };

		struct Chunk {
			std::byte* base = nullptr;
			size_t capacity = 0;
			size_t used = 0;
		};

		void add_chunk(size_t bytes);
		void release_chunks() noexcept;

		// 用默认分配器：arena 不能拿自己管理自己的元数据
		std::vector<Chunk> chunks_;
		size_t capacity_bytes_ = 0;
		size_t peak_bytes_ = 0;
		size_t grow_count_ = 0;
		size_t live_allocations_ = 0;
	};

	// 声明在所有使用 arena 的对象之前，析构时统一回收本次解析的临时内存。
	// 因为局部对象按声明逆序析构，声明在最前面才能保证它晚于所有使用者销毁。
	class ParseArenaScope {
	public:
		ParseArenaScope() noexcept = default;
		~ParseArenaScope() { ParseArena::instance().reset(); }

		ParseArenaScope(const ParseArenaScope&) = delete;
		ParseArenaScope& operator=(const ParseArenaScope&) = delete;
	};

	// STL 兼容分配器，是 ParseArena 的类型化外壳。
	// 所有类型共享同一块 arena，容量按需增长，不再存在"池满了静默退回 malloc"的情况。
	template<typename T>
	struct PoolAllocator {
		using value_type = T;
		using pointer = T*;
		using size_type = std::size_t;

		template<typename U> struct rebind { using other = PoolAllocator<U>; };

		PoolAllocator() noexcept = default;
		template<typename U> PoolAllocator(const PoolAllocator<U>&) noexcept {}

		[[nodiscard]] static pointer allocate(size_type n) {
			return static_cast<pointer>(ParseArena::instance().allocate(n * sizeof(T), alignof(T)));
		}

		static void deallocate(pointer p, size_type n) noexcept {
			ParseArena::instance().deallocate(p, n * sizeof(T));
		}

		template<typename U> bool operator==(const PoolAllocator<U>&) const noexcept { return true; }
		template<typename U> bool operator!=(const PoolAllocator<U>&) const noexcept { return false; }
	};

} // 命名空间 utils 结束

#endif // LINEARMEMORYPOOL_HPP（线性内存池头文件）

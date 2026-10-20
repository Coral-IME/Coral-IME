#include "pch.h"
#include "parse_arena.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <new>

#ifdef _MSC_VER
#include <malloc.h>
#endif

namespace coral::phonetics::support {

namespace {

	constexpr size_t kChunkAlignment = alignof(std::max_align_t);

	void* platformAlignedAlloc(size_t size, size_t alignment)
	{
#if defined(_WIN32) || defined(_MSC_VER)
		return _aligned_malloc(size, alignment);
#elif defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
		void* ptr = nullptr;
		if (alignment < sizeof(void*))
			alignment = sizeof(void*);
		if (posix_memalign(&ptr, alignment, size) != 0)
			return nullptr;
		return ptr;
#else
		return std::aligned_alloc(alignment, size);
#endif
	}

	void platformAlignedFree(void* ptr) noexcept
	{
#ifdef _MSC_VER
		_aligned_free(ptr);
#else
		std::free(ptr);
#endif
	}

} // namespace

ParseArena* ParseArena::create_instance() noexcept
{
	// 故意不析构：静态对象的销毁顺序无法保证，进程退出阶段仍可能有容器调用 deallocate。
	// arena 的内存由操作系统在进程结束时回收。
	// 首次取实例可能发生在任意线程（词典加载等），因此这里保留线程安全的一次性初始化；
	// 之后的调用只是 instance() 里的一次原子读。
	static ParseArena* s_arena = new ParseArena();
	s_instance.store(s_arena, std::memory_order_release);
	return s_arena;
}

ParseArena::~ParseArena()
{
	release_chunks();
}

void ParseArena::add_chunk(size_t bytes)
{
	chunks_.reserve(chunks_.size() + 1);	// 先扩容，避免 push_back 抛异常时漏掉刚分配的段
	void* raw = platformAlignedAlloc(bytes, kChunkAlignment);
	if (!raw)
		throw std::bad_alloc();
	chunks_.push_back(Chunk{ static_cast<std::byte*>(raw), bytes, 0 });
	capacity_bytes_ += bytes;
}

void ParseArena::release_chunks() noexcept
{
	for (auto& chunk : chunks_)
		platformAlignedFree(chunk.base);
	chunks_.clear();
	capacity_bytes_ = 0;
}

void* ParseArena::allocate(size_t bytes, size_t alignment)
{
	if (bytes == 0)
		bytes = 1;	// 保证每次分配返回互不相同的地址

	if (chunks_.empty())
		add_chunk(kDefaultChunkBytes);

	for (int attempt = 0; attempt < 2; ++attempt)
	{
		Chunk& chunk = chunks_.back();
		const auto base = reinterpret_cast<uintptr_t>(chunk.base);
		const uintptr_t aligned = (base + chunk.used + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
		const size_t offset = static_cast<size_t>(aligned - base);
		if (offset + bytes <= chunk.capacity)
		{
			chunk.used = offset + bytes;
			++live_allocations_;
			peak_bytes_ = std::max(peak_bytes_, live_bytes());
			return chunk.base + offset;
		}

		if (attempt != 0)
			break;

		// 当前段放不下：开新段。段大小翻倍并保证能装下这次分配
		size_t next = std::min(chunk.capacity * 2, kMaxChunkBytes);
		next = std::max(next, bytes + alignment);
		add_chunk(next);
		++grow_count_;
	}

	assert(false && "ParseArena: 新开的段仍然装不下这次分配");
	throw std::bad_alloc();
}

void ParseArena::deallocate(void* p, size_t bytes) noexcept
{
	if (!p)
		return;
	if (live_allocations_ > 0)
		--live_allocations_;
	if (chunks_.empty())
		return;
	if (bytes == 0)
		bytes = 1;

	// 只回收栈顶那一块，顺带把它前面的对齐填充一起还回去。
	// 不在栈顶的等 reset() 整体归还。
	Chunk& chunk = chunks_.back();
	auto* q = static_cast<std::byte*>(p);
	if (q >= chunk.base && q + bytes == chunk.base + chunk.used)
		chunk.used = static_cast<size_t>(q - chunk.base);
}

void ParseArena::reset() noexcept
{
	// 断言而不是静默容忍：arena 内存被跨解析的对象持有时，后续分配会覆写它的内容，
	// 这类问题在运行期几乎无法定位，必须在 Debug 阶段就暴露。
	assert(live_allocations_ == 0
		&& "ParseArena 内存被跨解析持有：检查是否有长期存活对象使用了 PoolAllocator");

	if (chunks_.size() > 1)
	{
		// 上次解析扩过段：合并成一段，让后续解析不再重复扩段
		const size_t merged = std::clamp(capacity_bytes_, kDefaultChunkBytes, kMaxChunkBytes);
		release_chunks();
		try {
			add_chunk(merged);
		} catch (const std::bad_alloc&) {
			// 留空即可，下次 allocate 会重新建段
		}
	}
	else if (!chunks_.empty())
	{
		chunks_.front().used = 0;
	}
	live_allocations_ = 0;
}

void ParseArena::shutdown() noexcept
{
	release_chunks();
	peak_bytes_ = 0;
	grow_count_ = 0;
	live_allocations_ = 0;
}

size_t ParseArena::live_bytes() const noexcept
{
	size_t total = 0;
	for (const auto& chunk : chunks_)
		total += chunk.used;
	return total;
}

} // 命名空间 utils 结束

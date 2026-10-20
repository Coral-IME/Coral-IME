#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_USERSHAREDMEMORYREAD_SRC_STRUCTURE_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_USERSHAREDMEMORYREAD_SRC_STRUCTURE_H_INCLUDED

namespace boost::interprocess::ipcdetail {

	template<class SizeType1, class SizeType2>
	inline SizeType1 get_rounded_size(SizeType1 orig_size, SizeType2 round_to)
	{
		return ((orig_size - 1) / static_cast<SizeType1>(round_to) + 1) * static_cast<SizeType1>(round_to);
	}

} // 命名空间 boost::interprocess::ipcdetail 结束
#include <cstdint>
#include <boost/interprocess/managed_windows_shared_memory.hpp>
#include <boost/config.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/set.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/indexes/iset_index.hpp>
namespace bip = boost::interprocess;

template<class T>
using InteropOffsetPtr = bip::offset_ptr<T, std::int64_t, std::uint64_t>;

using InteropMutexFamily = bip::mutex_family;

using InteropMemAlgo = bip::rbtree_best_fit <
	InteropMutexFamily,
	InteropOffsetPtr<void>
>;

using InteropManagedSharedMemory = bip::basic_managed_windows_shared_memory<
	char,
	InteropMemAlgo,
	bip::iset_index
>;

template <typename T>
class InteropAllocator : public bip::allocator<T, InteropManagedSharedMemory::segment_manager>
{
public:
	typedef std::uint64_t size_type;
	typedef std::int64_t  difference_type;

	template<class T2>
	struct rebind {
		typedef InteropAllocator<T2> other;
	};

	InteropAllocator(InteropManagedSharedMemory::segment_manager* segment_mngr)
		: bip::allocator<T, InteropManagedSharedMemory::segment_manager>(segment_mngr) {}

	template<class T2>
	InteropAllocator(const InteropAllocator<T2>& other)
		: bip::allocator<T, InteropManagedSharedMemory::segment_manager>(other.get_segment_manager()) {}
};


using ShString = bip::basic_string<char, std::char_traits<char>, InteropAllocator<char>>;

struct UserPhraseNode;

using NodePtr = InteropOffsetPtr<UserPhraseNode>;

#pragma pack(push, 1)
struct alignas(8) UserPhraseNode {
	UserPhraseNode(
		const InteropAllocator<void>& alloc,
		const char* word,
		const int commit,
		const float dee,
		const int tick,
		const char* jpy,
		const char* qpy
		): phrase(word, alloc),pinyin(jpy, alloc), quanPinyin(qpy, alloc), commit(commit), tick(tick), dee(dee) {}
	UserPhraseNode(
		const InteropAllocator<void>& alloc,
		const std::string_view word,
		const int commit,
		const float dee,
		const int tick,
		const std::string_view jpy,
		const std::string_view qpy
		): phrase(word.data(), word.length(), alloc),
		pinyin(jpy.data(), jpy.length(), alloc),
		quanPinyin(qpy.data(), qpy.length(), alloc),
		commit(commit), tick(tick), dee(dee) {}

	ShString phrase;
	ShString pinyin;
	ShString quanPinyin;
	int commit;
	int tick;
	float dee;

	bool operator<(const UserPhraseNode& other) const {
		if (this->dee != other.dee) {
			return this->dee > other.dee;
		}
		return this->phrase < other.phrase;
	}
};
#pragma pack(pop)

struct NodePtrCompare {
	bool operator()(const NodePtr& a, const NodePtr& b) const {
		return *a < *b;
	}
};

using shared_set = bip::set<
	NodePtr,
	NodePtrCompare,
	InteropAllocator<NodePtr>
>;

struct TransparentStringLess {
	using is_transparent = void;

	template<class T1, class T2>
	bool operator()(const T1& s1, const T2& s2) const {
		return std::string_view(s1) < std::string_view(s2);
	}
};

using shared_map = boost::interprocess::map<
	ShString,
	shared_set,
	TransparentStringLess,
	InteropAllocator<std::pair<const ShString, shared_set>>
>;

#pragma pack(push, 1)
struct alignas(8) UserSharedLexiconData {
	float versionTraditional;
	float versionSimplified;
	int   tickedTraditional;
	int   tickedSimplified;
	shared_map mapTraditional;
	shared_map mapSimplified;

	explicit UserSharedLexiconData(const InteropAllocator<void>& alloc)
	  : versionTraditional(1.0f),
		versionSimplified(1.0f),
		tickedTraditional(0),
		tickedSimplified(0),
		mapTraditional(TransparentStringLess(),alloc),
		mapSimplified(TransparentStringLess(),alloc)
	{}
};
#pragma pack(pop)

#endif // STRUCTURE_H

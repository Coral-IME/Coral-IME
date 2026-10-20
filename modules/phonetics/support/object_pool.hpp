#pragma once
#ifndef CORAL_MODULES_PHONETICS_SUPPORT_OBJECT_POOL_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_SUPPORT_OBJECT_POOL_HPP_INCLUDED

#include "pch.h"
#include <cstddef>
#include <vector>
#include <utility> 

// 非线程安全，勿滥用
template <typename T>
class MemoryPool {
public:
    explicit MemoryPool(const size_t pool_size) : pool_size(pool_size)
    {
        obj_size = align_size(sizeof(T), alignof(T));
        pool = static_cast<char*>(aligned_alloc(alignof(T), obj_size * pool_size));
        if (!pool) throw std::bad_alloc();
    }

    ~MemoryPool() 
    {
        clear();
        aligned_free(pool);
        for (auto& obj : dynamic_object) 
        {
            delete obj;
        }
    }

    template <typename... Args>
    T* allocate(Args&&... args) 
    {
        if (!free_list.empty()) {
            T* ptr = free_list.back();
            free_list.pop_back();
            new (ptr) T(std::forward<Args>(args)...);
            return ptr;
        }

        if (obj_count < pool_size) {
            return new (pool + ((obj_count++) * obj_size)) T(std::forward<Args>(args)...);
        }

        T* newBlock = new T(std::forward<Args>(args)...);
        dynamic_object.push_back(newBlock);
        return newBlock;
    }

    T* allocate() 
    {
        if (!free_list.empty()) {
            T* ptr = free_list.back();
            free_list.pop_back();
            new (ptr) T();
            return ptr;
        }
        if (obj_count < pool_size) {
            return new (pool + ((obj_count++) * obj_size)) T();
        }
        T* new_obj = new T();
        dynamic_object.push_back(new_obj);
        return new_obj;
    }

    void deallocate(T* ptr) 
    {
        if (!ptr) return;

        // 判断是否在池内
        char* raw_ptr = reinterpret_cast<char*>(ptr);
        if (raw_ptr >= pool && raw_ptr < pool + pool_size * obj_size) {
            ptr->~T();
            free_list.push_back(ptr);
        }
        else {
            delete ptr;
        }
    }
    
    void clear() 
    {
        for (auto& obj : dynamic_object) 
        {
            delete obj;
        }
        dynamic_object.clear();

        for (size_t i = 0; i < obj_count; ++i)
        {
            T* obj = reinterpret_cast<T*>(pool + i * obj_size);
            obj->~T();
        }
        obj_count = 0;

        free_list.clear();
    }

private:
	static size_t align_size(size_t size, size_t alignment) 
    {
		return (size + alignment - 1) & ~(alignment - 1);
	}

#if defined(_WIN32) || defined(_MSC_VER)
	void* aligned_alloc(size_t alignment, size_t size) 
    {
		return _aligned_malloc(size, alignment);
	}

	void aligned_free(void* ptr) 
    {
		_aligned_free(ptr);
	}
#elif defined(__linux__) || defined(__ANDROID__)
    void* aligned_alloc(size_t alignment, size_t size)
    {
        void* ptr = nullptr;
        posix_memalign(&ptr, alignment, size);
        return ptr;
    }
    void aligned_free(void* ptr)
    {
        free(ptr);
    }
#endif // WIN32
    char* pool = nullptr;          // 内存池
    size_t pool_size;     // 内存池大小
    size_t obj_size = 0;
    size_t obj_count = 0;
    std::vector<T*> dynamic_object;  // 动态分配的内存块
    std::vector<T*> free_list;  // 改为支持复用的内存池
};

#endif // MEMORYPOOL_HPP（内存池头文件）

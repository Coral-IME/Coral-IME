#pragma once
#ifndef CORAL_MODULES_STORAGE_VERIFIED_RESOURCE_H_INCLUDED
#define CORAL_MODULES_STORAGE_VERIFIED_RESOURCE_H_INCLUDED

#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace coral::storage {

enum class ResourceAccess { kCreateOrOpen, kOpenExisting };

// 不可变且已校验的载荷。load/reset 串行执行；使用方必须在
// reset 或析构前释放借出的指针。加载失败保持
// 既有数据不变。不同的请求需要显式 reset。
class VerifiedResource final {
public:
    VerifiedResource() = default;
    ~VerifiedResource();
    VerifiedResource(const VerifiedResource&) = delete;
    VerifiedResource& operator=(const VerifiedResource&) = delete;

    bool load(const std::wstring& name, const std::filesystem::path& path,
        const std::string& checksum, ResourceAccess access);
    void reset() noexcept;
    const void* data() const noexcept;
    std::size_t size() const noexcept;

private:
    struct State;
    mutable std::mutex mutex_;
    std::shared_ptr<const State> state_;
};

} // 命名空间 coral::storage 结束
#endif

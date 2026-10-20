#include "verified_resource.h"
#include "resource_mapping_backend.h"
#include <thirdparty/hash-library/md5.h>
#include <atomic>
#include <utility>

namespace coral::storage {

struct VerifiedResource::State {
    std::wstring name;
    std::filesystem::path path;
    std::string checksum;
    detail::Mapping mapping;
};

VerifiedResource::~VerifiedResource() = default;

bool VerifiedResource::load(const std::wstring& name, const std::filesystem::path& path,
    const std::string& checksum, ResourceAccess access) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto current = std::atomic_load(&state_);
    if (current)
        return current->name == name && current->path == path && current->checksum == checksum;
    if (name.empty()) return false;
    try {
        auto mapping = detail::open_mapping(name, path, checksum, access);
        if (!mapping || !mapping->data || mapping->size == 0) return false;
        auto next = std::make_shared<State>();
        next->name = name;
        next->path = path;
        next->checksum = checksum;
        next->mapping = std::move(*mapping);
        std::shared_ptr<const State> published = std::move(next);
        std::atomic_store(&state_, std::move(published));
        return true;
    } catch (...) {
        return false;
    }
}

void VerifiedResource::reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::atomic_store(&state_, std::shared_ptr<const State>{});
}

const void* VerifiedResource::data() const noexcept {
    const auto state = std::atomic_load(&state_);
    return state ? state->mapping.data : nullptr;
}

std::size_t VerifiedResource::size() const noexcept {
    const auto state = std::atomic_load(&state_);
    return state ? state->mapping.size : 0;
}

namespace detail {
bool matches_checksum(const void* data, std::size_t size, const std::string& checksum) {
    if (!data || size == 0) return false;
    return checksum.empty() || MD5{}(data, size) == checksum;
}
} // 命名空间 detail 结束
} // 命名空间 coral::storage 结束

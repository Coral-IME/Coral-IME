#pragma once
#ifndef CORAL_MODULES_STORAGE_RESOURCE_MAPPING_BACKEND_H_INCLUDED
#define CORAL_MODULES_STORAGE_RESOURCE_MAPPING_BACKEND_H_INCLUDED

#include "verified_resource.h"
#include <optional>

namespace coral::storage::detail {

struct Mapping {
    std::shared_ptr<const void> lease;
    const void* data = nullptr;
    std::size_t size = 0;
};

bool matches_checksum(const void* data, std::size_t size, const std::string& checksum);
std::optional<Mapping> open_mapping(const std::wstring& name,
    const std::filesystem::path& path, const std::string& checksum, ResourceAccess access);

} // 命名空间 coral::storage::detail 结束
#endif

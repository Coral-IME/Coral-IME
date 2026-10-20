#pragma once
#ifndef CORAL_MODULES_RUNTIME_RESOURCE_ACCESS_H_INCLUDED
#define CORAL_MODULES_RUNTIME_RESOURCE_ACCESS_H_INCLUDED

#include "../storage/verified_resource.h"
#include "../foundation/text_file_helpers.hpp"

namespace coral::runtime {
inline storage::ResourceAccess resource_access() {
    return hlp::shouldAllowCreateSharedMemory()
        ? storage::ResourceAccess::kCreateOrOpen : storage::ResourceAccess::kOpenExisting;
}
} // 命名空间 coral::runtime 结束
#endif

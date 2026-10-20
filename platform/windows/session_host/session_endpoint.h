#include "coral/product/product_identity.h"
#ifndef CORAL_PLATFORM_WINDOWS_SESSION_HOST_SESSION_ENDPOINT_H_INCLUDED
#define CORAL_PLATFORM_WINDOWS_SESSION_HOST_SESSION_ENDPOINT_H_INCLUDED

#include "modules/foundation/test_environment.h"
#include <windows.h>
#include <string>

namespace coral::transport {
inline std::wstring session_object_name(const std::string& prefix)
{
    DWORD session_id = 0;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id))
        throw std::runtime_error("Cannot resolve Windows session");
    const auto name = coral::testing::objectName(prefix + std::to_string(session_id));
    return std::wstring(name.begin(), name.end());
}

inline std::wstring session_host_stop_event()
{
    return L"Local\\" + session_object_name(coral::product::kSessionStopPrefix);
}

inline std::wstring session_host_mutex()
{
    return L"Local\\" + session_object_name(coral::product::kSessionMutexPrefix);
}
} // 命名空间 coral::transport 结束

#endif // CORAL_PLATFORM_WINDOWS_SESSION_HOST_SESSION_ENDPOINT_H_INCLUDED

#ifndef CORAL_PLATFORM_WINDOWS_SESSION_HOST_PIPE_SESSION_CLIENT_H_INCLUDED
#define CORAL_PLATFORM_WINDOWS_SESSION_HOST_PIPE_SESSION_CLIENT_H_INCLUDED

#include "coral/composition/session_port.h"

#include <cstdint>
#include <memory>
#include <string>

namespace coral::transport
{

std::wstring default_session_pipe();
std::unique_ptr<composition::SessionPort> make_pipe_session_client(
	std::wstring pipe_name = default_session_pipe(),
	std::uint32_t timeout_ms = 40);

} // 命名空间 coral::transport 结束

#endif // CORAL_PLATFORM_WINDOWS_SESSION_HOST_PIPE_SESSION_CLIENT_H_INCLUDED

#pragma once
#include <cstdint>
#include <optional>

namespace coral::ipc {
struct PipePeer {
    std::uint32_t process_id = 0;
    std::uint32_t session_id = 0;
};
// 仅在管道请求回调运行期间的工作线程上可用。
// 标识来自已连接的内核管道，绝不信 任请求字段。
std::optional<PipePeer> current_pipe_peer() noexcept;
} // 命名空间 coral::ipc 结束

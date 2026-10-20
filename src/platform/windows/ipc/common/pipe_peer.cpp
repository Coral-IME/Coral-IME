#include "pipe_peer_scope.h"
namespace coral::ipc {
namespace { thread_local std::optional<PipePeer> active_peer; }
std::optional<PipePeer> current_pipe_peer() noexcept { return active_peer; }
ScopedPipePeer::ScopedPipePeer(std::optional<PipePeer> peer) noexcept : previous_(active_peer) { active_peer = peer; }
ScopedPipePeer::~ScopedPipePeer() { active_peer = previous_; }
} // 命名空间 coral::ipc 结束

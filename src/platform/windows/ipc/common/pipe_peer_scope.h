#pragma once
#include "pipe_peer.h"

namespace coral::ipc {
class ScopedPipePeer {
public:
    explicit ScopedPipePeer(std::optional<PipePeer> peer) noexcept;
    ~ScopedPipePeer();
    ScopedPipePeer(const ScopedPipePeer&) = delete;
    ScopedPipePeer& operator=(const ScopedPipePeer&) = delete;
private:
    std::optional<PipePeer> previous_;
};
} // 命名空间 coral::ipc 结束

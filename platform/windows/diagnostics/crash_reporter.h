#pragma once
#include "crash_request.h"
#include <filesystem>

namespace coral::diagnostics {
// 仅限自有的 EXE；进程生命周期回调。在任何故障前准备好路径。
// 空覆盖选择相邻的产品收集器；缺少收集器则保持过滤器不变。
bool install_crash_reporter(CrashModule module,
    const std::filesystem::path& collector_override = {}, std::uint32_t wait_ms = 30000) noexcept;
}

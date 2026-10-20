#pragma once

#include "coral/composition/presentation_gateway.h"
#include "renderer_protocol.h"

#include <memory>
#include <string>
#include <functional>

namespace coral::renderer_ipc
{

std::unique_ptr<composition::RendererPort> CreateRemoteRendererPort(
	std::wstring pipeName = DefaultRendererPipeName(),
	std::uint32_t timeoutMs = 75,
	std::function<bool()> ensureRunning = {});

} // 命名空间 coral::renderer_ipc 结束

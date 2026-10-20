#include "mode_selection.h"

namespace coral {
namespace {
thread_local InputModeState* request_mode = nullptr;
}

void InputModeState::reset(bool isFullWidth, bool isTraditionalChinese)
{
	m_isFullWidth.store(isFullWidth, std::memory_order_relaxed);
	m_isTraditionalChinese.store(isTraditionalChinese, std::memory_order_relaxed);
}

InputModeState& inputModeState()
{
	static InputModeState instance;
	return request_mode ? *request_mode : instance;
}

ScopedInputMode::ScopedInputMode(bool full_width, bool traditional) : previous_(request_mode)
{
    state_.reset(full_width, traditional);
    request_mode = &state_;
}

ScopedInputMode::~ScopedInputMode()
{
    request_mode = previous_;
}

} // 命名空间 coral 结束

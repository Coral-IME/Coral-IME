#include "mode_selection.h"

namespace coral {

void InputModeState::reset(bool isFullWidth, bool isTraditionalChinese)
{
	m_isFullWidth.store(isFullWidth, std::memory_order_relaxed);
	m_isTraditionalChinese.store(isTraditionalChinese, std::memory_order_relaxed);
}

InputModeState& inputModeState()
{
	static InputModeState instance;
	return instance;
}

} // 命名空间 coral 结束

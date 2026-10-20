// InputModeState：全半角 / 简繁体的进程默认状态及线程内请求覆盖。
//
// 背景（设置项 Step 2 解耦，见 docs/工程结构现代化改造设计.md）：
// 旧实现把"当前生效值"缓存在 SettingsConfig 的成员变量里（m_isFullWidth /
// m_isTraditionalChinese），并用 setIsFullWidthForTsf / setIsTraditionalChineseForTsf
// 这类平台命名 API 供 TSF 临时切换——平台概念反向污染了核心配置类，且同一状态
// 在 INI、共享内存、进程成员间三处存放。
//
// 现在的分工：
//   - SettingsConfig（INI）：只存"持久化的默认值/用户偏好"；
//   - InputModeState（本类）：只存"当前生效值"，TSF 快捷键/菜单切换只改这里，
//     不落盘；引擎侧（用户词典简繁选择等）与 TSF UI 均读本类。
//
// 生命周期：Engine::init 时由持久值 reset 初始化；TSF 进程再次 ActivateEx
// （reloadSettings 路径）时重新从持久值 reset，保持旧的"重激活丢弃临时切换"行为。
//
// 注意（与旧行为一致的已知局限）：本类编入 coral_preferences 静态库，各加载模块
// （coraltsf64.dll / 宿主与设置 exe）各持一份实例。因此 TSF 的临时切换
// 只对 coraltsf 模块内可见，引擎（宿主进程）仍读自己那份——这与旧
// SettingsConfig 成员缓存的行为完全一致（旧代码中 ForTsf 切换同样不跨模块）。
// 跨模块统一依赖宿主推送机制（设置项 Step 3/4，见改造设计文档）。
// 本类与 ConfigSnapshot 并列；SessionPort 使用 ScopedInputMode 在同步请求内覆盖模式，
// 退出作用域后恢复默认访问点，临时模式不影响其他线程或后续会话。
#pragma once

#include <atomic>

namespace coral {

class InputModeState
{
public:
	// 用持久化配置重置当前值（Engine::init / TSF 重激活时调用）
	void reset(bool isFullWidth, bool isTraditionalChinese);

	bool isFullWidth() const { return m_isFullWidth.load(std::memory_order_relaxed); }
	void setFullWidth(bool value) { m_isFullWidth.store(value, std::memory_order_relaxed); }

	bool isTraditionalChinese() const { return m_isTraditionalChinese.load(std::memory_order_relaxed); }
	void setTraditionalChinese(bool value) { m_isTraditionalChinese.store(value, std::memory_order_relaxed); }

private:
	InputModeState() = default;

	std::atomic<bool> m_isFullWidth{false};
	std::atomic<bool> m_isTraditionalChinese{false};

	friend InputModeState& inputModeState();
    friend class ScopedInputMode;
};

// 当前请求线程有模式覆盖时返回其局部状态，否则返回进程默认状态。
InputModeState& inputModeState();

// 与同步会话请求同寿命；同一线程按嵌套顺序销毁，不改变其他线程的模式。
class ScopedInputMode {
public:
    ScopedInputMode(bool full_width, bool traditional);
    ~ScopedInputMode();
    ScopedInputMode(const ScopedInputMode&) = delete;
    ScopedInputMode& operator=(const ScopedInputMode&) = delete;
private:
    InputModeState state_;
    InputModeState* previous_;
};

} // 命名空间 coral 结束

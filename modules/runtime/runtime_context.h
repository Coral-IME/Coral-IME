// Engine：引擎生命周期与数据边界的显式封装（Phase 3 Step 1）。
// 编排配置初始化、词库路径装配与模型加载；不直接触碰任何平台 API。
// 内部保留 SyllableParser / ScriptConversion 兼容入口，实际模型在关闭时销毁；
// 旧 coral::runtime 通过 compatInstance() 持有同一个 Engine 实例。
#pragma once

#include "runtime_snapshot.h"
#include "runtime_options.h"
#include <cstdint>

namespace coral {

class Engine
{
public:
	// 过渡单例：coral::runtime 的进程内操作持有；模型实例迁移后
	// 由宿主进程显式构造并注入，届时删除本入口。
	static Engine& compatInstance();

	// 初始化：config ini → 设置注入 → 词库装配 → 模型加载。
	// 重复调用返回 false（引擎已初始化，语义错误而非幂等重入）。
	bool init(const EngineOptions& options);

	// 关闭：销毁模型、停止加载/学习/日志；借用访问须先结束。可重复调用。
	void shutdown();

	bool initialized() const { return m_initialized; }
    std::uint64_t generation() const { return generation_; }
	const EngineOptions& options() const { return m_options; }

	// 引擎侧配置读取统一入口（快照；由宿主边界装配并原子推送）
	ConfigSnapshot& config() { return m_config; }
	const ConfigSnapshot& config() const { return m_config; }

	// 从进程内持久配置重建 EngineConfig 并推送快照（配置变更后由边界调用；
	// 双轨期数据源为 SettingsConfig 单例，Phase 5 目录落地时改为宿主显式注入）。
	// 默认经过代际门闸（SettingsConfig::pollForUpdates）：共享内存代际/INI mtime
	// 未变化时跳过 7 键读取与快照重建，解析热路径成本降为一次无锁代际读。
	// forceRefresh=true 绕过门闸（init 首次装配使用）。
	void refreshConfig(bool forceRefresh = false);

private:
	Engine() = default;
	~Engine() = default;
	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;

    std::uint64_t generation_ = 0;
	bool m_initialized = false;
	EngineOptions m_options;
	ConfigSnapshot m_config;
};

} // 命名空间 coral 结束

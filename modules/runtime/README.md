# 引擎运行边界

此目录的生命周期、配置快照和引擎接口代码当前编入 coral_engine_core 静态库，Windows 进程与 TSF 接入位于 platform/windows/。当前实现不应被称为已经验证的平台无关移植层。

- `runtime_options.h` 定义初始化路径和开关。
- `runtime_context.h/.cpp` 编排配置、词典及模型加载和 shutdown；旧 coral::runtime 通过 compatInstance 使用同一引擎实例。
- `runtime_snapshot.h/.cpp` 维护原子配置快照并配合共享配置版本检测。
- `resource_access.h` 将运行模式转换成明确的创建/只打开策略；资源所有权由存储模块的 `VerifiedResource` 管理。
- `local_session_port.cpp` 保留直接调用适配；Windows TSF 使用的远程进程接入位于其平台目录。

自动学习用户历史位于 `modules/learning/`，固定/禁用候选规则位于 `modules/ranking/`。旧共享用户词典结构不是当前学习运行路径；数据格式和迁移说明分别见相邻目录 README。

shutdown 先等待模型加载，再停止学习历史和日志后台工作。人工演示初始化后的实际 DLL 三轮正常卸载已验证；旧冒烟的立即退出策略不代表所有特殊输入、UI 或跨进程卸载路径均通过。见 [资源生命周期](../../docs/resource_lifecycle.md)。

当前总架构和测试入口见 [架构](../../docs/architecture.md) 与 [维护](../../docs/maintenance.md)。历史重构阶段的路线图及过期行号不再作为维护入口。

旧引擎 DLL 导出已移除，内部操作为 core_operations。模型/宿主生命周期仍继续迁移，见 docs/static_engine_core.md。

B2b1 已支持进程内模型销毁与重建，端口绑定核心代次；旧端口及过期拥有者不能影响新实例。生产宿主管道仍待 B2b2，契约见 docs/runtime_reconstruction.md。

B2b2 已由生产宿主持有初始化端口；无使用方的 session_host_adapter 已删除。跨进程归属、数量限制、可中断传输及正常退出见 docs/session_host_lifecycle.md，UI/剪贴板专项仍待第六批。

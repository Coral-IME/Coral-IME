# user_history

自动学习用户历史的进程内运行时与持久化边界。

## 当前结构

- `Snapshot`：按 `(partition, lookup_key, phrase)` 排序的不可变 Base；
- `Manager`：小型有序 Delta、统计 Overlay、撤销记录和后台 worker；
- `Storage`：跨进程 checkpoint mutex、读取最新代、应用 mutation、容量裁剪和原子替换；
- `Codec`：`user_history.coral2` 的显式 little-endian V2 格式；
- `LegacyCodec`：旧自动学习 `.coral` 文件的只读迁移，并为候选策略迁移提供兼容解码。

自动学习历史独占 100,000 条预算。手动固定/禁用规则已迁移到
`modules/ranking`，不再参与历史评分、容量计算和淘汰。

## 并发约束

- Lookup 不访问磁盘，不获取进程锁；
- Learn 只持有短时进程内 mutex；
- 磁盘 IO 由 worker/显式 Flush 执行；
- 多进程 checkpoint 必须在命名 mutex 内重新读取最新 V2，再应用本进程 mutation；
- generation 位于文件头，是事实来源；当前阶段通过文件更新时间轮询发现外部变更。

## 迁移状态

`CUserLexicon` 协调学习历史和候选规则两个管理器，头文件已移除旧共享内存类型及四套状态字段。旧分区值和位置字段仅用于文件解码及迁移；业务接口只接收简繁属性。旧共享内存诊断工具保留独立的历史布局定义。

# 私有更新请求、传输与取消

当前实现说明，更新于 2026-10-21。主程序在用户点击后启动私有下载进程；更新默认启用，地址为 `https://coralime.cn`，离线构建显式设置 `CORAL_ENABLE_UPDATES=OFF`。产品流程见 [更新检测](update_detection.md)，文件验证见 [安装包接受](update_package_acceptance.md)。

## 职责与请求固定

`coral::updates` 的 `download_wire` 定义字节契约，`download_job` 用注入动作执行单个同步请求。`coral::update_delivery` 持有 Windows 子进程、匿名通信对象和更新专用 HTTP。`apps/update_agent/agent_actions` 接入定位、下载、实际文件验证与发布，`apps/settings_app/UpdateCoordinator` 持有检查线程和下载进程。

窗口展示后冻结发布 id、四段版本、字节数和 SHA256；启动时把这些值连同随机 128 位非零 nonce 编码为请求。子进程只按这个 id 查下载地址，不能重新查询 latest 并替换已显示版本。描述不传入子进程。包数据必须匹配该请求的大小与摘要；最终缓存路径仍由规范版本生成，不自动覆盖已有文件。

## CUP1 字节协议

固定请求 88 字节、进度 56 字节，整数均 little-endian。所有字段显式编码，不复制 C++ struct 布局。

| 共用头偏移 | 字节数 | 内容 |
|---|---:|---|
| 0 | 4 | ASCII `CUP1` |
| 4 | 2 | 版本 1 |
| 6 | 2 | kind：请求 1，进度 2 |
| 8 | 4 | 整帧字节数 |
| 12 | 4 | IEEE CRC32，计算整帧时此字段视为零，反射多项式 0xEDB88320 |
| 16 | 16 | 本次请求 nonce，不得全零 |

| 请求偏移 | 字节数 | 内容 |
|---|---:|---|
| 32 | 4 | release id，1..INT32_MAX |
| 36 | 8 | 四个 uint16 版本分量 |
| 44 | 8 | 包字节数，1..4GiB |
| 52 | 32 | SHA256 原始字节 |
| 84 | 4 | reserved，必须为零 |

| 进度偏移 | 字节数 | 内容 |
|---|---:|---|
| 32 | 4 | stage |
| 36 | 4 | percent |
| 40 | 4 | error |
| 44 | 4 | reserved，必须为零 |
| 48 | 8 | 非零 sequence |

stage 依次为 Starting=1、Locating=2、Downloading=3、Verifying=4、Publishing=5、Ready=6、Failed=7、Cancelled=8。Starting/Locating 为 0%，Downloading 为 0..80%，Verifying 为 90%，Publishing 为 95%，Ready 为 100%；Failed/Cancelled 小于 100% 且带对应错误。error 为 None=0、Cancelled=1、InvalidRequest=2、LocationFailed=3、DownloadFailed=4、TooLarge=5、PrepareFailed=6、VerificationFailed=7、PublishFailed=8、ChannelFailed=9、AgentExited=10、Timeout=11。

初始快照 sequence=1，子进程从 2 开始上报。接受方核对 nonce、递增编号、阶段和百分比；轮询允许跳过中间快照。完全相同的重复帧可忽略，较旧帧可忽略，同编号不同内容、退步、其他 nonce 和终态之后的修改拒绝。截断、额外字节、坏版本/kind/CRC/reserved 和非法语义拒绝，解码失败保留输出。CRC 是损坏检查，不是认证或发布者签名。

## Windows 能力与进程所有权

每次请求创建匿名 request/progress mappings、mutex、cancel event，并持有真实父进程句柄。显式绝对相邻 EXE 以 suspended/hidden 方式启动，使用 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 仅继承五个必要句柄，不继承其他已标为 inheritable 的句柄。请求只读，取消仅允许等待，父进程仅允许查询/等待；没有全局命名对象、NULL DACL 或从命令载荷信任 PID 的通路。句柄列表机制见 [Microsoft 进程属性文档](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)。

子进程只接受六个严格字段（协议版本及五个不同句柄），拒绝未知、重复、符号、前导零、溢出或失效值。核对持有父进程仍存活、与当前进程同 Windows 会话，并用系统进程快照核对实际父 PID。会话检查已实现；测试覆盖实际父进程和伪造父句柄，没有进行跨登录会话的特权运行验证。

父进程先把 suspended 子进程加入独立 `KILL_ON_JOB_CLOSE` job 再恢复运行。关闭先发送本次私有取消并等待 2 秒，超时只终止所拥有的 job，再等待 2 秒。父进程退出时 job 句柄关闭约束其子进程。正常轮询使用不等待的 mutex 尝试；坏帧/废弃 mutex 失败并取消。观察到退出后重新读取最终快照，避免最后一次上报与退出之间的正常竞争。Ready 还需子进程退出码 0，不能只凭活着的子进程写入 100% 展示安装。

匿名对象和继承权限限定正常请求的通信边界；同权限进程注入、句柄窃取或替换本机程序没有在此解决。

## 有界传输与提交

更新专用 Curl multi 使用 HTTPS、TLS 对端/主机校验，不跟随重定向。连接上限 5 秒、元数据默认 30 秒、下载默认 10 分钟；multi poll 每次最多 100ms 并检查取消/截止时刻。JSON 接收上限 64KiB，下载每个块在写入前受固定包大小约束，实际接收大小必须相等，HTTP 非 2xx、读写/flush/close 失败不能报成功。调度接口见 [libcurl multi poll](https://curl.se/libcurl/c/curl_multi_poll.html) 和 [multi perform](https://curl.se/libcurl/c/curl_multi_perform.html)。系统 DNS 或库内部调用仍可能延长等待；子进程由拥有的 job 提供停止兜底，不能把 100ms 调度间隔声称为所有系统调用的严格取消时限。

检查线程由 `MetadataRequest` 持有、取消并 join，不 detached；回调异常可观察。主程序销毁时用生命周期令牌拒绝迟到任务。其他模块的旧网络库仍保留，不能据此声称整个程序网络流程重写或全部离线。只供人工测试的显式 loopback policy 允许字面量 127.0.0.1 HTTP，生产入口没有 CLI/环境变量切换。

下载、校验、发布逐阶段执行。临时文件只归本请求清理；CNG 每个 64KiB 读取块检查取消，发布前再次检查。同一句柄的不覆盖 rename 是提交边界：取消不能回滚已提交包，也不保证最后一次检查与原子 rename 之间的竞态总由取消获胜。提交后 Ready 上报是尽力而为，丢失最终信号会让父进程报告失败，已验证缓存仍可在下次检查时复用。硬停止/父进程死亡可能留下本次唯一临时文件或已提交缓存，不扫描删除历史缓存。

实际文件摘要保证与已接受元数据一致，仍不证明发布者真实性；安装前校验返回到 `ShellExecuteW` 的路径替换间隙尚存。真实 HTTPS 联调证据按具体版本登记；实际安装与签名信任仍须独立验收。

## 验证范围

相关回归包括独立协议字节夹具及坏帧、注入动作流程/失败/取消/提交语义、真实私有子进程和实际 loopback 传输。子进程夹具使用人工三字节文件，覆盖中文空格路径、只读请求/等待型取消、非白名单句柄不继承、并发 nonce、摘要失败、坏 nonce/命令/父句柄、无终态退出、重复请求后的句柄回收、正常取消、拥有 job 的硬停止及父退出；没有创建或触碰现有产品的旧全局通信对象。HTTP 夹具覆盖限额、超大/不足文件、重定向拒绝、停滞取消/超时、生产 HTTP 拒绝和回调异常。

默认官网更新开启时注册 69 项客户端测试，找到独立服务端时再增 1 项，合计 70 项；关闭更新时另外注册禁用入口用例。具体结果以 [基线报告](build-baseline.md)及 [维护记录](code-separation.md)为准，UI 完整交互/焦点与 TSF 深度验收仍按维护范围处理。

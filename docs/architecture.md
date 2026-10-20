# 当前架构与入口

本说明描述候选中的源码边界，不包含旧提交号、作者机器路径或历史设计方案。

| 部分 | 源码位置 | 职责 |
|---|---|---|
| 会话与接口类型 | `include/coral/composition/`、`modules/composition/` | 注入 CandidateSource 的状态机、SessionPort 契约；独立 coral::composition target |
| 引擎 | `modules/phonetics/`、`modules/runtime/` | 拼音解析、检索、语言评分、引擎编排与内部操作适配；coral_engine_core 静态链接 |
| 存储表和视图 | `modules/storage/` | 既有词典/二元模型文件、生成和映射视图；当前仍随引擎编译 |
| 资源所有权与后端 | `modules/storage/verified_resource.*`、`platform/{windows,posix}/` | 只读映射校验、发布与平台句柄回收；独立 `coral_resource_io` target |
| 特殊输入 | `modules/assist/{shape_input,value_input,symbol_input}/` | 部首/笔画、时间/计算与符号候选 |
| 原生开发工具 | `tools/native/` | 索引生成、安装解包、控制台与旧共享布局诊断 |
| TSF | `src/platform/windows/coraltsf/` | Windows 输入上下文、组合文本、按键分派、焦点与宿主适配 |
| 会话字节协议 | `include/coral/transport/`、`modules/transport/` | CSW4 显式编码与边界验证，无 Windows API/词典实现依赖 |
| 引擎进程 | `platform/windows/session_host/` | 承载会话和引擎请求；客户端经引擎协议访问 |
| 渲染进程 | `src/platform/windows/renderer/` | 候选窗、悬浮栏、快照与用户动作；特殊宿主有进程内候选窗路径 |
| 主程序与服务 | `src/platform/windows/coralpinyin/`、`apps/launcher_service/` | 设置界面、进程启动和服务协作；主程序也链接引擎库 |
| 通用配置 | `modules/preferences/` | INI 模板、共享配置与版本检测 |
| 构建和数据 | 根 CMake、`tools/data/`、`packaging/windows/` | 公开源码构建、外置数据生成、安装包收集与编译 |

会话宿主、客户端和资源后端位于仓库根 `platform/`，其他既有 Windows 程序仍位于 `src/platform/windows/`。旧 `coralipc` 同步管道用于 UI 命令和服务协作，逐键候选请求使用 [CSW4 会话协议](session_wire.md)，宿主内部静态链接词典核心；渲染协议负责候选快照和动作校验。

词典和模型属于运行资源，不因工具源码开源而进入公开候选。源码构建默认生成小型自制演示，不挂载正式输入。正式包和演示均要求词典索引、languagemodel.coral、纠错模型、字符模型和配置配套。

`remember_candidate` 的 conditionId 在当前实现中只有用户词典词能得到有效词条 ID，系统词条返回 `kInvalidPhraseId`；旧 parse_candidates 的 condition 参数不代表条件输入链已完整启用。调试时应依据当前输出，不能套用旧设计稿的行为承诺。

测试环境通过 `CORAL_TEST_NAMESPACE` 和 `CORAL_USER_DATA_DIR` 隔离共享对象和用户目录；未设置环境变量时仍使用产品默认命名。不要直接运行旧数据回归程序去碰当前安装的共享配置。

维护入口见 [维护与验证](maintenance.md)、[构建](build.md) 和 [数据工具](data-tools.md)。已有配置门闸、英文候选、主索引路径及 DLL 退出限制见 [候选状态](../CANDIDATE_STATUS.md)。

B2b2 当前 Windows 宿主由拥有初始化责任的端口管理静态核心，HostRouter 管理进程/client_id 会话归属、容量和死亡回收；固定工作槽通过停止事件中断管道等待。SessionHost 外壳移除。实际多进程边界与限制见 [宿主生命周期](session_host_lifecycle.md)。

第六批 A：platform/windows/text_service 的 coral::session_bridge 独立于词典核心、COM 注册和 TSF PCH，通过注入端口持有逻辑会话及已确认配置；生产工厂留在 TSF 目标，既有 launcher 保留。失败分类与验证范围见 [桥接契约](text_service_session_bridge.md)。

第六批 B：coral::product_identity 引用生成标识；coral::host_bootstrap 提供可注入时钟/动作的启动策略与独立 Win32 适配。TSF 只注入既有安装状态、目录和服务 RPC，宿主运行生命周期与词典核心不归启动器所有。见 [启动契约](host_bootstrap.md)。

第六批 C：apps/launcher_service 的拥有型 RPC adapter 委托 coral::service_launch 请求策略与 Win32 动作；coralipc 在回调线程提供 kernel pipe peer，会话授权先于路径与令牌操作。SCM 入口保留原状态算法并集成局部对象，不作实际服务运行验收；见 [服务请求](service_launch.md)。

第六批 D：coral::diagnostics 提供严格请求与仅供自有 EXE 的共用崩溃启动器，apps/diagnostic_collector 保存本次 dump/清单/ZIP。TSF 不占用宿主进程的崩溃回调；没有网络、硬件标识、历史日志附件或自动重试依赖。见 [本地诊断](local_diagnostics.md)。

第六批 E1：coral::updates 提供无 Windows 的四段版本/严格元数据及默认禁用服务配置，coral::update_delivery 验证真实文件并以同一句柄不覆盖发布；apps/update_agent 迁移原下载流，apps/settings_app 的 UpdateCoordinator 接入原窗口。网络/进度 IPC 调度仍保留，后续由 E2 收敛；见 [更新接受](update_package_acceptance.md)。

第六批 E2：modules/updates 的 CUP1/DownloadActions 将请求、进度与同步下载流程分离；platform/windows/update_delivery 拥有匿名映射、五句柄继承、父会话检查、子进程/job 和有限 Curl multi；apps/update_agent/AgentActions 连接定位/文件接受，设置协调器拥有检查线程/下载进程。原 UI 保留，仅通信回调接入；见 [私有更新请求](update_private_channel.md)。

第六批 E3a：Windows coral::lexicon_io 静态库承载文件编码/字段识别，公开 ImportedText/read_import_file 契约由设置对话框消费，移除对设置PCH的依赖；既有词典导入算法/格式与引擎校验保留。见 [文本导入](lexicon_text_import.md)。

第六批 E3b：modules/lexicon_catalog 独立配置/路由契约（默认禁用），apps/settings_app 的 OnlineLexiconCoordinator 拥有服务与本轮请求，weak token拒绝迟到任务，main先stop再结束核心/窗口/日志。词库文件/编译/记录算法保留；不新增生产自动检查或UI入口，旧运输另批。见 [在线词库](online_lexicon_catalog.md)。

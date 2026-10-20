# 开发准则

本准则适用于项目自有源码。去重迁移的批次、验收和完成状态见 [代码去重计划](docs/code-separation.md)。

## 当前任务范围

2026-10-14 维护者调整：UI 不做专门去重/改名/重写，TSF 保留既有改动，余项仅做必要引用、接入和局部命名整理。完整焦点/组合文本、Search/AppContainer 和纯 UI 生命周期不再作为本次去重验收门槛；非 UI 业务继续。混合文件按职责处理，已有回归保留，排除项记录为保留而非重写完成。具体边界见 [范围调整](docs/code_separation_scope.md)。

## 目录与依赖

- `include/coral/` 放对外 C++ 契约，按领域分组，不暴露 Windows 句柄、第三方容器或内部单例。
- `modules/` 按领域组织实现。当前分组：`foundation`（基础设施）、`preferences`（设置）、`composition`（输入会话）、`runtime`（引擎编排与进程内操作适配）、`learning`（用户学习）、`ranking`（候选规则）、`phonetics`（解析、检索与评分）、`assist`（特殊输入）、`transport`（无平台的会话字节协议）、`translation`（翻译请求/结果和目标语言，Windows 网络/加密实现位于 platform/windows）、`updates`（发布版本/元数据、请求帧、下载流程与更新服务配置）。既有 Windows 耦合仍按后续批次拆分。
- 数据表与既有格式视图位于 `modules/storage/`，保留现有格式；当前存储编译单元仍由引擎 target 组织，目标级解耦按后续接口批次实施。平台实现进入 `platform/windows/`，独立程序进入 `apps/`。原生开发工具放在 `tools/native/`，Python 数据工具放在 `tools/data/`。
- `tests/`、`tools/`、`resources/`、`packaging/`、`cmake/` 保留行业通用目录名。第三方代码保留来源、命名、许可及哈希，不为降低重复率改写上游代码。
- 不得重新建立已迁出的 `src/src/` 或 `src/engine/` 结构。尚未迁移的平台目录可以维护，新增独立功能优先落入新模块。
- 公共头使用 `#include "coral/<领域>/<文件>.h"`。模块内部允许同目录短路径；跨模块依赖通过 CMake target 声明，不引入全局 include 搜索路径。

## 命名

新增语音领域为 `include/coral/speech/` 与 `modules/speech/`，提供供应商无关整段识别接口及腾讯云适配。独立 CMake 入口不得依赖 Windows SDK、主程序或 UI，原生联调工具进入 `tools/native/speech_probe/`。边界与未实现内容见 [语音输入](docs/speech_input.md)。

Windows 语音适配位于 `platform/windows/speech/`，设置主程序持有并停止录音/识别工作与键盘监听线程。关闭、停用及凭据更换须丢弃旧结果；输入前复核原窗口及焦点，录音不落盘，密钥仅在本机加密保存且不得进入共享设置或日志。见 [Windows 语音输入](docs/speech_windows.md)。

| 对象 | 规范 | 示例 |
|---|---|---|
| 目录、项目自有文件 | 小写 `snake_case`，名称表达职责 | `scalar_text_codec.h`、`session_driver.cpp` |
| 类、结构体、枚举类型 | `PascalCase`，不用 `C`、`K`、`I` 等历史前缀 | `SliceView`、`LexiconCatalog` |
| 函数、方法、局部变量、参数 | 小写 `snake_case` | `parse_value`、`request_id` |
| 私有成员 | 小写 `snake_case` + 单个尾下划线 | `payload_`、`revision_` |
| 常量、枚举成员 | `kPascalCase` | `kFormatVersion`、`kUnavailable` |
| 命名空间 | `coral::<领域>`，小写，不新增全局类型 | `coral::foundation`、`coral::preferences` |
| CMake 自有 target | `coral_<职责>`，新增别名 `coral::<职责>` | `coral_preferences`、`coral::preferences` |
| CMake 选项、环境变量 | `CORAL_` + `UPPER_SNAKE_CASE` | `CORAL_BUILD_STORAGE_TESTS` |
| 头文件保护宏 | `CORAL_` + 仓库相对路径的大写下划线 + `_INCLUDED` | `CORAL_MODULES_STORAGE_LEXICON_TABLE_H_INCLUDED` |
| Python | 模块/函数/变量 `snake_case`，类 `PascalCase` | `build_resource_bundle` |
| PowerShell | 文件 `snake_case.ps1`，函数按 `Verb-Noun` | `Test-ResourceBundle` |

不通过随机前缀、无意义缩写或刻意改写 `size`、`data`、`begin` 等惯用名称消除合理重复。Windows COM/TSF 回调、系统 ABI、第三方接口和 CMake 规定的文件名列为例外。已有公开契约先登记调用方，再按完整批次改名；同一实体的声明、实现和调用必须一起修改。

头文件保护宏随路径同步，测试替身中的显式定义也要同步。`IDC_STATIC`、`_WIN32_WINNT`、Windows 功能常量等条件定义是系统接口，不能当作保护宏改名。类型别名也采用 `PascalCase`，解析期容器使用 `Parse` 前缀明确生命周期；可跨解析持有的视图使用 `Persistent` 前缀。公开数据结构的字段采用 `snake_case`，既有 packed 记录只改名称，不调整类型、顺序或对齐。

## 实现与接口

- 去重要求改变职责边界或实现方式。仅改名称、缩进、注释、字段顺序不算完成重写，也不改变源码来源说明。
- 优先使用 C++17 标准库的容器、RAII 与类型约束。容器视图明确所有权、生命周期和越界行为；禁止复制拥有句柄或锁的对象。
- 后台任务由对象持有并显式等待，不新增泄漏式线程池单例。宿主在核心关闭前停止请求、加载、学习与日志工作；任务异常返回调用方，不能静默吞掉。资源采用校验后提交，失败释放临时资源；借用指针访问期间禁止 reset/销毁。见 [资源生命周期](docs/resource_lifecycle.md)。
- 新解析器先完整校验再提交结果。失败不产生半有效状态，整数溢出和尾部垃圾均拒绝。不要将异常输入的前缀当作有效值。
- 新数据格式必须定义 magic、版本、字节序、区段长度、完整性检查、限制及失败语义；禁止直接落盘编译器布局的 struct。
- 新协议采用固定宽度字段，显式编码，限制载荷与条目数，拒绝未知版本；解析失败保留输出，编解码共享参数边界。协议核心与平台传输分离，以独立字节夹具验证，不能只做往返测试。超时和取消的副作用必须明确。C++ STL 对象不得跨 DLL ABI。当前已实现协议见 [CSW4 规范](docs/session_wire.md)；旧引擎 DLL 已移除，核心静态链接；内部 C++ 操作不得重新添加导出宏或建立跨 DLL STL ABI，参见 [静态核心](docs/static_engine_core.md)。
- 重写按行为需求和人工夹具进行，不从闭源项目复制函数体，不将私有词库、代码片段、绝对路径和对比报告放入公开仓库。
- 新代码 UTF-8、四空格缩进；新文本默认 CRLF，与当前 Windows 工作树一致。旧编码迁移单独进行，不为改名批量重编码历史文件。保留已有作者与许可说明。

- 会话状态机通过 CandidateSource 注入查询/学习，平台和词典适配留在 runtime；共享解析器必须由二进制内统一的锁保护。工厂声明初始化与关闭的所有权，借用端口不能擅自关闭引擎；端口绑定运行代次，重建前结束所有借用访问，旧端口不能跨代次复活。模型对象在关闭时销毁，配置路径和转换器在重开时重新绑定；见 [模型重建](docs/runtime_reconstruction.md)。见 [会话契约](docs/composition_sessions.md)。

## 验证与交付

每批先记录改前基线、路径和符号映射，再同步构建、测试、脚本、文档及 `docs/source-license-map.json`。功能重写必须覆盖真实边界行为，不增加只验证改名的测试。

以实际执行的结果记录通过数量；UI、IPC、共享内存测试需要正常 Windows 环境。沙箱权限失败不能当作功能失败，也不能声称已经通过。正式数据和安装验收独立于人工演示测试。

每批完成后更新去重计划中的状态及剩余项。不得把旧源码 ZIP、旧哈希或旧构建日志写成当前结果。迁移中不发布、不安装、不操作现有产品的用户数据。

- Windows 会话宿主从管道核对实际进程与会话归属，固定工作槽和会话上限，回收死亡客户端；不得恢复无限连接线程或不可中断的同步管道刷新。停止先中断传输并等待在途请求，再销毁核心；传输端口重连后旧会话失效，不自动重放业务操作；端口析构用受截止值限制的内部租约释放清理存活进程的旧会话。见 [宿主生命周期](docs/session_host_lifecycle.md)。

- 文本服务桥接以最后成功配置恢复逻辑会话，新版本即使值相同也发送，不能通过重连绕过旧版本拒绝。读取最多一次恢复重试；有副作用操作仅对明确 InvalidSession 重试，断线/超时/协议错误不重放；恢复配置和业务操作共用恢复额度。候选快照完整构造后提交，界面版本与传输编号分离。见 [桥接契约](docs/text_service_session_bridge.md)。

- 产品运行标识在 resources/product_identity.json 修改，再运行 tools/checks/product_identity.py --write，提交全部生成引用；不手改生成物。宿主启动用显式绝对常规文件，禁止截断模块路径或隐式当前目录搜索；探测用明确的立即等待值，操作预算共用起始时刻，既有服务回调不可中断的限制必须记录。见 [启动契约](docs/host_bootstrap.md)。

- 服务启动请求必须完整解析唯一会话参数，并核对管道实际调用进程的会话；不得默认到控制台或接受跨会话请求。路径仅来自登记的绝对常规文件，已运行判断同时核对路径和会话；用户环境创建失败停止启动，清理前保存错误。回调身份由对象限定在线程回调期间，不从载荷相信进程身份；已有 RPC 的其他策略按各自批次迁移。见 [服务请求契约](docs/service_launch.md)。

- 崩溃回调仅由自有 EXE 安装，先在正常启动缓存同目录显式采集路径；DLL 不接管宿主进程的异常过滤器。故障路径用固定缓冲，不读注册表、写项目日志或创建线程；采集默认本地保存，不附带既有用户日志/硬件标识，不自动上传或扫描删除旧报告。版本化命令完整验证，异常采集失败不能冒充快照成功，有限等待不保证产物完成。见 [诊断契约](docs/local_diagnostics.md)。

- 更新版本/元数据用 coral::updates 的严格契约，不接受数字前缀、不完整摘要或服务器版本路径字符。安装包核对实际大小和 SHA256，读取失败不能与空 hash 一起通过；发布同一已校验句柄且不覆盖目标，安装前重验，失败不发送成功事件。不扫描删除旧缓存，停止并等待拥有的任务后释放资源。更新服务默认官网 HTTPS，离线构建显式设置 CORAL_ENABLE_UPDATES=OFF，不恢复旧服务地址。更新请求固定窗口已显示的 id/version/size/SHA256；不用全局命名共享对象、NULL DACL 或重新查询 latest。CUP1 显式帧核对关联/单调进度；仅继承必要能力、核对真实父进程与会话，关闭只停止拥有的 job。传输限定 HTTPS/接收限额/取消与超时，校验分块检查取消；提交后取消不回滚，硬停止可留本次临时文件。元数据检查线程由对象持有并等待，不 detached。实际服务器/安装与发布者真实性另行验收，见 [更新接受](docs/update_package_acceptance.md) 和 [私有更新契约](docs/update_private_channel.md)。

- 文本词库导入识别位于 coral::lexicon_io 与 Windows 独立目标，不重新放入 UI/PCH；对话框只消费 ImportedText。词典相关解析按维护者要求保留算法和格式，改名不得记为重写；保留实现的部分结果/旧编码边界须明确登记。引擎导入字段整批迁移时同步所有使用方，不能因本模块改名散改全仓。见 [文本导入](docs/lexicon_text_import.md)。

- 在线词库服务在 coral::lexicon_catalog 配置，CORAL_LEXICON_SERVICE_BASE_URL 默认为空；未配置不创建请求/操作词库目录，不回退旧主机。管理器 stop 先使本轮 weak token 过期，再取消并销毁/等待拥有的请求；工作回调和已排队 app 任务都核对本轮令牌。主程序在核心/窗口/日志结束前停止请求，不以 stop 标志代替等待。旧词库算法/格式保留，原网络运输与文件事务的未完成边界须继续记录。见 [在线词库](docs/online_lexicon_catalog.md)。

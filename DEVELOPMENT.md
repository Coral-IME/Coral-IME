# 开发准则

本准则适用于项目自有源码。工程目录、环境准备与编译调试入口见 [README](README.md)。

## 目录与依赖

- 公共 C++ 契约放在 `include/coral/<领域>/`，领域实现放在 `modules/<领域>/`。公共接口不暴露 Windows 句柄、第三方容器或内部单例。
- 平台适配放在 `platform/`，程序协调与独立程序放在 `apps/`；Windows 主程序、TSF 和渲染界面在 `src/platform/windows/` 维护。新增独立业务优先放入对应领域模块，避免将业务塞入 UI、PCH 或公共辅助头。
- C++ 开发工具放在 `tools/native/`，数据、检查、诊断与发布脚本分别放在 `tools/data/`、`tools/checks/`、`tools/diagnostics/`、`tools/release/`。构建产物和临时数据写入专用输出目录。
- 公共头使用 `#include "coral/<领域>/<文件>.h"`。模块内部允许同目录短路径；跨模块依赖通过 CMake target 声明，不增加全局 include 搜索路径。
- 协议核心与平台传输分开；语音基础模块的独立入口不依赖 Windows SDK、主程序或 UI。第三方代码保留上游来源、命名、许可与哈希。

## 命名与文本

| 对象 | 规范 | 示例 |
|---|---|---|
| 目录、项目自有文件 | 小写 `snake_case`，名称表达职责 | `session_driver.cpp` |
| 类、结构体、枚举、类型别名 | `PascalCase` | `SliceView`、`LexiconCatalog` |
| 函数、方法、局部变量、参数、公开字段 | 小写 `snake_case` | `parse_value`、`request_id` |
| 私有成员 | 小写 `snake_case` + 单个尾下划线 | `payload_`、`revision_` |
| 常量、枚举成员 | `kPascalCase` | `kFormatVersion` |
| 命名空间 | `coral::<领域>`，小写 | `coral::preferences` |
| CMake 自有 target / 别名 | `coral_<职责>` / `coral::<职责>` | `coral_preferences` / `coral::preferences` |
| CMake 选项、环境变量 | `CORAL_` + `UPPER_SNAKE_CASE` | `CORAL_RUNTIME_DATA_DIR` |
| 头文件保护宏 | `CORAL_` + 仓库相对路径的大写下划线 + `_INCLUDED` | `CORAL_MODULES_STORAGE_LEXICON_TABLE_H_INCLUDED` |
| Python | 模块/函数/变量 `snake_case`，类 `PascalCase` | `build_resource_bundle` |
| PowerShell | 文件 `snake_case.ps1`，函数按 `Verb-Noun` | `Test-ResourceBundle` |

- 保留 `size`、`data`、`begin` 等惯用接口名。Windows COM/TSF 回调、系统 ABI、第三方接口及构建工具规定的名称按原接口使用。
- 路径或公开符号变更时，同步声明、实现、调用、构建、工具、文档及 `docs/source-license-map.json`。头文件保护宏和测试替身的显式定义也需同步；系统功能宏不按保护宏改名。
- 解析期容器使用 `Parse` 前缀，跨解析持有的视图使用 `Persistent` 前缀，明确生命周期。既有 packed 记录的类型、顺序及对齐属于格式契约，不随名称调整。
- 新代码使用 UTF-8、四空格缩进；新文本默认 CRLF。资源脚本按资源编译器要求保留编码，不顺带批量重编码其他文件。
- 项目自有源码新增、改写的说明性注释使用简体中文。标识符、API 名、字符串资源、作者署名、注释掉的代码、保护宏和构建工具标记保持各自要求。

## 实现与生命周期

- 优先采用 C++17 标准库、RAII 与类型约束，明确容器视图的所有权、生命周期和越界行为。拥有句柄或锁的对象禁止复制；借用指针访问期间禁止重置或销毁资源。
- 解析输入先完整校验，再提交结果。拒绝整数溢出、尾部垃圾及不完整值，失败不留下半有效状态。资源同样采用校验后提交，失败释放临时资源。
- 新数据格式定义 magic、版本、字节序、区段长度、完整性校验、数量限制及失败语义，不直接落盘编译器布局的 struct。
- 协议使用固定宽度字段和显式编码，限制载荷与条目数，拒绝未知版本，编解码共享参数边界；用独立字节夹具验证，不能仅做往返测试。C++ STL 对象不得跨 DLL ABI；引擎核心静态链接，内部操作不添加 DLL 导出宏。
- 后台任务由对象持有，关闭时取消并等待；不新增 detached 工作线程或泄漏式单例。停止请求、加载、学习和日志工作后，再释放核心和窗口；任务异常返回调用方。
- 会话状态机通过 `CandidateSource` 注入查询与学习，平台和词典适配留在 runtime。共享解析器由二进制内统一锁保护；工厂明确初始化及关闭责任，借用端口不关闭引擎，端口与资源绑定运行代次。重建前结束借用访问，关闭时销毁模型，重开时重新绑定配置路径和转换器。
- 异步结果核对请求标识和运行代次，关闭、停用或配置变化后丢弃失效结果。超时、取消与提交后的副作用必须明确；有副作用操作不因断线或超时自动重放。

资源与会话契约见 [资源生命周期](docs/resource_lifecycle.md)、[运行时重建](docs/runtime_reconstruction.md)、[会话接口](docs/composition_sessions.md)和 [CSW4 协议](docs/session_wire.md)。

## Windows、网络与凭据

- 会话宿主从管道核对实际进程及会话归属，限制工作槽与会话数量，回收死亡客户端。停止时中断传输并等待在途操作，再销毁核心；使用有期限的等待，不依赖同步管道刷新。重连后会话失效，端口析构通过有截止值的租约释放清理会话。
- 文本服务桥接以最后成功配置恢复会话，新修订即使值相同也发送，不通过重连绕过版本检查。读取最多一次恢复重试；有副作用操作仅对明确的 `InvalidSession` 重试，配置恢复和业务操作共用恢复额度。候选快照完整构造后提交，界面版本与传输编号分离。
- 产品运行标识在 `resources/product_identity.json` 修改，运行 `python tools/checks/product_identity.py --write` 并提交生成引用，不手改生成物。宿主和服务使用明确的绝对常规文件路径，核对真实进程、路径和 Windows 会话，不隐式搜索当前目录。操作预算共用起始时刻。
- 服务请求完整解析唯一会话参数，不默认到控制台或接受跨会话请求。用户环境创建失败即停止启动，清理前保存错误；回调身份限定在线程回调期间，不从载荷相信进程身份。
- 崩溃回调仅由自有 EXE 安装，正常启动时缓存采集程序路径；DLL 不接管宿主进程的异常过滤器。故障路径使用固定缓冲，不读注册表、写项目日志或创建线程。采集仅本地保存，不自动附带用户日志或硬件标识，不自动上传；有限等待不等于产物完成。
- 更新版本和元数据严格解析，核对包大小与 SHA-256，使用同一已校验句柄提交且不覆盖目标，安装前重验。读取失败不能冒充有效摘要，失败不发送成功事件。请求固定已展示的发布身份，校验通信关联与单调进度；只继承必要句柄，只停止拥有的进程任务，不扫描删除其他更新缓存。
- 更新通信不用全局命名共享对象或 NULL DACL，核对真实父进程与会话。网络请求限定 HTTPS、接收大小、取消与超时；校验分块检查取消，提交后的取消不回滚。更新默认使用官网配置，关闭更新使用 `CORAL_ENABLE_UPDATES=OFF`。在线词库地址为空时不创建请求或操作词库目录。
- 词库导入解析独立于 UI/PCH，对话框只消费导入结果。在线词库管理器先使本轮令牌失效，再取消并等待请求；网络回调和已排队任务均校验本轮令牌。
- 云服务凭据仅在本机加密保存，不进入共享设置、日志或公开夹具。语音录音不落盘，识别结果输入前复核原窗口与焦点；当前产品的语音入口屏蔽。

具体边界见 [宿主生命周期](docs/session_host_lifecycle.md)、[TSF 桥接](docs/text_service_session_bridge.md)、[宿主启动](docs/host_bootstrap.md)、[服务启动](docs/service_launch.md)、[本地诊断](docs/local_diagnostics.md)、[更新包接受](docs/update_package_acceptance.md)、[更新通信](docs/update_private_channel.md)、[文本导入](docs/lexicon_text_import.md)、[在线词库](docs/online_lexicon_catalog.md)、[翻译](docs/translation_framework.md)及 [Windows 语音](docs/speech_windows.md)。

## 验证与提交

- 按行为需求与人工输入验证功能和失败边界，执行与改动相关的测试；结构调整后运行完整默认测试。不添加只验证改名或复述实现的测试，不降低既有断言来消除失败。
- UI、IPC、共享内存检查需要正常 Windows 环境。按实际执行记录结果、环境和对应源码版本，权限失败与功能失败分别说明；已有日志和哈希不能代表修改后的工作树。
- 使用隔离用户目录和共享对象命名空间验证，避免操作现有安装的用户数据。正式数据质量、实际宿主、安装、升级、卸载及签名信任分别验证，不由人工演示结果推断。
- 公开输入须为人工构造或来源、许可已确认的材料；不从闭源项目复制函数体，不提交私有词库、模型、语料、凭据、真实用户记录或私有审计材料。保留原作者和许可说明，名称调整不改变来源。
- 数据处理先运行公开小样本，输入与输出分离，使用新的输出目录并保留失败日志。正式包要求词典、模型、配置和清单配套，运行资源版本、程序版本与打包参数一致。

构建回归命令见 [README](README.md)，数据生成与打包入口见 [数据工具](docs/data-tools.md)，来源与许可要求见 [LICENSING.md](LICENSING.md)及 [第三方通知](THIRD_PARTY_NOTICES.md)。

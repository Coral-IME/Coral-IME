# 服务启动请求

第六批 C，2026-10-14。原服务目录迁入 `apps/launcher_service`，RPC adapter、SCM 入口、资源按职责组织；`platform/windows/service_launch` 提供独立启动请求策略与 Win32 动作实现。原 `CCoralPinyinService` singleton 和 `help.hpp` 删除，无使用方的 PCH 删除。服务、产物和三个 RPC 方法名继续使用现有标识；不是全部 Windows 程序迁移完成。

## 请求与身份

`LaunchMainProcess`、`LaunchEngineHost`、`LaunchRenderer` 分别对应设置程序、会话宿主、绘制程序。请求必须只有一个参数，表示规范 ASCII 十进制 Windows 会话编号，范围 1 至 4294967294；空、缺省、符号、空白、前导零、尾部文本、溢出和系统/无会话哨兵拒绝。没有“转到当前控制台会话”的默认行为。旧的无参数调用不再支持，调用方不能取得编号时停止发请求。

coralipc 从当前连接查询实际进程 ID，并用 GetNamedPipeClientSessionId 直接查询该连接的 Windows 会话，存为仅在当前 worker 回调期间有效的 `coral::ipc::PipePeer`，不按进程号再次查找会话。身份保存在 thread_local，并由对象在回调退出（包括异常）后恢复，不改变原 wire 或业务回调签名。身份查询失败为无身份，不能授权服务请求。[Windows 管道会话接口](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeclientsessionid)。

请求只能启动实际调用进程所属的交互会话；无身份、零进程 ID 或请求不同会话，返回 AccessDenied，且不读登记路径、不查询进程、不请求用户令牌。这是会话层面的授权，不宣称已实现所有用户/角色策略；其他既有 RPC 仍按原授权策略处理。

## 路径与执行

三个入口都经过安装门控。安装互斥名加入 `resources/product_identity.json`，代码和 NSIS 共用生成引用，旧值保留；当前清单 20 项。

唯一登记来源为 HKLM 的 32 位服务启动键，读取 REG_SZ 时先查询长度、按 UTF-16 字节数分配，再完整读取，限制 32768 字符，拒绝读取变化/无效长度/嵌入 NUL，不把固定缓冲截断结果当作路径。路径必须绝对、无引号/NUL，且 basename 是登记的设置程序；其余目标只选择同目录下的既定程序，不接受调用方路径或参数。文件检查要求常规文件，空或相对登记不会落到当前目录。

“已经运行”同时核对目标 Windows 会话与完整程序路径，避免跨会话或同名其他程序挡住启动；大小写按 Windows 路径比较。查询失败不能证明已有实例，后续启动仍可能由程序自身的单实例逻辑拒绝。此检查与启动不是原子事务，不承诺消除并发启动竞争。

Win32 动作取得指定会话用户令牌、复制主令牌、创建对应环境，再以显式绝对程序路径、程序目录为工作目录、不继承句柄和隐藏标志调用 CreateProcessAsUserW。环境创建失败返回错误，不在缺少用户环境时继续。仅设置程序传 `--startup-type=2`；宿主和绘制不再收到这项设置程序参数。令牌、环境和进程/线程句柄由对象释放；返回时先保存错误码，不让后续清理覆盖它。

## 错误与所有权

| 状态 | RPC 返回 |
|---|---|
| launched / already_running | S_OK |
| invalid_argument | E_INVALIDARG |
| access_denied | E_ACCESSDENIED |
| installing | S_FALSE |
| path_not_found | HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) |
| launch_failed | 原 Windows 错误对应 HRESULT，缺少错误时 E_FAIL |
| internal_error | E_FAIL |

`LaunchRpcServer` 持有 CIpcServer，先注册方法再接收请求，停止后等待回调结束，再释放业务动作。SCM 入口以局部对象绑定 server/backend 生命周期；SCM 状态与 Windows 回调本批主要作命名迁移，未声称全面重写或实际安装验收。

客户端 IPC 超时不会中止已经执行的业务/Windows API，不代表进程没有启动；本策略不自动重放。服务/宿主/TSF 要成套更新，不能以旧服务的宽松参数行为作为新客户端保证。

launched 表示系统进程创建成功，不保证目标程序已就绪；宿主启动器仍要等待管道及 CSW4 握手。start/stop 由拥有线程串行调用，停止等待回调结束。

## 验证范围

`service_launch_policy` 使用人工动作验证完整解析、无身份/跨会话拒绝、三个角色的固定路径、安装门控、非法登记/非文件、已运行及错误/异常；拒绝分支检查没有启动副作用。

`service_launch_rpc` 使用实际 coralipc server 和独立客户端进程，backend 为人工动作；验证三种方法、畸形/跨会话参数、安装与 Windows 错误返回，核对内核取得的子进程 ID，停止/重开及回调外身份为空。不会连接已安装服务或执行用户令牌启动。真实 SCM 运行、WTS/CreateProcessAsUser 的提权路径、Search/AppContainer、COM 焦点/组合文本与 UI 生命周期仍待专项。

词典/模型算法与格式保持原样，迁移分类与映射见 [变更表](service_launch_migration.json)。

本批 x64 Release/Debug 全量构建及完整默认回归各 41/41，Win32 核心、客户端、资源工具、TSF、服务和新请求模块构建通过。65 个词典 C++ 文件、27 个既有演示、2,600 个第三方和 483 个只读对照哈希不变；RPC/CSW4 编解码未变，许可和原编码/换行核对通过。本批未安装、签名、发布或提交；上述服务测试使用真实 RPC 传输和人工业务动作，不能代替真实 SCM/提权运行验收。

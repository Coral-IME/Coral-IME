# 本地诊断采集

第六批 D（2026-10-14）按维护者选择改为默认本地保存。`apps/diagnostic_collector` 提供 `coral_diagnostic_collector` target；产物名仍由产品标识清单生成。`platform/windows/diagnostics` 的 `coral::diagnostics` 共用严格请求和自有 EXE 崩溃启动器。旧上传、设备标识、历史报告重试与附带用户日志代码已删除；手动上传界面尚未实现。

## 请求与失败

新命令行必须恰好提供以下五项，可交换顺序，拒绝重复、未知项和旧参数：

```text
--coral-crash=1 --pid=123 --tid=0 --exception=0 --module=settings
```

数值必须为规范无符号十进制，不接受符号、空格、前导零、十六进制、溢出或尾部垃圾。PID 范围为 1..4294967294，TID 为 0..4294967294；异常地址为 uint64，采集前再检查本机指针宽度。非零异常地址要求非零 TID。角色只允许 `settings`、`updater`、`session_host`，不能用任意字符串拼接文件路径。零地址表示明确请求快照；异常转储失败不会降级成快照并报告成功。解析失败不改变结果。

退出码：0 表示 dump、清单和 ZIP 均已保存；2 表示参数/地址宽度非法；3 表示进程打开、架构核对或转储失败；4 表示目录、文件、压缩或其他运行失败。错误可能留下新目录或不完整转储，不生成成功 ZIP；已有报告不覆盖、不扫描、不删除。采集不自动重试。

## 保存范围

默认目录是 `%TEMP%\CoralPinyin\diagnostics\report_<随机128位编号>\`。显式 `CORAL_DIAGNOSTIC_OUTPUT_DIR` 可替换根目录，必须为绝对路径；测试使用工作区中的独立人工目录。并发以实际新建目录的结果保留唯一报告，文件使用 CREATE_NEW，ZIP 通过已有归档层完成后发布。

每个成功报告只包含本次 `process.dmp`、`report.json` 以及含这两个文件的 `report.zip`。清单记录格式版本、固定角色、PID、TID 和是否异常采集。没有设备/磁盘序列号、旧日志、用户词库或输入记录附件，没有网络依赖和上传地址。MiniDumpNormal、卸载模块和线程信息仍可能包含路径、调用栈和内存中的输入内容，不能据此声称诊断不含个人信息。

DbgHelp 仅从系统目录加载；在独立进程里转储明确指定的仍存活目标，异常地址按目标进程地址处理，拒绝不同 WOW64 架构。详见微软 [MiniDumpWriteDump](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump) 和 [MINIDUMP_EXCEPTION_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/ns-minidumpapiset-minidump_exception_information)。本批未实测跨架构采集；Win32 构建只证明相应实现可编译。

## 进程边界

设置、更新和引擎宿主在正常启动期间缓存同目录的固定采集程序绝对路径。取消故障时的注册表路径读取，不搜索当前目录；缺少采集程序或安装参数无效时不改变现有异常过滤器。回调和配置拥有整个自有 EXE 生命周期，第二次安装拒绝，不允许 DLL 借用此入口。

故障回调用固定缓冲构造参数，不调用项目日志、注册表、C++ 字符串分配或新线程；使用显式程序路径、不继承句柄、隐藏启动。最多等待 30 秒，之后结束本自有故障进程；超时不会杀掉采集程序或保证 ZIP 完整。Win32 调用内部仍可能分配资源；堆/栈严重损坏、递归/并发故障、调试器或其他库替换回调时不保证采集。未安装成功则由原应用/系统机制处理。

TSF 不再在 DllMain 安装进程级崩溃过滤器，也不终止所在应用。该系统接口会替换调用进程所有现有与未来线程的过滤器，故 DLL 加载/卸载必须尊重应用所有权；见微软 [SetUnhandledExceptionFilter](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setunhandledexceptionfilter)。TSF 所在应用的转储由应用或 Windows 的诊断机制负责。

## 验证与待办

默认测试新增严格参数夹具、真实本地诊断进程和 TSF 过滤器所有权三项。使用人工实验进程验证并发快照、主线程/工作线程未处理异常、中文空格程序路径、ZIP 字节与异常流、坏参数、坏地址、不可写根目录、缺失/不可启动/慢采集程序和不变的既有文件。实际加载/卸载构建出的 TSF DLL 核对宿主 sentinel 过滤器。

这些检查不制造正在使用的产品故障，不安装或注册 COM；完整焦点、Search/AppContainer、堆/栈损坏及真实更新/设置界面故障未验证。手动上传入口、保留期限和用户报告管理留给后续；新 CLI 与旧调用不兼容，必须成套分发 caller 和 collector。其他程序的更新/在线词库仍需各自迁移，不能将此批称为全程序离线验收。

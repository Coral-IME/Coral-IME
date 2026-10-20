# 首次输入耗时诊断

在 Windows 用户环境变量中设置 `CORAL_INPUT_TIMING=1`，然后重新启动承载 TSF 的目标程序、`coral_engine_host.exe` 和 `coral_renderer.exe`。服务代为启动引擎时会为目标用户创建环境块，因此先设置用户环境变量才能采集到子进程时间点。调试输出使用 Windows `OutputDebugStringW`，可用 DebugView 等工具抓取并按 `[coral_input_timing]` 过滤。采集结束后移除环境变量并重启这些进程。

如需直接保存输出，在输入前从工作区运行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\diagnostics\capture-input-timing.ps1 -OutputPath C:/diagnostics/input-timing-capture.log`，保持该窗口运行，输入结束后按 Ctrl+C。捕获器只保存带 `[coral_input_timing]` 的行。

每行包含 `qpc_us`（跨进程可对齐的单调时间点）、`pid`、`tid`、`stage`、`elapsed_us`、`id` 和 `result`。`elapsed_us` 是当前阶段耗时；`id` 在 TSF 按键阶段是虚拟键码，在引擎请求阶段是请求号，在候选查询阶段是候选版本。`result` 是 HRESULT、状态码或布尔值，具体看阶段。诊断开关关闭时不输出日志。

重点看：

1. `tsf.test_key_down`、`tsf.key_down`：Windows 调用 TSF 的总耗时。
2. `tsf.key_context`、`tsf.key_ensure_session`、`tsf.start_composition`、`tsf.invoke_key_handler`：首键在输入法内部的同步步骤。
3. `engine.ensure_host`、`engine.direct_launch`、`engine.service_launch`、`engine.ensure_service`、`legacy_ipc.connect` / `legacy_ipc.exchange`、`host.init_engine`、`host.pipe_ready`：首次拉起引擎所用时间。普通宿主可直接拉起引擎；受限宿主的服务调用才经过旧 `coralipc`。
4. `engine.handshake`、`tsf.create_session`、`tsf.session_config`、`engine.pipe_connect`、`engine.transact`、`host.dispatch`：新引擎协议的连接、往返和服务端执行耗时。
5. `tsf.query_engine`、`engine.parse_full`、`tsf.publish_candidates`、`renderer.ensure_available`：候选计算与显示耗时。

`coralipc` 旧同步管道目前负责 UI 命令与通过服务拉起进程，不承担逐键候选查询。当前 coralipc 客户端的连接、发送和接收共用 1500 ms 截止时间，服务端也有收发/断开截止时间；业务回调超时并不代表操作未执行。若日志显示 `legacy_ipc.connect` 或 `legacy_ipc.exchange` 很长，就要重点检查服务启动与旧管道。候选查询使用 `CoralPinyin.Session.v4` 新管道，但首屏查询仍在按键处理线程中同步等待。




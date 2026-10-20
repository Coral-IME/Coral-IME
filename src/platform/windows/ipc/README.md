# coralipc 传输约定

每次连接处理一个请求。长度头是一个独立的 4 字节 DWORD 管道消息，表示随后独立消息的 UTF-16 字符数，不包含末尾 NUL。请求和响应均要求 `1 <= length <= 512 * 1024`（最多 1 MiB 正文）。零长度、超长、短头、正文截断、超出声明长度的消息和嵌入 NUL 均拒绝。

客户端使用 overlapped I/O，连接、重试退避、发送和接收共用 1500 ms 截止时间；超时返回 `HRESULT_FROM_WIN32(ERROR_TIMEOUT)`。服务端收发和等待客户端断开也有 1500 ms 截止时间，停止事件取消 accept 和工作线程中正在等待的 I/O。取消后必须等待该 I/O 完成，才可释放 OVERLAPPED 和缓冲区。

协议 v2 以 `@coralipc2|` 开头。字段内 `%`、`|`、`:` 分别编码为 `%25`、`%7C`、`%3A`，单遍解码；其他 Unicode 字符（包括 Ō/Ŏ）保持原样。新版服务端仍识别没有标记的旧协议，并按旧格式回复，以避免误解码旧报文中本来就是文本的 `%7C` 等内容。旧协议本身无法区分原始 Ō/Ŏ 与转义符，兼容旧客户端时仍保留它的历史语义。

新版客户端需要新版服务端。发布时应一起重新构建并更新 coralpinyinsvc、coralpinyin 和 coraltsf，并重启旧服务/宿主；不自动回退到旧编码，以免重新引入 Unicode 数据损坏。

服务端固定 16 个管道实例，默认 2 个工作线程。首个实例使用 `FILE_FLAG_FIRST_PIPE_INSTANCE`，同名服务已存在时启动失败；启动失败会回收已创建资源。先完成 API 注册再启动接收线程，公开 RegisterApi 与查表通过互斥锁同步，执行回调时不持注册锁。Uninit 先停止服务，再销毁业务上下文。

停止等待业务回调返回。IPC 超时会释放客户端的等待，但不能强制中断任意业务回调，也不代表该操作没有执行；有副作用的操作超时后不可据此自动重发。

独立回归测试：

```powershell
cmake -S tests/ipc -B build/ipc-test-build -G "Visual Studio 16 2019" -A x64
cmake --build build/ipc-test-build --config Release
ctest --test-dir build/ipc-test-build -C Release --output-on-failure
```

顶层工程也注册 `coralipc` CTest。测试使用独立的进程 ID 管道名，不连接正在运行的产品服务。

第六批 C：回调期间可通过 pipe_peer.h 的 coral::ipc::current_pipe_peer 读取内核连接的进程/会话快照，thread_local 作用域退出时恢复，无身份时返回空；wire 和回调签名不变。launcher_service 使用这份身份限制请求到调用方会话，其他既有 RPC 策略未统一改变。

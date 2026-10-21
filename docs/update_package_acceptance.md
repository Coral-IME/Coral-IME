# 更新元数据与安装包接受

本页说明当前文件接受契约。客户端使用官网版本 2 元数据并在用户点击后下载和启动安装，流程见 [更新检测](update_detection.md)，进程通信和取消见 [私有更新请求](update_private_channel.md)。

## 服务配置与版本

`CORAL_ENABLE_UPDATES` 默认 ON，基础地址默认 `https://coralime.cn`；离线构建显式设为 OFF。地址接受 HTTPS 主机名、IPv4、可选端口和基础路径，不接受凭据、查询、片段或不安全字符；不支持 IPv6 字面量。检查路径为 `/api/v1/version/latest`，下载位置路径为 `/api/v1/version/getDownloadUrl?Id=<id>`。不回退旧服务。

源码默认程序版本为 **1.0.0.3**；可显式覆盖，打包版本与运行配置应一致。本机包版本先读取相邻配置，缺失或无效时回退程序版本资源。四段版本每段为 0..65535 的规范十进制，拒绝符号、前导零、空段、溢出和尾部字符，按数值逐段比较。

## 元数据接受

版本 2 通知包含发布 ID、version、fileSize、fileSha256 和 description；通知解析先检查 schema_version，再调用共享包元数据解析。发布 ID 为 1..2147483647，包大小为 1..4294967296 字节，SHA256 为恰好 64 个十六进制字符并转换为 32 字节摘要，说明最多 16KiB。整体最多 64KiB，根及 data 重复键、非法 UTF-8、嵌入 NUL、截断及尾部垃圾均拒绝；完整成功后才替换结果。

下载位置要求 data/url 字符串，最多 8KiB，HTTPS 和非空主机，不含凭据、反斜杠、空白/控制字符或 NUL。实际运输继续核对 TLS、状态、接收限额、超时与取消，不跟随重定向。显示后固定发布 ID、版本、大小和摘要，下载进程不能重查 latest 后替换版本。

## 文件校验与发布

缓存位于 `%TEMP%\CoralPinyin\updates\`。本次 pending 文件用随机 128 位名称及 CREATE_NEW 预留，ready 文件为 `package_<规范版本>.exe`；不扫描、迁移或批量删除旧缓存。失败后只在请求停止并等待后清理本次 pending 文件，硬停止可能遗留文件。

接受层验证实际长度和 SHA256；读取失败、大小不符或摘要失败均不能报成功。发布时以 GENERIC_READ|DELETE 和 FILE_SHARE_READ 打开 pending，在同一持有句柄上读取校验并用 FileRenameInfo 重命名，拒绝新的写入/删除访问。目标须在相同目录且不同于源，ReplaceIfExists 为 false；已有目标保留，不自动覆盖损坏缓存。

CNG 每个 64KiB 块检查取消，发布前再次检查；不覆盖 rename 是提交边界，取消不回滚已提交包。归档状态只有完整验证及发布成功才为 Ready；父进程还核对子进程成功退出。最终信号丢失可报告失败，下次仍可复用有效缓存。

复用缓存及 Shell 启动前重新校验。校验返回到 ShellExecute 之间仍有路径替换窗口，同步读大包可能阻塞主线程；摘要只能证明字节与已接受元数据一致，不证明发布者身份或签名信任。

## 验证范围

`release_metadata` 检查版本/JSON/摘要及失败输出，`package_acceptance` 使用人工文件检查 CNG、冲突访问、长度/内容、不覆盖发布及篡改拒绝；`download_wire`、`download_job`、`update_private_process`、`update_http_limits` 覆盖私有通信和传输。关闭更新的构建另注册 `update_agent_disabled`，不将它计入默认启用官网更新时的客户端 69 项。

这些测试不执行安装包或操作用户旧缓存；实际安装、升级、签名及真实宿主验收另行记录。结果入口见 [维护与验证](maintenance.md)及 [构建基线](build-baseline.md)。

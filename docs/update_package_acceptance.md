# 更新元数据与安装包接受

2026-10-15：第一版客户端现采用独立的 UpdateNotice 元数据，只检测并打开 GitHub 发布页，参见[更新检测框架](update_detection.md)。本页记录保留的下载/文件校验设施，不代表第一版的客户端流程。

第六批 E1（2026-10-14）只处理非 UI 更新业务。`modules/updates` 提供 `coral::updates` 的发布版本/元数据契约和服务配置；`platform/windows/update_delivery` 提供 Windows 文件接受。下载程序迁入 `apps/update_agent`，设置程序的业务协调器迁入 `apps/settings_app`。原窗口、控件与绘制保持，仅更新必要调用引用。网络调度和旧进度事件/共享 int 协议仍保留，不能将迁移或校验重写等同于全部更新业务独立实现。

## 服务配置

`CORAL_UPDATE_SERVICE_BASE_URL` 为 CMake 配置项，默认空。未配置时，自动检查返回，手动检查通过原提示入口说明没有服务；独立更新程序退出码为 2，发生在产品互斥、日志、进度 IPC 和请求初始化之前。默认不访问旧产品更新地址。配置 HTTPS 主机或基础路径后，仍沿用用户的自动检查设置；这里没有改 UI 开关或定时器。

配置可用主机名、IPv4、可选端口和基础路径，不接受凭据、查询、片段或可注入生成头的字符；此配置尚不支持 IPv6 字面量。检查路径为 `<base>/api/v1/version/latest`，下载位置路径为 `<base>/api/v1/version/getDownloadUrl?Id=<id>`。这些字段与路由是保留的服务适配，真实服务器兼容性未验证。默认三项新增测试都不向服务器发请求；显式配置服务后，disabled-agent 用例不注册。

这只关闭更新业务的缺省请求。在线词库、旧网页和其他模块仍待各自复核，不能称为全程序离线。

## 严格接受契约

版本为四段规范十进制，每段 0..65535，不接受符号、前导零、空段、溢出、尾部或路径字符。比较四个数值，不能用 atoi 前缀或字符串大小代替版本顺序。本机版本读取运行程序旁的 `config/coralpinyinConfig.ini` 的 app/Version，复用已有包元数据读取；不再相信可能指向其他安装的注册表 DisplayVersion。缺失/非法本机版本明确失败，不冒充“最新版本”。

保留 JSON 的 data 信封和字段名称，以下为人工例子：

```json
{"data":{"id":7,"version":"1.2.3.4","fileSize":3,"fileSha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","description":"人工版本"}}
```

id 必须为 1..2147483647 的整数；fileSize 为 1..4294967296 的整数；fileSha256 为恰好 64 个十六进制字符，大小写均转换成 32 字节摘要；description 可省略，最多 16KiB。整体最多 64KiB，UTF-8 完整验证，根/data 的重复键、嵌入 NUL、截断和尾部垃圾拒绝，解析成功后才替换结果。未知扩展字段可以保留，但不参与业务；这不是新二进制数据格式，也没有修改词典/模型格式。两端现在都要求完整元数据，旧不完整响应不兼容。

下载位置响应必须有 data/url 字符串，最多 8KiB，HTTPS 前缀、非空 authority，不含凭据、反斜杠、空白/控制字符或 NUL；实际 URL 连接解析和重定向仍由既有网络库处理。运输层应答内存上限、超时/取消、源验证和重定向策略留给 E2，这些接受限制不代表已限制网络缓冲。

## 文件校验与发布

缓存改为 `%TEMP%\CoralPinyin\updates\`，本次 pending 文件用随机 128 位名称及 CREATE_NEW 预留，ready 文件为 `package_<规范版本>.exe`。不扫描、批量删除或迁移旧产品缓存；失败后只在请求全部停止/等待后清理本次 pending 文件。

Windows 文件层完整验证实际长度和 SHA256，读取出错、不能打开、尺寸不符或摘要失败均不是成功，禁止将两个空 hash 视为一致。发布时以 GENERIC_READ|DELETE 和 FILE_SHARE_READ 打开 pending；读取校验与 FileRenameInfo 重命名使用同一持有句柄，拒绝新的写入/删除访问；不关闭校验对象再通过路径移动其他文件。目标须在相同目录、不能等于源，ReplaceIfExists 为 false。已有目标无论是否有效都保留，发布失败不会发送成功事件；如同版本的旧 ready 包无效，需明确处理该文件后重试，本批没有自动替换 UI。

归档状态只有完整验证并成功发布才是 kReady。UI 复用已有包及真正 Shell 启动前均再次读取校验，不再仅相信文件存在。校验返回到 ShellExecute 之间仍有路径替换窗口；同步读取大包也可能阻塞主线程。真实安装、UI 响应优化、发布者签名/可信来源和最终启动身份绑定未在本批实现；SHA256 只能核对期望字节，不能证明发布者真实性。本批测试不执行任何安装包。

系统接口依据：[BCryptCreateHash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptcreatehash)、[FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info)。不改变第三方源码或许可证。

## 所有权与保留项

DownloadWorker 析构先设置 stopping，再按 release/location/download 的产生顺序取消并等待请求句柄；等回调退出后释放共享内存/事件，最后在正常 main 作用域退出后关闭日志。主线程关闭事件只请求退出，不直接删除工作线程正在写的文件。失败退出码为 1，未配置服务为 2，已有实例/互斥失败为 3，成功发布为 0。网络取消实际时延不在本批证明。

EventToProgress/Success/Failure/Close 与 UpdateProcess 的名称、shared int 布局及旧 UI 轮询保留；没有通过新类名改善旧进度 IPC 的身份、权限、版本关联或跨会话问题。下载进程仍重新查询最新版本，未绑定显示中的 release；并发回调、取消调度及该关联由 E2 继续处理。产品 EXE 名由现有身份清单生成，旧 updater 目录和 PCH 删除。

## 验证

新增 release_metadata 的独立版本/JSON/UTF-8/摘要/重复/失败保留夹具；package_acceptance 的真实人工文件与 CNG 摘要、写访问冲突、错误长度/内容、同句柄新名称发布、目标保留及篡改后再拒绝；update_agent_disabled 实际运行默认独立程序，核对禁用退出码。没有请求旧更新服务、使用正式安装包或操作已有用户缓存。全量构建/默认回归结果登记在 [去重计划](code-separation.md)。真实联网/安装、旧 IPC 授权和下载取消生命周期尚待 E2；UI 与深度 TSF 专项按 [范围调整](code_separation_scope.md) 保留。

## E2 当前状态补充

以上 E1 记录中的旧 IPC、重新查询 latest 和网络上限/取消待办已由 E2 的私有请求、固定发布元数据与专用传输接替；DownloadWorker 删除。文件验证增加逐块取消，提交不回滚。真实服务/安装、发布者真实性与校验到 Shell 路径间隙仍保留。当前定义及测试范围见 [私有更新契约](update_private_channel.md)。

# 构建与演示

## 当前干净基线（2026-10-21）

提交 `d240364` 独立检出后，x64 Release 与 Debug 从空目录完整构建、客户端默认测试各 69/69 通过；系统 Python 3.13.3、自制演示 36 个文件的清单及跨配置校验通过。可选服务端另验 30/30。详见 [基线及复跑命令](build-baseline.md)。下文各迁移批次的旧测试数量和默认值属于历史记录。

已验证 Windows x64、VS 2019、安装的 Windows SDK、Debug 与 Release。README 提供预设命令。依赖在候选内从源码编译；第六批 D 诊断使用 SDK 声明并从系统目录动态加载 DbgHelp，无需旧预编译导入库。系统接口见 [Microsoft 文档](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump)，运行边界见 [本地诊断](local_diagnostics.md)。

ZIP 使用 [miniz 3.1.2](https://github.com/richgel999/miniz/releases/tag/3.1.2)。固定提交、文件来源和 SHA-256 见 `third_party/miniz/UPSTREAM.json`，不再需要原 zip-utils 预编译库。

## 开关

| CMake 变量 | 默认值 | 用途 |
|---|---|---|
| `BUILD_TESTING` | ON | 编译并注册默认测试 |
| `CORAL_BUILD_DEMO_DATA` | ON | 从自制输入生成演示资源 |
| `CORAL_RUNTIME_DATA_DIR` | 空 | 构建时复制的外置运行包 |
| `CORAL_ENABLE_OFFICIAL_DATA_TESTS` | OFF | 额外注册正式数据回归 |
| `CORAL_TEST_DATA_DIR` | 空 | 正式回归的资源根目录 |
| `CORAL_UPDATE_SERVER_SOURCE_DIR` | 客户端仓库同级的 `server-side` | 可选的更新服务端仓库，用于跨仓库契约和服务端测试 |
| `CORAL_ENABLE_UPDATES` | ON | 启用官网更新检查/下载；离线构建显式设为 OFF |
| `CORAL_UPDATE_SERVICE_BASE_URL` | `https://coralime.cn` | 更新服务的 HTTPS 主机/基础路径；关闭更新使用上述独立开关 |
| `CORAL_LEXICON_SERVICE_BASE_URL` | 空 | 在线词库独立 HTTPS 主机/基础路径，空时不创建请求；无旧地址回退 |

只编译代码：

```powershell
cmake -S . -B build/code-only -G "Visual Studio 16 2019" -A x64 -DBUILD_TESTING=OFF -DCORAL_BUILD_DEMO_DATA=OFF
cmake --build build/code-only --config Release
```

外置运行包传 `-DCORAL_RUNTIME_DATA_DIR=C:/path/to/runtime`，该目录包含 `data/` 与 `config/`。配置阶段指出缺少的核心资源；此检查不替代完整清单、格式版本与运行回归。

## 独立服务端仓库

更新服务端单独维护和开源，不放进客户端仓库。默认布局为同一目录下的 `public/`（客户端）与 `server-side/`（服务端）；若检出名称或位置不同，配置时传入 `-DCORAL_UPDATE_SERVER_SOURCE_DIR=C:/path/to/server-repository`。

客户端的 `update_notice` 测试始终运行；找到服务端的 `latest.example.json` 时额外验证示例数据。找到服务端实现和测试时注册 `update_notice_server`，其测试使用本机临时端口。只检出客户端仓库时，会明确提示未接入服务端测试，客户端编译和自身回归不依赖服务端目录。

## 演示验证

默认生成主词典、bigram、pbin 索引、英文、部首、笔画、符号、简繁资源、纠错及字符模型。中间文件位于输出树；模型为合成值，不代表正式质量。

单独运行演示测试：

```powershell
ctest --test-dir build/win-x64-debug -C Debug -R "^demo_engine$" --output-on-failure
```

当前客户端默认注册 69 项测试，包含引擎/协议/配置、UI/TSF、语音/翻译/更新、ZIP 互操作、双进程共享配置隔离和演示引擎。可选服务端接入后增加一个 CTest 项。ZIP 由 Python 独立解压核对 Unicode 名称、CRC、压缩内容、空文件及禁止覆盖。隔离检查让两个进程同时持有共享配置并检查内容不串扰。

正式数据测试也由隔离入口启动，不要绕过入口直接运行旧程序。第 4 步已验证外置数据链、安装端索引一致性和本地未签名安装包编译；x86 只验证 TSF 目标。正式配置门闸回归仍失败，安装程序未执行，网络行为与正常 DLL 静态析构未验证。完整脚本用法见 [数据工具](data-tools.md)。已有英文精确候选受 isShowInfos 影响，演示明确验证开启该开关的分支，见候选状态。

更新程序 target 已迁为 coral_update_agent（产物名由身份清单保留），默认无更新请求；新契约与限制见 [更新接受](update_package_acceptance.md)。配置服务属于构建选择，不能只修改地址便声称实际联网/安装与旧 IPC 已验收。

第六批 E2 的 coral_updates 新增 CUP1 与同步下载流程，coral_update_delivery 新增私有进程/job 与更新专用 Curl multi；coral_update_agent 不再依赖旧网络调度/全局事件。服务仍默认空，真实 HTTPS、安装与签名验证未执行；见 [私有更新契约](update_private_channel.md)。

文本导入实现由 coral_lexicon_io（coral::lexicon_io）独立静态目标构建，设置程序和根构建的导入回归链接该目标；单独构建 tests/lexicon_import_dialog 使用相同实现源，不依赖设置PCH。默认测试仍51项，未增加仅改名测试。见 [文本导入](lexicon_text_import.md)。

coral_lexicon_catalog（coral::lexicon_catalog）提供在线词库服务配置/路由；OnlineLexiconCoordinator仍由设置程序编译，保留原词库算法/PCH依赖。配置服务只允许该管理器使用新地址，不自动注册定时器/界面入口；旧下载运输与记录事务未完成，不能只填地址便认定联网可用。见 [在线词库边界](online_lexicon_catalog.md)。

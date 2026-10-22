# 珊瑚输入法（Coral Pinyin）

珊瑚输入法是一款面向 Windows 的拼音输入法，由个人开发者 Coral-IME 维护。普通拼音输入、候选生成和用户学习在本机完成。

我们力求用最低的磁盘内存占用实现最优的候选词推荐

[官网](https://coralime.cn/) · [源码仓库](https://github.com/Coral-IME/Coral-IME) · 联系邮箱：coralime@126.com

## 程序功能

| 功能 | 说明 |
|---|---|
| 拼音输入 | 支持全拼、简拼和双拼，可选择双拼方案、模糊音与拼音纠错设置。 |
| 候选与学习 | 支持候选翻页、用户词库学习、候选固定和删除，以及以词定字。 |
| 智能预测 | 根据连续输入的中文上下文进行整词续接，可设置跨次关联强度。 |
| 中英混输 | 支持英文词查询、补全及中英文混合组句。 |
| 特殊输入 | 提供 U 模式部首/笔画输入，以及 V 模式计算、日期时间等辅助输入；支持符号和表情候选。 |
| 输入设置 | 可配置简繁体、全半角、中英文标点、快捷键、候选数量、字体和主题。 |
| 剪贴板历史 | 保存并选择本机剪贴板历史内容，默认开启，内容以明文保存在本机。 |
| 翻译 | 默认关闭；用户配置自己的阿里云凭据后，可通过快捷键主动翻译候选内容并选择译文上屏。 |
| 自动更新 | 默认开启官网 HTTPS 更新检查，可在“关于”中关闭。 |

当前产品的语音入口屏蔽，在线词库服务默认未配置而禁用。崩溃诊断仅在本地保存，当前没有内置上传入口。联网与本地数据处理的详细说明见 [隐私政策](docs/legal/隐私政策.md)。

## 下载与自动构建

安装包见 [Releases](https://github.com/Coral-IME/Coral-IME/releases)，请阅读各版本说明区分测试包、演示包和正式版；GitHub 自动提供的 Source code 压缩包不是安装程序。

[Windows 自动构建](https://github.com/Coral-IME/Coral-IME/actions/workflows/windows.yml)检查 x64 Debug/Release、运行默认测试、编译 x86 文本服务组件，并生成未签名演示安装包。演示包仅含自制小词库，关闭自动更新，不代表正式输入质量。下载、Release 草稿和实际安装验收步骤见 [自动构建说明](docs/github-actions.md)。

维护者可手动运行[正式词库测试包工作流](https://github.com/Coral-IME/Coral-IME/actions/workflows/official.yml)，从独立私有仓库读取固定版本数据，核验正式资源后生成测试安装包。数据边界、下载及验收见[正式词库打包说明](docs/official-data-release.md)。

[Code signing policy](docs/code-signing-policy.md)：SignPath Foundation 申请阶段，尚未获批；当前安装包未签名，正式数据的开源与签名资格仍待确认。

## 工程与开发

### 目录说明

下列路径均相对于包含 `CMakeLists.txt` 的客户端仓库根目录。

| 路径 | 作用 |
|---|---|
| `include/coral/` | 按领域组织的公共 C++ 接口、协议类型和产品标识。 |
| `modules/` | 引擎与业务模块，包括拼音解析、组句评分、用户学习、候选规则、设置和特殊输入等。 |
| `platform/` | 平台适配。`windows/` 包含引擎宿主、会话客户端、TSF 会话桥接、服务启动、文件映射、网络与凭据处理；`posix/` 提供部分资源后端，不代表完整产品支持该平台。 |
| `apps/` | 设置程序的业务协调，以及启动服务、更新代理和本地诊断采集程序。 |
| `src/platform/windows/` | Windows 产品入口和界面：`coralpinyin/` 为主程序及设置，`coraltsf/` 为 TSF 文本服务，`renderer/` 为候选窗与悬浮栏；另含界面公共代码、IPC 和共享内存工具。 |
| `src/common/` | 产品共用的辅助代码和资源。 |
| `src/lib/`、`third_party/` | 随源码构建的第三方依赖及来源、许可清单。 |
| `src/script/` | 转发至 `tools/` 的兼容脚本入口；开发时直接使用 `tools/`。 |
| `resources/` | 公共配置模板、双拼方案、图标和 `product_identity.json` 产品运行标识。 |
| `samples/demo/` | 人工编写的演示词条、合成模型及演示资源生成与加载验证。 |
| `tests/` | 按功能划分的自动测试，由 CTest 统一运行。 |
| `tools/native/` | C++ 开发工具，包括词库编译、数据包解码、资源编译、引擎交互调试和解析性能采样。 |
| `tools/data/` | 数据整理、词库/模型生成、运行资源构建和清单校验。 |
| `tools/checks/`、`tools/diagnostics/` | 构建回归、隔离运行、产品标识检查与诊断脚本。 |
| `tools/release/`、`packaging/windows/` | Windows 发布文件收集、NSIS 安装包和签名脚本。 |
| `cmake/` | 公共编译选项、输出布局、产品标识与资源复制规则。 |
| `docs/` | 功能说明、模块契约、数据格式、构建及许可文档。 |
| `build/` | 本机构建输出，包括程序、调试符号、演示资源及测试临时数据，不纳入版本控制。 |

核心模块按职责进一步划分：

| 模块 | 职责 |
|---|---|
| `foundation`、`preferences` | 日志、基础工具和配置管理。 |
| `composition`、`runtime` | 输入会话状态机、引擎编排与进程内操作适配。 |
| `phonetics`、`language_model` | 拼音/英文解析检索、组句及语言模型评分。 |
| `storage` | 词典、模型和索引的存储格式及资源访问。 |
| `learning`、`ranking` | 用户学习历史、候选固定/删除与排序规则。 |
| `assist` | 部首、笔画、数值、日期时间和符号等特殊输入。 |
| `transport` | 会话请求与响应的字节协议。 |
| `translation`、`updates`、`lexicon_catalog` | 翻译、版本更新与在线词库配置及业务契约。 |
| `speech` | 语音识别基础模块；当前产品入口屏蔽。 |

更新服务端单独维护。客户端可以独立编译；如需跨仓库契约测试，可通过 `CORAL_UPDATE_SERVER_SOURCE_DIR` 指向服务端源码，默认查找同级 `server-side/`。

### 开发环境

- Windows，Visual Studio 2019，安装 C++ 桌面开发工具及 ATL/MFC 组件。
- Windows SDK；工程默认 SDK 版本为 `10.0.18362.0`，可通过 `CMAKE_SYSTEM_VERSION` 指定已安装版本。
- CMake 3.21 或更新版本。
- Python 3.9 或更新版本；默认演示及测试脚本仅使用标准库。完整数据流水线的额外依赖见 `tools/data/requirements.txt`。

工程使用 C++17，预设固定采用 Visual Studio 2019 生成器，提供 x64 和 Win32 构建配置。当前整套产品的构建平台为 Windows。

### 编译与测试

在客户端仓库根目录打开 PowerShell，依次执行：

```powershell
# Debug：日常开发与断点调试
cmake --preset win-x64-debug
cmake --build --preset win-x64-debug
ctest --preset win-x64-debug

# Release：发布配置下的构建与回归
cmake --preset win-x64-release
cmake --build --preset win-x64-release
ctest --preset win-x64-release
```

默认构建会从 `samples/demo/fixtures/` 生成人工演示资源，无需正式词库和模型。演示仅用于验证资源格式、加载和基本输入路径，不代表正式输入质量。

| 输出路径 | 内容 |
|---|---|
| `build/<预设名>/coralpinyin/<Debug或Release>/` | EXE、TSF DLL、原生工具及 PDB 调试符号。 |
| `build/<预设名>/lib/` | 静态库，按配置分目录。 |
| `build/<预设名>/demo-runtime/` | 演示资源根目录，包含 `data/`、`config/` 及校验清单。 |
| `build/<预设名>/test-users/` | 隔离测试的用户数据。 |

需要 Win32 TSF 时：

```powershell
cmake --preset win-x86-release
cmake --build --preset win-x86-release --target coraltsf32
```

查看已注册测试或只运行演示加载测试：

```powershell
ctest --test-dir build/win-x64-debug -C Debug -N
ctest --test-dir build/win-x64-debug -C Debug -R "^demo_engine$" --output-on-failure
```

测试数量随构建选项及是否接入独立服务端变化，以当前构建树的清单为准。构建与默认测试不会安装输入法；TSF 在真实应用中的输入、安装和升级需要另外验证。

### 开发与调试

配置完成后，在 Visual Studio 中打开 `build/win-x64-debug/coralpinyin.sln`，选择 Debug 和对应目标设置断点。修改代码后也可只重建所需目标：

```powershell
cmake --build --preset win-x64-debug --target coral_engine_console
```

排查拼音解析、候选生成或用户学习时，先通过控制台直接驱动引擎：

```powershell
$env:CORAL_TEST_NAMESPACE = "console_" + [guid]::NewGuid().ToString("N")
$env:CORAL_USER_DATA_DIR = Join-Path (Get-Location).Path ("build/console-users/" + $env:CORAL_TEST_NAMESPACE)
New-Item -ItemType Directory -Force -Path $env:CORAL_USER_DATA_DIR | Out-Null
.\build\win-x64-debug\coralpinyin\Debug\coral_engine_console.exe .\build\win-x64-debug\demo-runtime 7
```

输入 `nihao` 等拼音查看候选；`:n` / `:p` 翻页，`:s 1` 选择当前页第一个候选，`:r` 重置，`:q` 退出。控制台使用演示目录下的 `console-scratch/`，每次启动会重建该目录；同一演示目录一次运行一个控制台。断点调试时，将同一资源目录的绝对路径及页大小设为程序参数，并在项目调试环境中设置独立的 `CORAL_TEST_NAMESPACE` 和 `CORAL_USER_DATA_DIR`。

调试 TSF、候选窗或会话通信时，分别关注 `coraltsf32` / `coraltsf64`、`coral_renderer` 和 `coral_engine_host` 目标，附加到实际加载 DLL 或承载相应功能的进程，并匹配进程位数与 PDB。真实 Windows 界面检查见 [系统界面回归清单](docs/windows-system-ui-regression.md)。解析性能采样见 [基准工具说明](tools/native/parse_bench/README.md)。

### 资源与常用构建选项

源码与运行数据分开管理。正式词库、模型和语料需要单独提供；外置运行包应包含配套的 `data/` 与 `config/`。公共模板在 `resources/config/`。

| CMake 变量 | 用途 |
|---|---|
| `CORAL_RUNTIME_DATA_DIR` | 指定构建时复制的外置运行资源根目录。 |
| `CORAL_BUILD_DEMO_DATA` | 是否生成演示资源，默认 `ON`。 |
| `BUILD_TESTING` | 是否构建并注册测试，默认 `ON`。 |
| `CORAL_ENABLE_OFFICIAL_DATA_TESTS`、`CORAL_TEST_DATA_DIR` | 启用额外正式数据测试并指定资源根目录，默认不启用。 |
| `CORAL_APP_VERSION` | 显式指定四段式程序版本；打包版本须与其一致。 |
| `CORAL_ENABLE_UPDATES` | 是否编译更新功能，默认 `ON`；离线构建设为 `OFF`。 |
| `CORAL_UPDATE_SERVICE_BASE_URL` | 更新服务地址，默认 `https://coralime.cn`。 |
| `CORAL_LEXICON_SERVICE_BASE_URL` | 在线词库服务地址，默认空。 |

例如，使用外置运行包构建：

```powershell
cmake --preset win-x64-debug -DCORAL_RUNTIME_DATA_DIR=C:/data/runtime
cmake --build --preset win-x64-debug
```

仅编译代码时，可在配置命令中追加 `-DBUILD_TESTING=OFF -DCORAL_BUILD_DEMO_DATA=OFF`。改变这些选项后，需要重新配置对应构建树。

开发规范见 [DEVELOPMENT.md](DEVELOPMENT.md)；更多资源生成、验证和安装包收集方法见 [构建说明](docs/build.md)与 [数据工具](docs/data-tools.md)。

### 许可

项目有权授权的代码、工具及自制演示采用 MPL-2.0，第三方组件按各自许可，正式词库、模型和语料独立管理。保留已有作者与许可声明。详见 [LICENSE](LICENSE)、[许可范围](LICENSING.md)和 [第三方通知](THIRD_PARTY_NOTICES.md)。用户使用条款见 [用户协议](docs/legal/用户协议.md)和 [许可说明](docs/legal/许可说明.md)。

# 第三方来源与维护

2026-10-21 基础复核：当前清单含 21 项组件/许可范围、2,520 个唯一文件及 23 份许可文本，哈希和路径核对一致；完整结论及来源限制见 [复核记录](source-license-review.md)。

完整通知在根目录 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)，机器可核对清单在 [third_party/manifest.json](../third_party/manifest.json)。每个保留组件记录本地文件 SHA-256、版本证据、用途和许可文本；下载的许可还记录固定 URL、提交及校验值。

部分历史源码没有保留导入提交。这里采用本地源码快照作为准确身份，不将“版本宏相同”或“补齐同名许可”说成与上游某个版本完全一致。引入的 CMake 定义、Windows 适配和既有文件改动也不能被称为未修改的上游发行包。

库目录中保留的上游 README 和文档描述上游完整工程，可能包含本候选没有附带的工具、平台或数据。它们用于上游背景参考；本候选的实际构建、数据和安装入口以根 README 及 docs 中的维护文档为准。

MARISA 按其 BSD-2-Clause 选项分发，保留原双许可说明；OpenCC、parallel-hashmap/Abseil 和 PacketVideo 文件保留 Apache-2.0；WTL 使用 MS-PL，WIL 使用 MIT。完整许可须以组件附带文本为准。OpenCC 自带字典与项目正式数据另行管理，未放入公开候选。

微软 SDK、ATL/MFC、系统 DbgHelp 以及 Python 模型工具依赖由构建者自行准备，候选不带这些二进制。NSIS 编译器同样外置；当前安装脚本使用 zlib 压缩，并随安装包带其相关许可。更换 NSIS 插件或压缩模块时须重新核对适用声明。[NSIS 官方许可说明](https://nsis.sourceforge.io/Docs/AppendixI.html)区分了各压缩模块。

已无当前源码引用的 BBHash、旧 Windows 线程池、LRU 辅助头和 WebView2 SDK 头文件已从候选移到私有归档，原件保留。旧 zip-utils 已由 miniz 替换，旧 DbgHelp 预编译文件未进入候选。

## 目录依赖核对（2026-10-12）

`common/thirdparty/` 中的 `bbhash/` 已为空且没有源码或构建引用，已移除空目录。其余组件均有实际引用：

| 目录 | 当前用途与主要入口 |
|---|---|
| `hash-library` | 引擎资源的 MD5 校验，`modules/foundation/text_file_helpers.hpp`；第六批 D 的离线诊断不再依赖 MD5 或设备标识 |
| `parallel_hashmap` | 引擎词典、语言模型及专用缓存，`modules/phonetics/lexicon_source.h`、`context_scorer.h` |
| `simpleini` | INI 配置、版本与双拼方案，`modules/preferences/ini_store.h`、`package_version.h`、`layout_metadata.h` |
| `xxhash` | 共享配置键哈希，`modules/preferences/shared_preferences.cpp` |

`lib/` 下六个库均有使用，保留如下：

| 目录 | 当前用途与主要入口 |
|---|---|
| `boost` | 配置和旧共享布局诊断，`modules/preferences/shared_preferences.cpp`、`tools/native/history_inspector/main.cpp` |
| `curl` | 网络与上传，`platform/windows/common/nanet` 的 `coralnet` 目标静态链接 `libcurl_static` |
| `marisa-trie` | 英文、部首词典及索引生成，`modules/phonetics/latin_lexicon.cpp`、`tools/native/lexicon_builder`，并被 OpenCC 使用 |
| `opencc` | 简繁转换，`modules/phonetics/script_transform.hpp`、`script/script_conversion.cpp` |
| `spdlog` | 日志后端，`modules/foundation/trace_log.cpp` 的 `coral_trace` 目标 |
| `xz-embedded` | 安装期词典包解压，`tools/native/bundle_decoder` 目标直接编译解码源码 |

OpenCC 内的 RapidJSON 用于配置解析。TCLAP 与 Darts-clone 虽不参与当前默认目标，仍分别被保留的 `CmdLineOutput.hpp` 和可选 `DartsDict.cpp` 源码引用，本次保留；删除它们需要一并裁剪上游可选源码及其构建入口。

项目的 [MPL 说明](../LICENSING.md)只覆盖自有文件，不覆盖上述组件；Mozilla 对文件级许可和独立文件的说明见 [官方 FAQ](https://www.mozilla.org/en-US/MPL/2.0/FAQ/)。

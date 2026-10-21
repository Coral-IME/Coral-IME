# 珊瑚输入法（Coral Pinyin）

开发前请阅读 [开发准则](DEVELOPMENT.md) 与 [代码去重计划](docs/code-separation.md)。当前维护范围见候选状态；词典相关算法按维护者要求保留。

面向 Windows 的拼音输入法，由个人开发者 Coral-IME 维护。普通拼音输入、候选生成和学习在本地完成；代码、数据和工具已分离，提供可独立构建的自制演示。

官网：https://coralime.cn/ · 联系/隐私邮箱：coralime@126.com · 计划公开源码：https://github.com/Coral-IME/Coral-IME。当前处于开源与正式发行准备阶段，本轮不改变仓库可见性；已提供的下载测试包不等同于当前源码的正式发行。

正式词库、概率表、模型、评测语料及原 Git 历史在候选之外。演示输入由人工构造，见 `samples/demo/fixtures/`，采用 MPL-2.0；词频和模型统计是合成值，仅用于验证格式与加载。

## 构建与测试

需要 Windows、Visual Studio 2019 C++ 工具（含 ATL/MFC）、Windows SDK、CMake 3.21 或更新版本、Python 3.9 或更新版本。Python 演示及测试脚本仅使用标准库。

在候选根目录执行：

```powershell
cmake --preset win-x64-debug
cmake --build --preset win-x64-debug
ctest --preset win-x64-debug

cmake --preset win-x64-release
cmake --build --preset win-x64-release
ctest --preset win-x64-release
```

当前源码默认版本为 **1.0.0.3**，可用 `CORAL_APP_VERSION` 显式覆盖；已有构建缓存和历史测试包版本需分别核对。默认注册 **69 项客户端测试**，接入独立服务端后合计 **70 项**。

2026-10-21 从提交 `d240364` 独立检出、使用空构建目录，Windows x64 Debug/Release 完整构建通过，客户端默认测试各 **69/69** 通过；可选服务端另验 30/30。环境、复跑方式及源码快照见 [当前构建基线](docs/build-baseline.md)。本轮未重跑 x86，完整 x86 产品和实际安装仍未验证。

默认生成 `build/<预设名>/demo-runtime/`，含 `data/`、`config/` 和 SHA-256 清单。产品输出位于同一构建树的 `coralpinyin/<配置>/`。演示测试显式选择资源路径、独立用户目录及共享内存命名空间，默认不安装输入法。

公共配置位于 `resources/config/`。可通过 `CORAL_RUNTIME_DATA_DIR` 显式选择外置运行包；只编译代码可关闭 `CORAL_BUILD_DEMO_DATA` 与 `BUILD_TESTING`。详见 [构建与演示](docs/build.md) 和 [代码与数据边界](docs/data-boundary.md)。

ZIP 从固定版本 miniz 源码编译；诊断使用 Windows SDK 声明与系统 DbgHelp DLL。第三方保留原声明，完整许可和来源清单见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)；较早源码的原导入提交未记录，采用本地文件校验值明确身份。

数据、检查与打包入口已整理，详见 [脚本用法](docs/data-tools.md)。本地演示及正式数据安装包已编译，但未执行安装或签名。正式配置门闸和主索引差异仍需当前版本复核；公开文档、许可和隐私文本已统一；生产保留期限、官网部署及正式发布验收仍须落实，见 [候选状态](CANDIDATE_STATUS.md)。

当前维护入口：[架构](docs/architecture.md)、[维护与验证](docs/maintenance.md)、[第三方来源](docs/third-party.md)。历史脚本与设计过程文档已私有归档，公开候选不包含旧提交或旧 golden 集。

全新获取与发布检查见 [源码检出与发布准备](docs/source-reproducibility.md)。

## 许可与数据处理

项目有权授权的代码、工具和自制演示采用 MPL-2.0，第三方按各自许可；正式词库、模型和语料独立管理。保留所有既有作者及许可声明，公开署名 Coral-IME 不取代其他权利人。见 [许可范围](LICENSING.md)与 [面向用户的许可说明](docs/legal/许可说明.md)。

自动更新默认开启，可在“关于”关闭；云翻译默认关闭，由用户自行配置阿里云凭据并主动触发。剪贴板历史默认开启且明文保存在本机；崩溃诊断仅本地保存，当前无内置上传入口。语音入口屏蔽，在线词库默认禁用。软件不能整体称为“不联网的离线版”。

[用户协议](docs/legal/用户协议.md)和 [隐私政策](docs/legal/隐私政策.md)首次正式发布时生效；官网/更新服务采用维护者确认的阿里云杭州，开发者控制的服务端日志及反馈邮件最多保存一年。本地数据按其独立规则保存和清理。统一信息与仍需在发行时落实的事项见 [公开发行信息](docs/release-info.md)。

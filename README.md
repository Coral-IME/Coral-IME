# coral-pinyin：开源候选

开发前请阅读 [开发准则](DEVELOPMENT.md) 与 [代码去重计划](docs/code-separation.md)。当前进行目录、命名和基础设施的分批迁移；词典相关算法按维护者要求保留。

Windows 拼音输入法的本地开源候选。已完成源码、数据和工具的整理：公开代码可独立构建，数据、检查及打包脚本使用显式输入输出，并提供自制小样本流水线。已建立全新的本地候选 Git，尚未对外发布。

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

2026-10-21 从提交 `d240364` 独立检出、使用空构建目录，Windows x64 Debug/Release 完整构建通过，客户端默认测试各 **69/69** 通过；可选服务端另验 30/30。环境、复跑方式及源码快照见 [当前构建基线](docs/build-baseline.md)。本轮未重跑 x86，完整 x86 产品和实际安装仍未验证。

默认生成 `build/<预设名>/demo-runtime/`，含 `data/`、`config/` 和 SHA-256 清单。产品输出位于同一构建树的 `coralpinyin/<配置>/`。演示测试显式选择资源路径、独立用户目录及共享内存命名空间，默认不安装输入法。

公共配置位于 `resources/config/`。可通过 `CORAL_RUNTIME_DATA_DIR` 显式选择外置运行包；只编译代码可关闭 `CORAL_BUILD_DEMO_DATA` 与 `BUILD_TESTING`。详见 [构建与演示](docs/build.md) 和 [代码与数据边界](docs/data-boundary.md)。

ZIP 从固定版本 miniz 源码编译；DbgHelp 使用 Windows SDK 导入库与系统 DLL。第三方保留原声明，完整许可和来源清单见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)；较早源码的原导入提交未记录，采用本地文件校验值明确身份。

数据、检查与打包入口已整理，详见 [脚本用法](docs/data-tools.md)。本地演示及正式数据安装包已编译，但未执行安装或签名。正式配置门闸回归仍有失败，新旧 mainlexicon.coral 产物有路径差异；法律文档、联网行为及发布检查待后续整理，见 [候选状态](CANDIDATE_STATUS.md)。

当前维护入口：[架构](docs/architecture.md)、[维护与验证](docs/maintenance.md)、[第三方来源](docs/third-party.md)。历史脚本与设计过程文档已私有归档，公开候选不包含旧提交或旧 golden 集。

全新获取与发布检查见 [源码检出与发布准备](docs/source-reproducibility.md)。

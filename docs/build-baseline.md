# 当前版本干净构建与测试基线

日期：2026-10-21。验证提交：`d24036496c19805b0a9b2de49efdd02a3b4fd3ba`。

## 结果

| 检查 | 结果 |
|---|---|
| 独立检出 | 本地 `clone --no-hardlinks`，3,425 个跟踪文件与原提交逐字节一致；构建前后工作树均干净 |
| x64 Release | 空构建目录完整构建通过；客户端默认测试 69/69，通过用时 83.00 秒 |
| x64 Debug | 另一空目录按 `win-x64-debug` 预设完整构建通过；客户端默认测试 69/69，通过用时 106.23 秒 |
| 自制演示 | 两种配置分别生成 36 个运行资源，逐项大小/SHA-256 校验通过；输入与生成文件跨配置完全一致 |
| 可选服务端 | 独立服务端提交 `3190d5927783144ef8afdc9b1a420d076da599fd` 的 30 项测试通过；新 Release 客户端解析服务端公开示例通过 |
| 源码 ZIP | 由上述客户端提交 `git archive` 导出，3,425 个文件与独立检出逐字节一致；不含 Git、构建、私有数据或审计目录 |

源码 ZIP SHA-256：`fe7ab73dea031f325dd7c89c4dffd92702e05635ee3f2b289f6b4c230641b0e7`。ZIP 和包含机器路径的原始日志/输出哈希保存在维护者私有审计区，不作为仓库源码文件提交。

## 环境和范围

Windows 10.0.26200 x64；Visual Studio 2019、MSVC 19.29.30159.0、Windows SDK 10.0.19041.0；CMake 3.31.11；系统 Python 3.13.3。显式指定 Python 解释器，测试使用本轮独立可写临时目录；没有调整访问策略、修改断言、删除或跳过失败用例。

`BUILD_TESTING=ON`、`CORAL_BUILD_DEMO_DATA=ON`；正式资源路径为空，正式数据测试关闭。更新配置沿用源码默认值，测试使用人工数据/自有本机服务，不进行公网更新、云端翻译或真实录音。

独立客户端检出没有同级服务端，因此默认是 69 项；接入服务端时增加一个 `update_notice_server` CTest 项，内部运行服务端单元测试。本轮服务端单独运行，不将它合并冒称为客户端 70/70。

此前本机构建树的 62/70 和失败诊断记录保留。改用独立检出、新建缓存及显式系统 Python 后，当前客户端全部通过；这证明当前源码在所记录环境可通过，不证明旧构建所有失败均有相同根因。编译仍有既有类型转换警告，不宣称零警告或跨工具链二进制逐字节复现。

## 复跑方式

从当前本地客户端仓库根目录执行，下列命令会创建同级新目录；已有同名目录时应另选名称，不能覆盖旧基线。

```powershell
$sourceRepository = (Get-Location).Path
$pythonExe = (Get-Command python.exe -CommandType Application).Source
New-Item -ItemType Directory -Path ../coral-clean | Out-Null
git -c core.autocrlf=false clone --no-hardlinks "$sourceRepository" ../coral-clean/src
Set-Location ../coral-clean/src
git checkout --detach d24036496c19805b0a9b2de49efdd02a3b4fd3ba
New-Item -ItemType Directory -Path build/test-temp -Force | Out-Null
$env:TEMP = (Resolve-Path build/test-temp).Path
$env:TMP = $env:TEMP

cmake -S . -B build/x64 -G "Visual Studio 16 2019" -A x64 "-DPython3_EXECUTABLE=$pythonExe"
cmake --build build/x64 --config Release --parallel 2 -- /nr:false
ctest --test-dir build/x64 -C Release --output-on-failure

cmake --preset win-x64-debug "-DPython3_EXECUTABLE=$pythonExe"
cmake --build --preset win-x64-debug --parallel 2 -- /nr:false
ctest --preset win-x64-debug --output-on-failure
git status --short
```

使用具备正常文件、注册表和命名对象权限的 Windows 会话，并确认所选 Python 可以处理测试临时夹具。服务端另行检出后，可显式传 `CORAL_UPDATE_SERVER_SOURCE_DIR` 接入；客户端自身的 69 项不依赖它。

## 适用边界

本基线只对应上述提交；本轮随后新增的基线报告、路径登记及构建文档是文档改动，不作为已经再次干净检出的新提交。后续运行代码、构建规则或依赖变化后应重新建立基线。

本轮没有安装、签名、替换现用输入法或对外发布，没有验证正式词库质量、正式配置门闸及真实宿主输入体验；未重跑 x86/ARM64 或其他系统。来源/授权限制和旧 AI 令牌历史问题继续独立处理。历史源码 ZIP、测试数量和安装包记录不自动代表当前版本。

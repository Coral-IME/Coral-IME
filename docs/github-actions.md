# GitHub 自动构建与演示安装包

工作流为 `.github/workflows/windows.yml`，仅使用 GitHub 托管环境，不需要维护者配置个人令牌或签名密钥。

## 自动执行的内容

向 `main` 提交代码或创建目标为 `main` 的拉取请求后，自动执行：

1. 在 Windows Server 2022 / Visual Studio 2022 环境分别编译 x64 Debug、Release，并运行各自的默认 CTest；零项测试会失败。
2. 独立编译 Win32 Release 的 `coraltsf32`，供 64 位 Windows 上的 32 位应用使用，不代表支持 32 位 Windows 安装。
3. 使用本轮 Release 生成工具和公开人工夹具重建配套演示运行资源，检查清单及安装端重建结果。
4. 使用标准 NSIS 生成未签名演示安装包，保存源码提交、构建链接、文件清单和 SHA-256。打包入口传入 `--demo`，安装器标为演示版，安装后由用户从开始菜单打开，不依赖额外的安装后启动插件。

演示构建关闭自动更新，避免演示安装连接正式更新渠道。默认程序版本为 `0.0.0.0`，不是当前正式源码版本的发行声明。现有本地 VS 2019 预设保持可用，CI 独立使用 VS 2022 和镜像已安装的 Windows SDK。检出前关闭 Git 自动换行转换，保留源码和第三方许可证的原始字节及摘要。

## 宿主旧运行库兼容检查

TSF 会使用宿主进程已加载的 MSVC 运行库。部分 Python/Qt 应用自带较旧的 `MSVCP140.dll`，新版 MSVC 默认生成的 mutex 初始化布局会与其不兼容，在激活入口加锁时读取空地址。所有 MSVC 编译单元统一定义 `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`，包括 TSF、静态依赖、第三方源码和预编译头，以保持构造路径一致。仅修改某个源文件或回退本机编译器不能覆盖云端发行构建。

默认 CTest 的 `msvc_mutex_runtime` 检查静态 mutex、对象成员 mutex、跨线程互斥与递归加锁。Release CI 另从 Microsoft 符号服务器下载固定的 x64 MSVCP140 14.26.28720.3，校验大小和 SHA-256 后，在独立临时目录中运行同一个探针，并核对进程实际加载路径。未启用兼容构造的新版工具链对照必须产生 `0xC0000005`，兼容构建必须完成全部加锁操作；对照程序不进入发行二进制 artifact。结果写入 `build/ci-x64/mutex-compat/report.json`，随 Release 日志保留。

本机可使用已取得的同一运行库进行检查，不改系统运行库或已安装的输入法：

```powershell
python tools/checks/check_msvc_mutex_compat.py --probe build/win-x64-release/coralpinyin/Release/msvc_mutex_runtime_probe.exe --runtime <旧版MSVCP140.dll路径> --work-dir build/mutex-compat
```

该检查验证实际编译的 C++ mutex 与旧运行库的兼容行为。真实应用中的 TSF 激活、输入、安装和升级仍需单独验收。Microsoft 对该兼容开关的说明见 [STL 17.10 发布说明](https://github.com/microsoft/STL/releases/tag/vs-2022-17.10)。

## 下载测试产物

1. 打开仓库的 **Actions → Windows CI and demo installer**。
2. 选择绿色成功的运行记录，在 **Artifacts** 下载 `coral-pinyin-demo-installer`。
3. 解压后读取 `RELEASE_NOTES.md`，安装文件为 `coralpinyin-demo-windows-x64-<版本>.exe`。
4. 使用 PowerShell 的 `Get-FileHash <安装包路径> -Algorithm SHA256` 与 `SHA256SUMS.txt` 对照。

Actions 产物保留 14 天，下载通常需要登录 GitHub；供普通用户持续下载的版本应发布至 Releases。中间二进制保留 7 天，失败日志保留 14 天。

## 创建 Release 草稿

工作流合入 `main` 后：

1. 在上述 Actions 页面选择 **Run workflow**，分支选 `main`。
2. 填写四段式程序版本（每段 0–65535，不带前导零），例如 `0.0.0.0`。
3. 勾选 `create_draft` 后运行。必须等全部编译、测试和安装包生成成功，才会创建草稿。
4. 在 **Releases** 检查标题、说明和附件。标签采用 `v<版本>-demo.<运行编号>`，对应本次源码提交；草稿设为预发布且不设为最新正式版本。
5. 在独立虚拟机完成下述验收后，可将草稿发布为演示预发布版。公开后，访客可通过 Releases 下载附件，无需 Actions 下载权限。

同一次运行重跑如果已有同名草稿会失败，不覆盖已有附件；要重新出包，请发起新的运行。非 `main` 手动运行只保留构建产物，不创建草稿。未勾选 `create_draft` 时不创建 Release。

## 维护者需要执行的步骤

- 如果 Actions 页面提示尚未启用工作流，启用该仓库的 Actions；如果账户策略阻止官方 Actions，按提示允许本工作流使用的 GitHub 官方 Actions。
- 演示流水线无需个人访问令牌或 Secrets。仅创建草稿的任务申请 `contents: write`，其余任务只读源码。
- 在独立虚拟机验证安装、32/64 位应用中的基本输入、退出、重启、升级与卸载。演示包沿用正式产品的安装标识，不应覆盖日常使用的版本。流水线不会在维护者电脑上自动安装。
- 演示数据规模很小，不用它评价正式候选质量，也不将其标为正式发行版本。

## 后续正式数据及签名

正式词库和模型由独立的主分支手动工作流读取私有仓库，固定版本、逐文件核验并与本次生成工具配对，见[正式词库打包说明](official-data-release.md)。本演示流水线不读取私有数据。

默认标准打包模式保留既有的安装后启动逻辑，需要额外的 ShellExecAsUser 插件。正式词库测试工作流使用 `--manual-launch`，保留正常产品名称，安装后从开始菜单打开，不依赖该插件。演示模式继续标为演示版。

SignPath 的申请、数据许可是否符合免费计划、签名审批人和账户双重验证需另外落实。获批后再接入托管签名，按最终签名文件重新生成摘要。当前草稿和构建产物不宣称拥有受信任签名或完成实际安装验收。

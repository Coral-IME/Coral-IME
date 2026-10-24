# 正式词库测试安装包

`Official dictionary test installer` 是主分支手动工作流。它与默认 Windows CI 共用只读的 `windows-build.yml`，执行 x64 Debug/Release 编译和测试、x86 文本服务组件构建，再生成含正式词库的未签名测试安装包。普通提交和公开 PR 的自动构建继续只使用公开演示夹具。仅创建或发布 Release 的独立任务申请写入 Release 的权限。

## 数据和访问权限

正式输入保存在独立私有仓库 `Coral-IME/Coral-IME-data`。`resources/official-data.json` 固定完整提交、数据版本、归档摘要和逐文件清单摘要；不读取浮动分支作为发行数据。数据包含中文及英文词库、语言模型输入、U 模式输入、转换资源和两个既有编译模型。模型明确复用，未重新训练。

私有仓库的只读部署密钥对应公开仓库 Actions Secret `OFFICIAL_DATA_SSH_KEY`。该密钥只允许读取一个数据仓库；不授予修改数据或访问其他仓库的权限。检出不保留凭据。只有受信任 `main` 上手动运行的打包任务读取此 Secret，复用的普通检查任务不接收它。修改主分支发行脚本相当于修改能读取数据的受信任代码，维护者应审阅这些变更。

公开安装包必然包含运行所需的编译数据；原始 CSV、模型输入归档、私有配置和用户记录不会成为源码、Actions 附件或 Release 附件。词库和模型继续适用各自原有许可；公开源码 MPL-2.0 不自动覆盖这些数据。

## 生成与下载

1. 进入 **Actions → Official dictionary test installer → Run workflow**，选 `main`。
2. 填写四段程序版本，例如 `1.0.0.5`，勾选 `create_draft`。
3. 工作流核验输入摘要，以本次 Release 生成工具在新目录生成运行资源，并比较安装端解码重建的词库及语言模型索引。
4. 正式数据按键序列、词库导入、U/V 模式、共享配置和字符模型加载评分五项检查必须全部通过。测试使用独立用户目录和共享对象命名空间；公开附件仅保存状态汇总，不上传原始输入或内部测试日志。
5. 成功后下载 `coral-pinyin-official-installer`，或进入 Releases 查看“正式词库测试版”草稿。安装文件是 `coralpinyin-official-test-windows-x64-<版本>.exe`，附带 SHA256SUMS.txt、BUILD_INFO.json、package-manifest.json 和版本说明。

草稿标签为 `v<版本>-rc.<运行编号>`，预发布且不设为最新正式版。同一运行产生的同名草稿不会被覆盖。Actions 附件保留 14 天，草稿供维护者验收；公开发布草稿后，访客才可直接下载 Release 附件。

正式模型流程启用官网 HTTPS 自动更新；演示构建仍关闭更新。尚未接入受信任签名。安装完成后从开始菜单打开输入法；`--manual-launch` 使用标准 NSIS，不需要 ShellExecAsUser 插件。包内使用正常产品名称及安装标识。

配置同步测试在独立目录关闭自动拼音纠错，以无完整词条命中的 `zua → 抓` 验证模糊音开关前后的候选差异，避免自动纠错及精确词条策略掩盖配置变化。

## 更新数据与轮换密钥

在仓库外准备只含 `source/` 和 `models/` 的输入目录，使用新输出目录执行：

```powershell
python tools/release/official_data.py pack --input-dir <输入目录> --output-dir <新输出目录> --data-version <数据版本>
```

输入范围由工具显式限制；缺少模型、加入本机配置或路径越界都会失败。核验来源、许可及模型复用方式后，将生成的 official-inputs.zip 和 inputs-manifest.json 提交到私有仓库。更新公开固定版本文件中的完整提交和两个摘要，走正常代码审阅及 CI，再手动生成测试包。数据超过 GitHub 单文件上限时须另行设计存储，不能绕过大小限制直接提交。

轮换密钥时创建新的只读部署密钥，更新 `OFFICIAL_DATA_SSH_KEY`，成功读取并出包后撤销旧密钥。密钥私钥不得提交任何仓库。

## 安装验收

演示包的安装结果不自动适用于本包。在独立 Windows 虚拟机验证版本、安装、重启后启用、32/64 位应用输入和卸载；并检查常用词、长句、英文、中英混输、U/V 模式、简繁转换、模糊音开关及用户词学习。原始模型继续复用的限制应在评价输入质量时考虑。记录实际安装版本和结果后，再决定公开测试发行及后续签名接入。

## 2026-10-25 本地 INT8 模型与持续交付

当前固定数据版本 official-20261025.1，额外包含 models/character_model.coral，4,430,160 字节，INT8，SHA-256 b202ab7a3fe603bad2ad4d46e85dc978e8234844b6809c288f276a4ff9ae7bab。源权重、原始词库和训练材料仍留在私有数据范围，不进入公开源码。

正式流程必须核验锁文件、归档、模型容器摘要、原生加载及有限数值评分。模型漏带、被替换、原生评分失败均不能出包；原先四项正式数据检查继续保留，共五项。package_windows.py 将发行副本的 app/Version 写入实际程序版本，不改变源模板，避免模板 0.0.0.0 影响更新判断。BUILD_INFO.json 与 package-manifest.json 保留模型、源码和数据身份。

在 Actions 手动运行 official.yml，填写更高的程序版本、release_description，开启 publish_release 和 sync_website，即依次完成：Windows Debug/Release 测试及 x86 构建 → 固定私有输入与模型核验 → NSIS 未签名测试包 → GitHub v<版本> Latest → 官网安装包和更新接口。默认 publish_release=false，仅创建草稿；普通 push/PR 不发布。已存在 GitHub 标签不覆盖，官网相同来源/摘要的重复部署幂等；失败时可以重跑失败任务。

CORAL_RELEASE_API_KEY 为独立 CI 发布凭据，只能新建/上传/发布自身的更高版本，不能列出管理数据、编辑或删除手动版本。只给受信任 main 的网站同步任务使用，不传入普通检查或构建任务。上传前核验所有发行附件摘要，上传后服务器再次核对长度及 SHA-256，发布后核对固定 ID 下载地址。官网最新入口和后台下载统计沿用现有逻辑，网页登录密钥不用于 CI。

本版本仍是未签名测试发行；自动测试和模型评分不代表实际安装/升级、签名信任或真实输入质量已验收。SignPath 申请状态保持不变。

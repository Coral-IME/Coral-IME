# Windows 打包与代码签名

当前打包入口为 `tools/release/package_windows.py`，安装与签名材料在 `packaging/windows/`。默认只收集文件、`--sign-mode None`；明确传入 `--makensis` 才编译安装程序。旧批处理路径转发新参数，不再支持旧 `-version=`、CORAL_SIGN_MODE 或自动下载 NSIS 的流程。

先完成与版本一致的 x64 构建、x86 coraltsf32 构建和外置运行包。数据与引擎/生成工具校验值必须配套，完整命令见 [数据工具](data-tools.md)。

## 明确选择签名模式

| 模式 | 行为 |
|---|---|
| None | 默认；不调用签名或证书操作 |
| LocalTest | 在 CurrentUser/My 创建或复用本地测试证书，用于明确选择的本机测试 |
| Store | 使用指定指纹的已有签名证书；正式时间戳地址由调用者提供 |
| Artifact | 使用调用者配置的签名 Dlib 和 metadata 文件，不自动创建云账户或服务 |

仅选择其他模式时才在收集副本上签名，自有二进制、NSIS 卸载程序和最终安装程序均使用选定流程。候选不分发 DbgHelp.dll，无需对系统 DLL 重新签名。

可单独显式签名一个准备好的自有文件：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File packaging/windows/sign_windows.ps1 -FilePath C:/packages/app.exe -Mode Store -Thumbprint '<证书指纹>' -TimestampUrl '<时间戳服务地址>'
```

`Store` 的证书必须带可用私钥。`Artifact` 使用 CORAL_SIGN_DLIB 和 CORAL_SIGN_METADATA 或对应脚本参数；不能把私钥、PFX、口令或令牌 PIN 放入源码或清单。签名失败会使流程失败，签名后修改文件需要重新签名。

本地测试证书不会自动成为其他机器信任的正式发布证书。需在自己的测试机器验证信任链时，手动使用 `packaging/windows/trust_local_test_signer.ps1`；撤销其添加的当前用户信任使用 `-Remove`。这与签名动作分开，不由默认打包入口执行。

核对签名可使用 `Get-AuthenticodeSignature`，以实际 Status、SignerCertificate 和 TimeStamperCertificate 为准。当前整理只验证未签名安装包编译，没有执行签名、证书信任或安装；其他模式仍需在真实签名环境验证。

技术参数见 [Microsoft SignTool](https://learn.microsoft.com/windows/win32/seccrypto/signtool)；NSIS 的卸载程序签名使用 3.08 或更新版本。发布者身份、证书服务选择及公开源码地址仍需在发行准备时确定，不沿用旧域名或未经确认的公司名称。

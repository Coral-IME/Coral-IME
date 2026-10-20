# 阿里云通用翻译本机验证

独立脚本为 `tools/diagnostics/aliyun_translation_probe.py`，仅使用 Python 标准库，不接入输入法、不修改已安装程序或已有密钥配置。

## 协议

- HTTPS POST `https://mt.aliyuncs.com/`，API 版本 `2018-10-12`，操作 `TranslateGeneral`。
- 采用 ACS3-HMAC-SHA256，公共参数通过 `x-acs-*` 请求头传递，签名进入 Authorization。需要 AccessKey ID 和 AccessKey Secret；临时 STS 凭据还需 SecurityToken。
- 请求体是 UTF-8 表单 `application/x-www-form-urlencoded`：`FormatType=text`、`SourceLanguage=auto`（可改 zh/en）、`TargetLanguage=en`、`SourceText`、`Scene=general`。中文和 `+ & = %` 等字符通过表单编码保留。
- 同时检查 HTTP 状态与业务 `Code=200`，从 `Data.Translated` 读取非空译文。错误保留错误码和 RequestId，区分网络/TLS、认证、权限、未开通、语言和空结果。
- 请求超时参数为 20 秒（套接字操作超时，非整个任务的绝对截止时间），响应最多 1 MiB；每次点击只发送一次，不自动重试。拒绝重定向，避免带签名请求被转发。默认使用 Python/系统可见的代理环境与有效 TLS 证书校验。

官方资料：

- [TranslateGeneral](https://help.aliyun.com/zh/machine-translation/developer-reference/api-alimt-2018-10-12-translategeneral)
- [服务接入点](https://help.aliyun.com/zh/machine-translation/developer-reference/api-alimt-2018-10-12-endpoint)
- [V3 请求与签名](https://help.aliyun.com/zh/sdk/product-overview/v3-request-structure-and-signature)
- [接口元数据](https://api.aliyun.com/meta/v1/products/alimt/versions/2018-10-12/apis/TranslateGeneral/api.json)

## 使用

在仓库根目录运行：

```powershell
python tools/diagnostics/aliyun_translation_probe.py --gui
```

在本机窗口填写 AccessKey ID 和 AccessKey Secret，保留原文语言 auto、译文语言 en，点击翻译。常规 AccessKey 不需要填写 STS Token。原文默认是简短中文样例；成功后可改成英语并将目标语言改为 zh 继续验证。

也支持命令行，密钥以不回显的提示读取：

```powershell
python tools/diagnostics/aliyun_translation_probe.py --text "你好" --source zh --target en
```

如已配置 `ALIBABA_CLOUD_ACCESS_KEY_ID`、`ALIBABA_CLOUD_ACCESS_KEY_SECRET` 和可选 `ALIBABA_CLOUD_SECURITY_TOKEN` 环境变量，会优先使用。不要把真实密钥写进命令行参数或源文件。`--report <路径>` 可写结果 JSON，包含译文、RequestId 和耗时，不保存凭据或签名；报告可能含测试原文对应的译文，应放在仓库外的本地目录。

## 验证记录

2026-10-15：10 项无凭据单元测试通过，包括官方固定签名样例、UTF-8 表单、字符边界、唯一 nonce、成功结果、业务失败、格式错误、凭据脱敏、重定向拒绝及单次请求。隐藏窗口创建/关闭检查通过。

使用官方示例的虚构凭据向机器翻译网关发送一次请求，收到 HTTP 404 / `InvalidAccessKeyId.NotFound`。这证明本机 HTTPS 与网关认证错误路径可达，不表示真实账户翻译成功。随后由维护者在本机窗口输入真实 AccessKey，实测收到 HTTP 200 / 业务 Code 200，DetectedLanguage=zh，返回译文“hello, welcome to use coral pinyin input method.”，耗时 617 ms。RequestId 为 `01A13E86-3143-5023-8904-992E8FC907FA`。此为单次真实请求的连通性证据，不代表长期延迟或稳定性指标。独立脚本流程已跑通，本次仍未接入 C++ 工程。

工程后续接入需将当前单项 API Key 配置改为 ID/Secret 两项，并确定凭据存储、异步任务及超时/取消处理；本次脚本不会提前改变这些设置。

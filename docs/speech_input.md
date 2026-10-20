# 语音输入基础模块

跨平台核心调用腾讯云 [SentenceRecognition](https://cloud.tencent.com/document/product/1093/35646)，录音结束后一次提交并返回完整 UTF-8 文字，不做流式识别。核心不依赖 UI 或麦克风驱动；新增的 [Windows 接入](speech_windows.md) 单独提供设置页、快捷键、悬浮栏入口、麦克风采集与桌面小窗，把录音交给 `AudioClip` 后复用该核心。Windows 接入自 2026-10-20 起由 `kSpeechFeatureEnabled` 临时屏蔽，跨平台核心、工具和测试保留不变。

## 目录与依赖

- 公共契约：`include/coral/speech/recognizer.h`，命名空间 `coral::speech`。
- 快捷键与按下发言规则：`include/coral/speech/shortcut.h`，仅保存按键状态，不记录文字。`PushToTalkTracker` 以 Ctrl+Win 按下开始、松开或按下其他键结束，供 Windows 键盘监听复用。
- 腾讯云适配：`include/coral/speech/tencent_recognizer.h` 与 `modules/speech/`，构建目标 `coral::speech`。
- 原生联调工具：`tools/native/speech_probe/`，目标 `coral_speech_probe`。
- 测试：`tests/speech/`，固定签名、人工 WAV/PCM、返回协议与失败边界。

实现仅依赖 C++17、libcurl、既有 RapidJSON 和新增 MIT PicoSHA2，不使用 Windows API、Bcrypt、Qt、引擎词库或 UI。网络使用 libcurl 公共接口，SHA-256 使用固定快照的 PicoSHA2；HMAC/TC3 按 [腾讯云签名 v3](https://cloud.tencent.com/document/api/213/30654) 组合，使用 UTC 日期。主工程仍仅支持 Windows；本模块另有独立 CMake 入口，其他平台须完成实际编译/联网验收后才能宣称支持。

## 输入与调用

第一版有意只开放 WAV 和裸 PCM，均为单声道、有符号 16 位小端、8 kHz 或 16 kHz，采样率须与引擎前缀一致。默认引擎 `16k_zh`，8 kHz 录音使用 `8k_zh`。WAV 校验 RIFF/WAVE、整体长度、区块边界、fmt、字节率、数据长度；不做转码或重采样。PCM 必须显式声明正确采样率。

腾讯云限制时长不超过 60 秒，本地上传的 Base64 不超过 3 MiB，因此原始文件最多 2,359,296 字节（WAV 含头）。模块在网络调用前拒绝空输入、超长、奇数字节 PCM、损坏 WAV、立体声、采样率不匹配或缺少凭据。URL、压缩音频、长录音任务、词级时间戳、热词以及麦克风驱动尚未实现。

```cpp
#include "coral/speech/tencent_recognizer.h"

coral::speech::AudioClip audio;
auto failure = coral::speech::load_audio_file(path,
    coral::speech::AudioFormat::kWav, audio);
if (failure != coral::speech::Failure::kNone) { /* 处理读取失败 */ }
coral::speech::TencentRecognizer recognizer({secret_id, secret_key, temporary_token});
const auto result = recognizer.recognize(audio);
if (result.ok()) { /* result.text 为完整 UTF-8 文字，静音允许空字符串 */ }
```

调用为阻塞式，由调用方工作线程执行。本模块不创建线程或隐式重试，不写文件/日志。`RequestControl` 默认 30 秒，最大 5 分钟；预算覆盖编码、签名及请求。取消标志由调用方持有，必须存活至调用返回；取消检查约每 50 ms 发生一次，网络解析器本身可能受系统 DNS 实现限制。取消/超时后丢弃迟到结果，但已提交音频的云端处理与计费不能撤销。

真实传输仅允许 HTTPS，核验证书及主机名、不跟随重定向，响应最多 1 MiB。不自动重试以避免重复计费。`Result` 区分本地输入、缺少凭据、取消、超时、网络、HTTP、鉴权、云端拒绝和无效响应；保留规范化的云端错误码、RequestId 供定位，不返回云端原始错误消息、签名或音频。返回协议完整校验后才提交结果。

`Recognizer` 是供应商无关接口。其他供应商可独立实现；当前 `TencentRecognizer` 支持注入 `HttpTransport` 进行离线测试。注入传输必须自行遵守控制预算及 TLS 契约，不能在 UI 线程直接调用。

## 构建

主工程照常构建，新增目标随主工程生成。独立模块在仓库根目录执行：

```text
cmake -S modules/speech -B build-speech -DBUILD_TESTING=ON
cmake --build build-speech --config Release
ctest --test-dir build-speech -C Release --output-on-failure
```

独立构建默认查找系统 libcurl（至少 7.85，需 TLS 支持）及线程库；也可加 `-DCORAL_SPEECH_BUNDLED_CURL=ON` 编译仓库自带 curl。Windows 自带 curl 使用 Schannel；其他平台使用上游 curl 的 TLS 配置，通常需 OpenSSL 开发包。此入口绕过顶层 Windows 门控，不改变主产品平台支持范围。

## 凭据与联调

只在运行工具的进程环境提供 `CORAL_TENCENT_SECRET_ID`、`CORAL_TENCENT_SECRET_KEY`；临时凭据另提供 `CORAL_TENCENT_TOKEN`。无需 AppId 或地域参数。勿把实际凭据写入源码、示例、命令参数或公开仓库；生产凭据分发方式尚待设计，不把长期云密钥内置客户端。

```text
coral_speech_probe path/to/recording.wav
coral_speech_probe path/to/8k.wav 8k_zh
coral_speech_probe path/to/recording.pcm 16k_zh pcm
```

成功时 stdout 输出文字，stderr 输出音频毫秒数和 RequestId。失败时只输出失败枚举数值、供应商错误码及 RequestId；退出码成功 0、识别失败 1、参数/文件/缺少环境配置 2。输出编码为 UTF-8，控制台需使用 UTF-8；Windows 工具使用宽字符入口读取文件路径，其他平台按 UTF-8 参数解析。

官方 [Java 示例](https://github.com/TencentCloud/tencentcloud-speech-sdk-java-example/blob/main/test_wav/16k/16k_30s.wav) 含可用的 `test_wav/16k/16k_30s.wav`：30.12 秒、16 kHz、单声道、16 位，963,884 字节，SHA-256 `005fe804003bcdc2250982eb1657d60538a7719fd04cb0b7ec41d8f0288e12af`。测试录音保存在仓库外，不随产品分发。文档中的旧 COS `test.wav` 地址已返回无效桶错误；联调使用该官方仓库示例即可。

基础建设阶段的离线测试不代表云端鉴权验收。随后使用维护者提供的临时测试凭据和官方录音，已在 2026-10-16 完成真实 HTTPS 转写：返回完整中文文字、30120 ms 时长及 RequestId；凭据未写入源码、测试夹具或日志。Windows 麦克风采集与输入位置交互的实机验收单独记录，不能从文件转写成功推断为已完成。

## 本次验证（2026-10-16）

x64 Release 主工程完整构建成功，默认回归 62/62 通过（含两项新增语音测试）。模块独立 Windows x64 Release 配置/构建成功，独立测试 2/2 通过；Win32 Release 模块及工具编译成功。现有 Win32 构建关闭测试，因此未执行 Win32 运行回归。

离线测试以 Python `hashlib/hmac` 独立计算固定 TC3 期望值，并覆盖 SHA-256、RFC 4231 HMAC 长短密钥、Base64 补齐、临时 Token、完整 UTF-8 结果、静音、鉴权与业务拒绝、畸形响应、WAV/PCM 边界、取消、超时、文件读取和传输异常。官方 30.12 秒 WAV 通过读取及格式校验。使用明确无效的测试凭据，原生工具向真实腾讯云 HTTPS 域名发送该官方录音，收到 `AuthFailure.SecretIdNotFound` 及 RequestId，确认网络和鉴权错误返回链路；这不验证有效签名或成功转写。

依赖源/许可 SHA-256 与项目许可路径映射已检查。尚未执行有效凭据转写、macOS/Linux/Android 实机测试、Debug、麦克风采集或 UI 验收；未安装替换现用程序或对外发布。

# 维护与验证

当前说明更新于 2026-10-21。开发遵循 [开发准则](../DEVELOPMENT.md)，实现与验证记录见 [维护计划](code-separation.md)。UI 保留、TSF 仅必要整理，范围见 [维护边界](code_separation_scope.md)。

## 构建和测试

使用根 README 的 CMake 预设，产物在 `build/<预设>/`。默认官网更新开启时，客户端注册 **69 项测试**；找到同级独立服务端仓库时，再注册 `update_notice_server`，合计 **70 项**。正式数据等额外选项会改变数量，以该构建树的测试清单为准。

```powershell
ctest --test-dir build/win-x64-release -C Release -N
python tools/checks/run_all.py --build-dir build/win-x64-release --configuration Release --no-build
```

覆盖引擎、会话和协议、共享配置、TSF/UI、语音/翻译、更新、ZIP 及自制演示。服务端 CTest 项内部运行独立仓库单元测试，不能把内部测试数与客户端项数相加。提交 `d240364` 的独立 x64 Debug/Release 基线各 69/69、服务端另验 30/30，见 [基线报告](build-baseline.md)；这不是后续工作树的全量回归结果。

正式数据回归需显式设置 `CORAL_ENABLE_OFFICIAL_DATA_TESTS` 与 `CORAL_TEST_DATA_DIR`，使用隔离用户目录与共享对象命名空间。默认测试通过不代表正式输入质量、安装兼容性或实际宿主行为全部通过。已有正式配置门闸失败与主索引差异尚未完成当前版本复验，不删除断言来掩盖问题。

## 配置和验证边界

- 源码默认程序版本为 **1.0.0.3**；显式 `CORAL_APP_VERSION` 可以覆盖，已有构建缓存不会随文档变化。打包版本、运行配置和版本资源应使用同一发行版本，见 [构建说明](build.md)。
- 更新默认访问 `https://coralime.cn`；关闭构建中的更新使用 `CORAL_ENABLE_UPDATES=OFF`。协议、下载接受及取消分别见 [更新流程](update_detection.md)、[安装包接受](update_package_acceptance.md)、[私有更新通信](update_private_channel.md)。
- 更新测试使用人工元数据、人工文件、隔离子进程及本机服务，不执行安装包。真实 HTTPS 联调需显式运行工具，结果按具体版本登记。
- 在线词库独立配置默认空，未配置时不创建请求。见 [在线词库边界](online_lexicon_catalog.md)。翻译默认关闭；语音实现和测试保留，但产品入口屏蔽。
- 诊断测试仅采集隔离的人工故障进程，产物位于构建树的测试目录，不采集用户正在运行的程序或旧用户日志。见 [本地诊断](local_diagnostics.md)。

公开夹具只使用人工或来源、许可已确认的输入。公开候选不包含旧 golden 集、正式语料或真实用户输入记录，不能宣称已执行这些门禁。

## 数据、许可和发行

修改数据入口时先运行公开小样本，再核对核心索引和安装端解包重建是否逐字节一致；正式源和模型分别登记，复用模型不写成全量重训。使用新的输出目录，保留失败日志，不覆盖源输入。操作入口见 [数据工具](data-tools.md)。

第三方身份、许可和文件哈希见 `third_party/manifest.json`；项目文件分类见 `docs/source-license-map.json`。保留上游及既有作者声明，不能仅凭版本宏或改名证明来源与独立实现。

安装包随附 MPL、第三方通知、完整许可及同版本用户协议、隐私政策、许可说明。发行主体、源码入口和运营事项见 [发行信息](release-info.md)与 [发布前核对](legal/发布前核对.md)。本地编译不等于安装、签名或公开发行。

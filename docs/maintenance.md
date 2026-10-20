# 维护与验证

从根目录 README 的 CMake 预设构建，产物写入 `build/<预设>/`。当前默认 x64 Debug、Release 各注册 54 项测试（配置更新服务后不注册默认禁用入口用例），包含协议、TSF 状态、用户历史格式、配置/UI、特殊模式、混输，以及 ZIP、命名空间、自制演示引擎和基础契约检查。开发遵循 [开发准则](../DEVELOPMENT.md)，当前去重迁移见 [批次计划](code-separation.md)。

```powershell
python tools/checks/run_all.py --build-dir build/win-x64-release --configuration Release --no-build
```

当前去重范围按 [维护者调整](code_separation_scope.md) 执行：UI 保留、TSF 仅简单整理，既有默认测试继续保留。范围缩小不等于完成未执行的发行/UI 验收。

该入口先检查确有测试注册，正式数据清单和旧导出基线不会自动成为必跑项。完整参数见 [数据工具](data-tools.md)。

正式数据额外回归需显式设置 CORAL_ENABLE_OFFICIAL_DATA_TESTS 与 CORAL_TEST_DATA_DIR；测试由独立用户目录和共享对象命名空间启动。当前已知 config_gate 失败仍保留，不应修改断言来掩盖产品行为。默认测试通过不表示正式输入质量或安装兼容性全部通过。

公开候选没有旧 golden 集和正式评测语料，不能声称旧 golden 门禁已经执行。新的用例只能使用人工夹具或来源、许可已确认的输入，不将真实用户日志或输入记录当作夹具。

修改数据入口时先运行公开小样本，再检查 core 索引与安装端解包重建逐字节一致。正式源和模型分别登记；明确复用模型不能写成全量重训。输出选择新的目录，保留失败日志，禁止覆盖源输入。

第三方源码、许可和版本证据登记于 `third_party/manifest.json`。更新依赖时保留上游声明、补齐许可文件并重建清单；版本宏不足以证明原导入提交。项目自有许可范围见 `docs/source-license-map.json`，其中 PacketVideo 与其他外部文件不属于项目 MPL 代码。

安装包收集会携带 MPL、第三方通知、完整许可文本及组件清单。公开发布前还需确定发布主体和源码地址，核对联网、日志、输入记录、崩溃上传等实际行为，见 [发布前核对](legal/发布前核对.md)。本地编译安装程序不等于执行安装或公开发行。

第六批 D 的 diagnostic_request、diagnostic_process 与 tsf_filter_ownership 验证严格请求、隔离人工故障转储/ZIP 和真实 DLL 加载卸载的进程过滤器所有权。测试输出在 build 树的 test-users/diagnostic-<配置>；不采集用户正在运行的程序或旧用户日志。契约、退出码及尚未实现的手动上传见 [本地诊断](local_diagnostics.md)。

第六批 E1 新增 release_metadata、package_acceptance 和 update_agent_disabled：只使用人工 JSON/文件，不联系旧更新服务或执行安装程序。默认 CORAL_UPDATE_SERVICE_BASE_URL 为空；真实服务和旧更新 IPC 的关联/授权与取消仍待 E2。见 [更新接受契约](update_package_acceptance.md)。

第六批 E2 新增 download_wire、download_job、update_private_process、update_http_limits。真实子进程仅执行人工动作/文件，HTTP 仅隔离 loopback 服务，覆盖关联、继承、取消/父退出/拥有 job 与接收限额；不触碰已安装产品的旧全局对象。默认51项，配置更新服务为50项。见 [私有更新请求](update_private_channel.md)。

第六批 E3a 复用 lexicon_import_dialog_smoke 验证迁移后的同一文本解析库，人工文件/引擎替身/原断言保留，测试数量不变。词典算法和旧失败语义保留，不能作为新算法证据；见 [文本导入](lexicon_text_import.md)。

第六批 E3b 新增 lexicon_service、online_lexicon_lifecycle、lexicon_service_configuration；人工服务 URL/实际CMake配置脚本、同一管理源码配假网络/队列及隔离目录，不访问公网或现有用户词库。54为当前默认数量；配置更新服务为53，词库服务配置不改变注册数。见 [在线词库门控与回收](online_lexicon_catalog.md)。

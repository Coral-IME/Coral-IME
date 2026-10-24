# 数据、检查和打包入口

2026-10-19 新增独立表面词 LM 实验格式 `CSLM0001`，见 [模型接口](surface_language_model.md)。下述既有 CSV/压缩包构建流程继续生成旧 `KNAGBIN1`，不承载新概率语义。新模型目前单独编码、原生读回并绑定实际主词典，尚未接入正式安装包生成与端侧重建；不要将它写入旧二元 CSV 后调用旧流水线。

正式入口统一位于 `tools/data/`、`tools/checks/`、`tools/release/` 和 `packaging/windows/`。旧 `src/script/` 入口只转发到新位置，参数改为下列显式路径，不再支持 `BUILD_DIR` 或旧目录层级推断。

## 输入与输出

源目录需要以下文件，正式输入由调用者在公开代码之外提供：

| 文件 | 格式与用途 |
|---|---|
| `coral_lexicon.csv` | 七列：pinyin,frequency,word,fullPinyin,identifier,isPolyphonic,in_m2 |
| `coral_languagemodel.csv` | 五列：prob,prev_word,next_word,prev_fullPinyin,next_fullPinyin |
| `pinyin_frequency.csv` | pinyinstr,frequency；字符模型统计输入 |
| `umode_radical.csv`、`umode_stroke.csv` | 带表头的 UTF-8 CSV：word,code,pinyin；code 为部首序列或笔画序列 |
| `eng_lexicon.csv` | 带表头的 UTF-8 CSV：search_word,display_word,frequency,can_split；can_split 为 0 或 1，frequency 为正整数 |
| `dw.coral` | UU 定义文本 |
| `opencc/` | 配套转换及符号资源，包含 s2t.json、symbol_word.coral、emoji_word.coral 及配置引用的字典 |

输出必须为新的空目录，位于源码外或专用 `build/` 内；禁止输入输出重叠，已有结果须另选目录。所有索引、中间 CSV、模型、日志、清单和数据包写入输出树。失败输出保留日志及 `complete:false`，不生成完成清单；打包入口拒绝不完整资源。

主词库编译为 `data/generallexicon/mainlexicon.coral`，语言模型编译为同目录的 `languagemodel.coral`。文件名在生成、加载、配置校验和安装端重建中一致。U 模式和英文 CSV 支持 UTF-8 BOM、CSV 引号及转义；旧空白分隔文本须先转换，不能直接改扩展名。U 模式和英文编译后的 MARISA/dat 文件名保持现有约定。

## 可公开的小样本流水线

先按照 README 构建 x64 Release，再安装模型构建依赖：`python -m pip install -r tools/data/requirements.txt`。所列版本是本地验证使用的版本；普通 CMake 演示仍仅需 Python 标准库。

```powershell
python tools/data/prepare_demo_source.py --output-dir build/demo-source
python tools/data/build_runtime.py --source-dir build/demo-source --output-dir build/demo-runtime-tools --resource-compiler build/win-x64-release/coralpinyin/Release/coral_resource_compiler.exe --lexicon-builder build/win-x64-release/coralpinyin/Release/coral_lexicon_builder.exe --bundle-decoder build/win-x64-release/coralpinyin/Release/coral_bundle_decoder.exe --data-version demo-v1 --engine-version current-v1
python tools/data/generate_manifest.py --source-dir build/demo-runtime-tools --manifest build/demo-runtime-tools/runtime-manifest.json --verify
```

`prepare_demo_source.py` 从人工夹具适配正式源格式；不从正式词库取样。重跑时选择新的输出目录。`--engine-version` 是调用者声明的版本/格式身份，不自动推断 Git；实际工具 SHA-256 也记入 `build-record.json`，不能仅靠标签判断兼容性。

第三批起原生工具为 `coral_lexicon_builder`、`coral_bundle_decoder`、`coral_engine_console`、`coral_history_inspector`。数据入口使用 `--lexicon-builder` 和 `--bundle-decoder`；旧参数不再接受。完成的 `build-record.json` 版本为 2，工具哈希字段为 `coral_lexicon_builder_sha256`、`coral_bundle_decoder_sha256`。打包入口拒绝旧版记录，需用当前工具在新目录生成配套运行包；词典/模型格式、运行资源清单版本和源清单版本保持不变。

## 正式数据链

同一 `build_runtime.py` 接受外置 `--source-dir`、`--output-dir`，以及显式编译工具位置。公共模板默认来自 `resources/config/`，可用 `--config-dir` 指定经过审查的其他模板。

默认重建字符模型和混拼模型。若指定 `--model-dir`，则明确复用该目录的 `char_lm.dat` 与 `correction_pinyin_model.dat`，记录校验值和 `model_mode:explicit-reuse`；这不代表重新训练或独立验证模型来源。

GPT2 字符模型独立于上述两个纠错模型。需要携带时给 build_runtime.py 传入 --character-model PATH；该文件须为 CCHLM001 容器，会检查版本、长度及载荷 SHA-256，复制为 data/generallexicon/character_model.coral，并写入构建记录与运行包清单。package_windows.py 会收集该文件并让安装器原样落盘；传入 --require-character-model 可防止漏带。默认不选择外部权重，也不绑定本机模型路径。

可先生成源清单，再通过 `--source-manifest` 在构建前校验：

```powershell
python tools/data/generate_manifest.py --source-dir C:/data/source --manifest C:/data/manifests/source.json --data-version my-data-v1 --engine-version current-v1
```

源清单放在源目录之外，格式版本为 2，不读取旧私有输入列表、预测语料或旧导出基线。校验同时发现内容变化、新增或移除文件。

生成顺序为词条索引、二元索引、引擎词典、两种模型、CSV 模式的 pbin 索引、压缩包、C++ 解包及安装端 pbin 对比、配套配置和清单。

词条索引通过 `index_lexicon.py --english eng_lexicon.csv` 并入英文模型身份；完整生成入口自动传入该参数。英文查询索引独立，模型 ID 和词权重与主词库共享。直接生成可使用 `coral_lexicon_builder --csv coral_lexicon.csv coral_languagemodel.csv output_dir eng_lexicon.csv`；压缩入口的 `--english eng_lexicon.csv` 将模型词条写入 bundle，安装端无需额外英文源即可恢复两个索引。详见 [中英混输](mixed_language_dictionary.md)。

二元索引将已有精确匹配或首词回退所选端点写入 `intermediate/bigram/bundle_coral_languagemodel.csv`，压缩工具用该文件绑定引用，避免两端回退规则不同导致丢项。概率 token 保留原值；未解析行、终止词和多音词回退另有审计文件。编码入口遇到未匹配项直接失败，不再悄悄丢弃。

两份完成索引 `mainlexicon.coral`、`languagemodel.coral` 必须与 C++ 解包后重新生成的文件逐字节一致，完成记录才写 `installer_bins_match:true`。旧正式快照的主索引来自另一条 marisa 派生路径，与本流程不完全等价；原快照继续保留，不能宣称正式候选行为已全面保持一致。

## 检查与评测

```powershell
python tools/checks/run_all.py --build-dir build/win-x64-release --configuration Release --no-build
```

`run_all.py` 默认检查 Debug 和 Release，拒绝零项测试。正式资源清单校验须额外传入配套的 `--runtime-dir` 和 `--manifest`；导出校验须显式传入 `--dumpbin` 和 `--exports-baseline`，不会隐式使用旧基线。

性能采样入口为 `tools/checks/perf.py --executable ... --runtime-dir ... --output-dir ... --runs 10`，默认给 sequence_test 传独立用户目录；其他程序用重复的 `--arg` 自定参数，`@USER_DIR@` 会被替换。结果是进程端到端耗时，包含初始化，不能作为每键延迟指标。可用 `--previous` 比较已有结果。

本地正式回归中 sequence、lexicon_import_engine、uvmode_engine 已通过；config_gate 的“下一次相同输入解析发生变化”断言失败，在旧正式包上同样失败。此问题尚未修复，不能将正式回归表述为全部通过。

## 打包与诊断

先生成配套运行包，再编译 x86 Release 的 `coraltsf32`。文件收集不再自动编译、训练、下载 NSIS 或创建签名证书：

```powershell
python tools/release/package_windows.py --build-dir build/win-x64-release --x86-tsf build/win-x86-release/coralpinyin/Release/coraltsf32.dll --runtime-dir C:/data/runtime --output-dir C:/packages/new-package --version 1.0.0.0
```

上述命令只收集文件。需编译安装程序时加 `--makensis C:/tools/NSIS/makensis.exe`。默认 `--sign-mode None`；其他签名模式须显式选择。签名脚本也要求明确 `-Mode`。原签名和信任证书工具归入 `packaging/windows/`，此次未执行签名、证书信任或安装。

打包检查 x64/Win32 架构、运行包清单、完成状态和引擎/生成工具配对，使用公共图标路径，不再分发 DbgHelp.dll。NSIS 源码使用显式 PACKAGE_DIR、OUTPUT_FILE 与 UTF-8 输入。测试安装包使用 0.0.0.0；正式版本须与编译时 CORAL_APP_VERSION 对齐。

诊断工具位于 `tools/diagnostics/`，必须显式指定 `-OutputPath`。旧路径转发参数；此阶段只检查语法，未采集真实输入或操作已有进程。历史脚本、过期文档和第三方通知仍在第 5 步复核。

收集包现携带 LICENSE、LICENSING.md、THIRD_PARTY_NOTICES.md、THIRD_PARTY_MANIFEST.json 及每个组件的完整许可文本，NSIS 会将其安装到产品目录。源码取得地址和最终主体信息仍需在实际发行前补齐。

当前构建记录使用版本 3 的 resource_compiler_sha256，旧版本 2 记录需重新生成；数据/模型格式保留。生成器进程、退出与打包边界见 [静态核心说明](static_engine_core.md)。

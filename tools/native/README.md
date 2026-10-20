# 当前引擎工具

这些 C++ 工具随顶层 CMake 构建，位于构建树 `coralpinyin/<配置>/`，当前保留原可执行文件名。源码目录分别为 `lexicon_builder/`、`bundle_decoder/`、`engine_console/` 和 `history_inspector/`。Python 数据流水线统一位于根目录 `tools/data/`，见 [数据工具文档](../../docs/data-tools.md)。

| 工具 | 用途 |
|---|---|
| `coral_lm_probe` | 校验独立表面词模型与主词典身份，批量查询词元或多词元短语分数；见 [模型接口](../../docs/surface_language_model.md) |
| `coral_speech_probe` | 腾讯云录音转文字联调工具，仅读取显式音频与环境凭据；见 [语音输入](../../docs/speech_input.md) |
| `coral_lexicon_builder` | 从配套 CSV 生成 mainlexicon.coral/languagemodel.coral；也保留显式 marisa/dat 模式 |
| `coral_bundle_decoder` | 解开 v2 词典包，安装端不依赖 Python |
| `coral_engine_console` | 显式指定资源根目录的交互调试，使用独立用户目录与命名空间启动 |
| `coral_parse_bench` | 排除词库加载和预热，测量正常解析调用耗时及目标词前 N/首位命中；见 [基准说明](parse_bench/README.md) |
| `coral_history_inspector` | 旧共享内存布局诊断；不读取当前的学习历史和候选规则文件 |

总数据链由 `build_runtime.py` 调用编译工具，核对解包重建索引一致后生成完成清单。不要混用旧中文键 marisa 派生主索引与新的 CSV 完成索引。

历史词库加密、SQLite 导入、旧评测脚本与设计过程文档不在本候选中，原材料已私有保留。公开候选不宣称能够重建正式词库、训练语料或旧评测集。

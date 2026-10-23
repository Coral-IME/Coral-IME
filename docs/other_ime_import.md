# 从其他输入法导入

在设置的“词库和自学习”页点击“从其他输入法导入”，会打开本地 Python 迁移工具。支持此次验证的微软拼音 UDL、搜狗 SGPU 和微信 LevelDB。用户选择源文件、保存位置并导出后，点击“在珊瑚中预览导入”，返回原有词库导入面板，完成拼音校验、查重、异常项修改及最后确认。导入后可使用原有“撤销本次导入”。

## 本地工具与文件

公开脚本为 `tools/data/ime_dictionary_tool.py`，构建后复制到设置程序旁的 `tools/ime_dictionary_tool.py`，Windows 安装包也包含同一脚本。需要本机 Python 3 和 Tkinter，不下载运行环境；未发现 Python 时显示明确提示，用户仍可使用“导入词库”导入已有 TSV。工具的窗口由用户独立操作，关闭设置窗口后仍可完成导出；如原设置窗口已关闭，回传会提示改用手动导入。

| 来源 | 用户选择 |
|---|---|
| 微软拼音 | `ChsPinyinUDL.dat` 自学习词库 |
| 搜狗拼音 | `sgim_gd_usr.bin`，文件头为 `SGPU` |
| 微信输入法 | 包含 CURRENT、清单、数据表和日志的完整账号目录，复制前应退出微信输入法 |

每次在用户选择的位置新建结果目录，不覆盖已有导出。微信输入在临时快照上读取，核对复制前后的来源哈希。恢复不完整副本需要用户主动选择，限制写入导出报告；损坏校验和不会被静默忽略。词库内容不上传。

默认保留各来源的精简 TSV 与详细记录，另外合并生成 `coral_import.tsv`：

```text
word    pinyin
珊瑚词库迁移    shan hu ci ku qian yi
```

实际使用制表符和 UTF-8 BOM。合并按词语和原始拼音去重，不对读音重新猜测。只合并主迁移词表，微信搭配、频次索引、原始字节不加入普通词库。来源词频的语义和珊瑚权重不同，转换时不灌入目标词频。混合英文、超长或不受支持的词条由引擎在预览中标为异常，不静默当作成功。

## 设置程序与回传

`apps/settings_app/other_ime_import_coordinator.*` 负责定位随程序发布的脚本、从 Python 安装登记或显式 PATH 找到绝对解释器路径，并直接创建进程，不拼接到命令解释器。窗口返回只接收该次启动进程所拥有的发送窗口及约定的 UTF-16 文件路径消息；校验大小、终止符、绝对常规文件和 TSV 扩展名。消息先返回接收确认，再排队进入预览，避免大型词库解析阻塞跨进程回调。重复打开同一活动工具会提示继续使用现有窗口。

回传不执行导入提交；提交仍需要用户在珊瑚预览页点击导入。设置窗口销毁后释放进程句柄并废弃接收关联，不终止用户正在操作的独立工具，也不保留后台线程或挂起回调。

## TSV 兼容边界

`coral::lexicon_io` 新增命名列解析：按 `word` / `pinyin` 读取 TSV，支持列重排和双引号转义，跳过表头，其余原始字段不参与词条生成。只有 `word` 的前缀表可交由原有自动注音流程处理。具有 `previous_word` / `next_word` 的搭配表明确拒绝作为普通词库导入。命名列重复、数据行列数不匹配、跨行或不闭合引号都会拒绝整份命名列表，不留下部分成功的解析结果。

原有 TXT、通用分隔文本和自有 CSV 行为保留。拼音规范化、自动注音、现有词查重、写入和撤销由既有 `LexiconImporter` 负责，没有扩展目标学习数据的持久化结构。

## 来源与验证范围

格式与编号含义核对参考 [ime-lexicon](https://github.com/taxueseek/ime-lexicon)、[微软 UDL 格式解析](https://github.com/studyzy/imewlconverter/blob/master/src/ImeWlConverter.Formats/Win10MsSelfStudy/Win10MsPinyinSelfStudyImporter.cs)、[搜狗 SGPU 格式解析](https://github.com/studyzy/imewlconverter/blob/master/src/ImeWlConverter.Formats/SougouBin/SougouBinParser.cs)。脚本内置的是编号音节对应数据和本项目的文件读取实现，未引入外部解析器运行依赖。此记录不是为参考仓库整体重新授权。

公开回归只使用人工夹具：脚本转换、写入失败回滚、不覆盖旧输出、空词库、WAL 写入与删除、缺失文件恢复、命名列解析、工具进程启动与发送者校验、导入面板预览/提交/撤销，以及引擎中的候选可检索性。开发构建和自动测试不代表已安装版本完成升级，也不替用户导入真实个人词库。

### 本次验证记录（2026-10-22）

- Windows x64 Release 完整构建通过，随程序复制的脚本与公开源码一致。
- 工具的 11 项人工夹具检查、进程回传检查、生产导入面板和设置页 96/120/144/192 DPI 检查通过。
- 使用既有运行资源及全新隔离用户目录执行真实引擎导入：空格拼音规范化、写入后可检索、再次导入查重、撤销后用户条目消失且可重新导入，均通过。
- 默认测试共 72 项。首次 65 项通过，6 项因系统临时目录写入失败；改用工作区专用临时目录重跑后通过。合计 71 项通过，`renderer_window_state` 的 HKCU 悬浮栏位置持久化断言仍失败，该功能源码未在本次修改。不能声称完整默认回归全部通过。
- 已检查安装包收集脚本和人工打包夹具，尚未编译、安装新的 NSIS 安装包，也未替换当前已安装的输入法。

本机构建日志：`build/migration-full-build.log`；默认回归：`build/migration-ctest.log`、`build/migration-rerun.log`；真实引擎回归：`build/migration-engine-test.log`。界面渲染位于 `build/win-x64-release/tests/settings_ui/dpi-renders/lexicon-*.png`。

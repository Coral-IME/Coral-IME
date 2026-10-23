# 文本词库导入边界

2026-10-14，第六批 E3a。按维护者要求保留词典相关算法及格式；本批是职责与名称迁移，不能视为独立实现重写。

公开契约位于 `include/coral/lexicon_io/text_import.h`，Windows 实现位于 `platform/windows/lexicon_io/text_import.cpp`，由 `coral_lexicon_io`（别名 `coral::lexicon_io`）静态目标编译。设置程序和现有导入回归链接同一库；单独构建测试目录时使用同一实现源。实现包含自己的标准库/Windows 声明，不依赖设置程序 PCH、窗口、引擎单例或网络。

| 原实体 | 当前实体 |
|---|---|
| ui/lexiconimportparser.h/.cpp | coral/lexicon_io/text_import.h、platform/windows/lexicon_io/text_import.cpp |
| lexicon_import | coral::lexicon_io |
| ParsedFile | ImportedText |
| TextEncoding / FileFormat | ImportEncoding / ImportLayout |
| ParseFile / ParseText | read_import_file / parse_import_text |
| EncodingName / FormatName / DelimiterName | encoding_label / layout_label / delimiter_label |
| fileName | file_name |

枚举成员采用 `kPascalCase`，内部辅助函数和局部参数采用 `snake_case`；完整符号与路径见 [迁移表](lexicon_text_import_migration.json)。既有 `UserLexiconImportItem` 等引擎导入契约本批保留，不用解析器迁移顺带改全仓词典字段。

保留 UTF-8、带/不带 BOM 的 UTF-16 与 GB18030 识别规则，原 64MiB 文件上限、500 行分隔符取样、Tab/逗号/全角逗号/竖线/空格优先顺序、注释与 Rime 头处理、行号、自有 CSV 字段及字符集标志。预览原始行与待校验词条一一对应；规范化拼音、自动注音、查重与写入仍由既有引擎入口执行。对话框只调整类型、调用与结果字段引用，布局、绘制、交互与显示文本不变。

保留的旧解析行为也保留其限制：UTF-16 解码按原规则读取双字节单元，尾部单字节被忽略，未增加代理对验证；CSV 并非完整通用 CSV/YAML 解析器，原转义/引号判断不变；失败可能已经填写部分结果，成功不主动清空调用方错误字符串。`parse_import_text` 只重置条目、预览行、格式和分隔符，文件路径/编码等元数据按调用方原值保留。文件属性检查与实际读取之间的变化窗口没有在本批修复。这些都是保留实现，不能按新解析器“完整验证后提交”的准则声称已重写；行为修复必须另批明确算法范围。

原解析器声明/函数体经逆向符号映射与忽略 include/guard/注释/空白后逐 token 核对一致；字面量、操作符和控制流程保留。现有实际导入回归继续覆盖人工 UTF-8/GB18030/UTF-16、纯词语/词语拼音/自有 CSV、全角分隔符、预览、校验与导入流程，不新增只验证改名的测试。执行结果以 [迁移计划](code-separation.md) 当前批次为准；正式数据及完整 UI 交互专项仍不属于此批。

在线词库管理器在本批逐字节保留，不以文件导入整理宣称其网络/取消/记录事务已改造。维护者已确认下一批默认禁用在线词库，配置新服务后启用；当前旧管理器的实际门控尚未落地，后续实现单独记录。TSF、词典核心、演示数据及第三方保持原边界。

## E3b 后续状态

E3a 当时保留的旧在线管理器已经迁出UI；默认空服务门控和迟到回调失效已落地。当前主程序未新增自动检查入口，词库运输/记录事务仍待处理。文本导入本身本批逐字节未改；见 [在线词库状态](online_lexicon_catalog.md)。

## 2026-10-22 迁移词表扩展

新增带 `word` / `pinyin` 表头的 TSV 命名列读取，明确拒绝上下文搭配表，支持迁移工具直接回到原有导入预览。此项是明确的行为扩展，不沿用 E3a 的逐 token 一致性结论；旧通用格式不因本次接入而重写。完整流程与限制见 [其他输入法导入](other_ime_import.md)。

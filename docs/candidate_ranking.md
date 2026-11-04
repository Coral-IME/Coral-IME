# 候选学习排序器

该模块用 LightGBM LambdaRank 训练数值树，将已有候选的基础分、字符模型分、分词和二元特征联合排序。它不生成新候选，也不修复未召回的词句。产品端采用自有 C++17 树遍历器，无 Python、LightGBM DLL 或新增后台线程依赖。训练数据和模型权重由使用者自行提供，不随源码分发。

## 启用与回退

启用智能候选并成功加载字符模型后，引擎自动查找字符模型同目录的 `candidate_ranker.coral`。不携带排序器的资源包保持原融合。开发时可在启动前用 `CORAL_CANDIDATE_RANKER` 指定其他绝对路径，显式路径无效时回到原融合，不再尝试目录默认文件。短输入、智能候选关闭、字符推理失败、资源损坏或特征版本不兼容时保留原排序，并在加载失败日志中记录原因。替换字符模型后应重新训练、验证排序器。

排序范围沿用 `CORAL_CHARACTER_RERANK_PATHS`，字符数门槛沿用 `CORAL_CHARACTER_RERANK_THRESHOLD`。搜索预算、全串词库命中分支和界面候选容量不由此模块修改。排序只在相同拼音覆盖长度的候选间置换原有分数槽，原始概率保持原值；这样可继续与直选、混输等来源比较。树分相同时保留原次序。

`CORAL_RANKING_TRACE` 指定 UTF-8 JSONL 路径时输出实际候选特征。轨迹包含拼音、上下文和候选原文，适合隔离开发评测，普通运行应保持关闭。每次解析按组输出，调用方应按输入顺序保留边界，不要仅凭拼音合并不同会话。未启用排序或轨迹时不额外提取路径特征。

## 特征契约

版本为 1，32 个 binary64 特征。顺序以 `tools/data/candidate_ranker_format.py` 的 `FEATURE_NAMES` 为准，与 `make_ranking_features` 一致。包括基础分及差值、字符总分及归一化分数、原重排分数、三种名次、字符/拼音长度、词数、单字比例、最长词、平均词长、二元奖励及覆盖、纠错标志、字符分缺失标志、组内分差与候选数、上下文长度和二元资源可用性。

名次从 0 开始；相同分数保持原序。无字符分时数值填 0 并设置缺失标志，差值无法计算时为 0。`pair_hit_ratio` 的分母为 `max(1, word_count-1)`，命中数包含句首/上下文转移，因此可能大于 1。特征输入不接收目标答案、来源文件名或评测标签。字典、二元或字符模型、候选门槛和权重发生变化后，已有排序权重需要重新评测。

## 训练与导出

依赖见 `tools/data/requirements_candidate_ranker.txt`。训练工具通过官方 LightGBM 4.6.0 C API 工作，产品运行时不加载该依赖。参考 [C API](https://lightgbm.readthedocs.io/en/v4.6.0/C-API.html) 与 [LambdaRank 参数](https://lightgbm.readthedocs.io/en/v4.6.0/Parameters.html)。

每个 `.npz` 包含 `features`（N×32，float64）、`labels`（N，0/1）和 `groups`（各组行数）。组内至少两个候选且包含正确候选。未召回目标可排除出排序监督，但必须保留在端到端准确率分母中。训练、校准与最终比较需按输入/目标去重，实际隔离范围和历史泄漏风险应写入私有实验清单。

```powershell
python tools/data/train_candidate_ranker.py --train train.npz --valid calibration.npz --out output_directory
python tools/data/candidate_ranker_format.py output_directory/model.json candidate_ranker.coral
```

模型选择只使用校准集；最终比较不能反过来用于不断试参后宣称独立验收。准确率需同时报告完整输入总数、原首选正确数、新首选正确数、修复数和新增错误数，并保留按来源/长度的拆分。

验收允许用户选择二选时，同时报告首选命中率（Top1）与前二候选命中率（Top2）。Top2 按实际原生候选列表的第 1、2 项任一与目标全文相等统计，包含候选合并和容量规则的结果；API 失败、未召回输入仍计入完整分母。记录仅第二项补中的数量，以及相对基线的前二修复和损失。Top2 的提升不能写成首选准确率提升，内部搜索窗口覆盖率也不能作为实际前二命中率。

## 二进制格式与校验

打包时用 `prepare_optimized_runtime.py check --candidate-ranker ... --ranking-profile ...` 冻结排序模型和配套资源。配置文件格式为1，记录 `candidate_ranker_sha256`、`character_model_sha256`、`dictionary_sha256`、`pair_bonus_sha256`，以及 `character_weight`、`search_paths`、`neural_paths`、`rerank_threshold`。当前正常启动的字符权重为0.5、搜索预算32；后两项必须与本轮 CMake 参数一致。预检后修改任一配套输入会使收集失败。

`stage` 使用 `--ranker-probe` 进行原生加载验证，并将模型信息写入运行包来源记录。`package_windows.py --require-candidate-ranker` 检查模型存在、摘要与记录一致、字符功能已编译以及评分门槛与配置一致；NSIS 将模型原样部署到字符模型旁边，安装不含排序器的包时移除旧排序器文件。最终安装包清单分别记录字符和排序模型，避免只打入字符模型却误认为学习排序已生效。

所有整数为小端；浮点为 IEEE 754 binary64。80 字节头依次为：magic `CRNK0001`、u32 格式版本 1、u32 头长 80、u64 文件总长、u32 特征数 32、u32 特征版本 1、u32 树数、u32 节点数、32 字节载荷 SHA-256、u32 flags 0、u32 reserved 0。载荷先保存每棵树的 u32 起始节点和 u32 节点数，再保存各节点。

节点为 32 字节：i32 feature、i32 left、i32 right、u32 flags、f64 threshold、f64 value。子节点下标相对本树。叶节点 feature/left/right 均为 -1，flags/threshold 为 0。数值分支采用 `<=`；flags bit 0 表示缺失值向左，bit 1 表示 LightGBM 的 NaN 缺失模式；普通模式下 NaN 按 0 比较。内部节点 value 为 0。导出器拒绝类别分支、Zero 缺失模式和线性叶子。

加载器限制 2 MiB、512 棵树、65,536 个节点和深度 64，检查完整长度、摘要、特征范围、数值有限性、子节点边界、环、共享节点及不可达节点。通过后一次提交；失败不破坏对象原有的有效模型。推理不修改模型，拒绝无穷输入或非有限输出。

## 验证入口

构建 `coral_ranker_probe` 后，可向其标准输入逐行传入 32 个特征，输出树分，用于与 LightGBM C API 核对。`--features` 输出独立的特征夹具。`coral_ranker_probe model.coral --benchmark` 将标准输入的特征每 8 行分组，交错执行 10,000 次并输出时延分位数；最多接收 8,192 行，加载和文本解析不计入推理时间。CTest 的 `candidate_ranker` 覆盖阈值相等、NaN、损坏文件、失败重载、训练数据边界和特征/JSON 转义。引擎接入仍须验证关闭排序时整个候选页一致、实际原生输出与离线预测一致，以及单进程时延；安装后的界面验收单独记录。

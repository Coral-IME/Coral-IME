# 独立训练字符模型

`tools/data/train_character_model.py` 实现随机初始化的字符 GPT。训练、词表构造、模型选择、导出都不需要其他输入法的权重、词表、解析脚本或模型输出。运行时使用同一份公开的 CCHLM001 格式和原生 C++ 推理实现。

默认网络为 3 层、256 维、4 个注意力头、1024 维前馈层、6000 个字符 token、128 个位置，约 400 万参数。词表仅从训练文本统计，BOS 与 UNK 各占一个 token；模型预测下一字符，使用因果注意力、预归一化、tanh GELU 和共享输入输出嵌入，不学习 EOS。训练输入是文字，不要求完整拼音或逐句候选标注。

## 输入与运行

训练集和验证集各提供一个 UTF-8 JSONL，每行包含独立文档身份和文字：

```json
{"doc_id":"train-0001","text":"今天可以写了。这样的组合是可以继续使用的。"}
```

自行准备具有使用权限的语料。按文档及来源先划分训练、验证、测试集，做近重复簇隔离；脚本额外拒绝文档 ID 重叠、NFKC 与空白归一化后的全文重复、非法 Unicode 和超出预算的语料。自动检查不能代替上游的近重复隔离。正式准确率测试集不参与词表、训练、最佳检查点选择或重排权重选择。

```powershell
python -m pip install -r tools/data/requirements_character_model.txt
python tools/data/train_character_model.py --threads 4 train `
  --train C:/corpus/train.jsonl --validation C:/corpus/validation.jsonl `
  --output C:/models/character-run-001 --device cpu `
  --steps 10000 --batch-size 32 --eval-every 500
```

有适配的 PyTorch CUDA 环境可使用 `--device cuda`。本机不会自动下载模型、上传语料、租用算力或切换设备。正式训练应扩大语料和训练预算，观察验证损失与独立输入法准确率，不把少量训练步数的流程验证当作产品质量。默认最多读取每个分区 1000 万字符，可用 `--max-chars` 明确扩大；当前实现将选定语料和编码块保存在内存中。语料应先清洗，训练时不跨文档拼接，长文分块后每块重新从 BOS 开始。当前不提供断点续训，需为较长实验预留完整运行时间。

输出目录必须不存在，包含：

- `best.pt`：验证集字符交叉熵最低的检查点，包含初始模型这一基线。
- `last.pt`：最后一个训练步骤的检查点。
- `character_model.coral`：最佳检查点对应的引擎模型。
- `metrics.jsonl`：训练和验证损失、步骤与用时。
- `manifest.json`：数据摘要、随机种子、网络规格、参数量、软件版本、训练脚本摘要及导出文件摘要。

单独导出已有检查点：

```powershell
python tools/data/train_character_model.py export --checkpoint C:/models/character-run-001/best.pt --output C:/models/character_model.coral
```

检查点仅保存张量和基本类型；读取显式使用 `weights_only=True`。导出文件拒绝覆盖，保存 float32 权重和载荷 SHA-256。运行端不依赖 PyTorch。

## 原生一致性与准确率

准备独立的 `cases.jsonl`，每行是 `{"context":"今天","text":"可以写了"}`。用原生工具逐条对比训练图和引擎图的原始字符对数概率：

```powershell
python tools/data/train_character_model.py verify --checkpoint C:/models/character-run-001/best.pt --model C:/models/character-run-001/character_model.coral --probe build/neural-on/coralpinyin/Release/coral_character_probe.exe --cases C:/corpus/cases.jsonl
python tests/language_model/test_character_training.py build/neural-on/coralpinyin/Release/coral_character_probe.exe
```

训练测试需要额外的 PyTorch 环境，按上述命令单独运行，不增加普通 CTest 的依赖。它实际训练一个随机网络，检查验证损失下降、验证字符不进入词表、训练验证重叠拒绝及原生推理一致性。普通 CTest 保持纯 Python/C++ 合成模型和解码器测试。

字符交叉熵只验证语言建模能力；最终要在固定语料、相同词库、相同用户历史下比较开启/关闭模型后的首选命中、文字正确率、纠正与误伤、P50/P95 延迟。模型负责重排已有最多 32 条完整中文路径，不会补回未召回的候选或修复拼音切分。自有模型达标后，可通过 build_runtime.py --character-model PATH 显式选择权重；package_windows.py --require-character-model 可确保打包时不漏带。源码不包含模型权重。

PyTorch 接口参考：[因果注意力](https://docs.pytorch.org/docs/stable/generated/torch.nn.functional.scaled_dot_product_attention.html)、[检查点加载](https://docs.pytorch.org/docs/stable/generated/torch.load.html)。

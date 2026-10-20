# 解析耗时与命中率基准

`coral_parse_bench` 直接调用静态核心的 `coral::runtime::parse_candidates`（原 parsepinyin 路径），默认取前 8 个候选、全拼模式、无前文。

先同步加载运行资源，再进行最多 64 次不计时的预热。每条独立输入先清解析候选缓存，与正常会话桥接一致；清缓存不卸载词库，也不计入本次调用时间。用 Windows `QueryPerformanceCounter` 仅测量 `parse_candidates` 调用，包含配置快照、解析、检索、排序及候选输出。总耗时为逐次耗时之和，保留小数微秒。不包含启动、词库加载、数据读取、预热、清缓存、目标比较和结果文件写入，也不包含 TSF/IPC/界面时间。

使用独立的新用户目录和共享内存命名空间，不读取已有用户学习数据，不提交候选学习。词库路径通过显式运行资源目录指定；目录必须有 `data/` 和 `config/coralpinyinConfig.ini`，可以传根目录或 `data/` 子目录。程序会在运行资源目录下建立唯一的 `bench-scratch-*` 目录，正常测试结束先关闭引擎，再退出进程。

输入为 UTF-8 的逗号分隔文本，一行一个测试条目：第一列输入拼音、第二列唯一目标词，其余列为参考候选，不参与判分。支持 UTF-8 BOM、注释和 `PinYin,Target,...` 表头；空行不参与统计，缺少输入或目标的行计为无效行。输入格式不支持 CSV 引号转义或跨行字段。按有效行计数，重复行分别测试，不自动去重。

- 命中：解析成功且第二列目标词出现在本次返回的前 N 个候选中。
- 首位命中：解析成功且目标词是第一个候选。
- 有候选：解析成功且返回至少一个候选；不等同于目标词命中。
- 解析失败：API 返回 false；仍计入有效条目分母。

构建：`cmake --build <buildDir> --config Release --target coral_parse_bench`。使用 Release 版本测量，Windows 沙箱限制共享内存时需在正常 Windows 环境运行。

```powershell
& '<buildDir>/coralpinyin/Release/coral_parse_bench.exe' '<runtimeDir>' '<test1.txt>' '<test2.txt>' --size 8 --out '<details.csv>' --summary '<summary.csv>'
```

`--out` 保存每条的来源文件、原始行号、输入、目标、API 状态、候选数、目标排名、微秒耗时及返回候选。目标排名 0 表示未命中。`--summary` 保存每文件和 TOTAL 汇总。控制台输出整体均值、中位数、P95/P99、最小/最大及总解析耗时。

可用 `run_benchmark.ps1` 对输入目录中的所有 `.txt`/`.csv` 文件按名称排序测试，默认顺序执行 3 个独立进程。比较各轮耗时范围，并核对命中统计一致；不同词库、配置、CPU 或用户词库下的结果不能直接混作同一基线。私有数据及实际评测结果保留在仓库外。

# 静态核心与外部边界

第五批 B2a 移除旧引擎 DLL。词典代码编译为 `coral_engine_core` 静态库（别名 `coral::engine_core`，保留 `coral::phonetics` 领域别名），链接到引擎宿主、设置程序、工具和相关测试。运行程序不再需要 langengine.dll；TSF 通过 CSW4 联系宿主，Windows COM 导出仍是系统接口。

## 内部操作

`modules/runtime/core_operations.h/.cpp` 替代旧 API 文件，命名空间是 `coral::runtime`。44 个历史操作登记了名称映射，其中两个无调用方的兼容入口删除；输出迭代器使用领域内 StringOutputIterator，未使用的整数迭代器别名删除。旧 DLL 导出定义、链接修饰宏和 x86 导出名称补丁移除。

此接口是二进制内部的 C++ 适配层，不是新的跨编译器 ABI。词典生成、解析、学习、注音、纠错及已有行缓冲算法保留，仅改命名/作用域/链接限定；部分历史参数和局部变量仍随保留算法存在，最终名称复核继续登记。仅改名不代表独立算法实现，也不改变作者与许可来源。

各程序链接同一个核心 target，移除重复编入的会话/宿主适配源文件，使本地端口的初始化责任和共享锁在同一二进制内一致。内部兼容访问入口仍保留，B2b1 已支持关闭时销毁实际模型并重开，端口通过进程内代次失效；生产宿主生命周期由 B2b2 继续。见 [模型重建](runtime_reconstruction.md)。

## 数据生成

`coral_resource_compiler.exe` 是构建期原生工具，接口为四个命名路径：

```text
coral_resource_compiler --source-dir DIR --index FILE --bigram FILE --output-dir DIR
```

它把既有生成步骤留在进程内，支持 Windows 宽字符命令行，不再让 Python 加载 DLL 并传递 19 个指针参数。生成目录不能位于源目录内，目标文件已存在或输出经路径解析逃离根目录时拒绝。算法失败可能留下部分新输出；使用新的输出目录重试，不自动覆盖输入或旧结果。

Python generate_lexicons.py 调用此工具并正常返回，移除 ctypes 和强制进程退出。build_runtime.py 参数改为 `--resource-compiler`，build-record.json 版本为 3，使用 resource_compiler_sha256；两个安装端生成器的哈希字段保留。词典、模型、dictpack 与 source/runtime 清单格式不变。

## 打包与检查

打包只接受版本 3 完整构建记录，核对资源编译器、词典表生成器和解包器哈希；检查所有收集的程序及构建工具都是 x64，并读取实际 PE 导入表拒绝旧引擎 DLL 依赖。构建期资源编译器不进入安装目录，安装端仍使用 coral_lexicon_builder 和 coral_bundle_decoder。NSIS 不再复制 langengine.dll；历史版本卸载清理行保留。

旧构建树可能仍留有历史 DLL/.def/测试程序，它们不是当前 target 的生成物；不能据它们认定新版本仍使用旧 ABI，也不能将旧导出哈希当作当前结果。run_all.py 的可选导出检查改为 `--tsf-exports-baseline`，检查 TSF 系统导出。

## 实际验证与范围

本批 x64 Release/Debug 全量构建、默认回归各 34/34；Win32 静态核心、原生资源编译器和 TSF 编译通过。static_core_linkage 检查实际程序 PE 依赖；core_shutdown 用隔离的新进程检查同步启动后普通查询、异步加载后关闭、初始化失败清理和正常返回，替代不存在的 DLL 卸载测试。

人工数据完整流水线通过，25 个数据文件与此前 DLL 流水线逐字节相同，安装端重建的核心索引一致；构建中的 27 个演示数据文件也保持不变。新原生工具通过中文/空格路径验证，拒绝覆盖、源目录重合（含不同大小写）和未知参数。暂存演示包不包含引擎 DLL，旧记录和错误生成器哈希拒绝。未编译本批安装器，未安装、签名或发布。

词典 65 个 C++ 文件和内部操作函数体扣除登记命名、链接限定及两个删除入口后 token 一致；第三方及闭源扫描文件哈希保持不变。依赖边界、生成器进程和构建/打包逻辑改变，不将保留的词典算法记为重写。

生产宿主仍有同步管道等既有实现，特殊输入/剪贴板/UI 完整退出尚未验证；部分历史交互冒烟仍强制退出。静态链接解决旧 DLL ABI 边界，不能证明全部后台资源都已可中断关闭。B2b1 已完成进程内模型重建；B2b2 已验证会话归属和普通输入/慢连接下真实跨进程关闭，见 [宿主生命周期](session_host_lifecycle.md)；完整 UI/剪贴板路径仍待第六批。见 [迁移计划](code-separation.md)和[内部操作迁移表](static_core_migration.json)。

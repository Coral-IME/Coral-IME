# User Lexicon Reader - 用户词典实时监控工具

独立的 C++ 可执行程序，直接读取 Boost.Interprocess 共享内存，实时监控用户词典状态。

此工具用于旧共享内存布局。当前引擎已使用独立的学习历史和候选规则文件，
不再创建此共享对象；下列四组数据仅描述旧版本。

## 特性

- **直接读取共享内存**：不需要解析文件，直接访问内存中的用户词典数据
- **实时监控**：检测到状态变更时自动刷新；空闲时显示轻量指示器
- **完整信息**：显示所有 4 个词典（Simplified, Traditional, SimplifiedCandidatePolicy, TraditionalCandidatePolicy）
- **表格显示**：清晰的表格格式，显示拼音、词语、上屏次数、索引、固定状态等
- **版本信息**：显示每个词典的版本号和 tick 值

## 使用方法

### 前提条件

1. **匹配的旧共享布局已提供**：此工具诊断既有共享布局，不要求引擎 DLL 文件；当前静态核心是否提供该布局需按版本核对
2. **共享内存已创建**：旧实现创建名为 `CORALPY_1.0_userlexicon` 的共享内存

### 运行

```bash
# 直接运行
cd build\win-x64-debug\coralpinyin\Debug
.\coral_history_inspector.exe
```
也可以直接双击 `coral_history_inspector.exe` 运行。

### 交互命令

- **q**: 退出程序
- **自动刷新**: 检测到用户词典状态变化时自动刷新；空闲时在控制台显示轻量加载指示器

## 显示内容

### 版本信息
```
Version Info:
  Simplified: 1.001 (tick: 42)
  Traditional: 1.000 (tick: 0)
  SimplifiedCandidatePolicy: 1.000 (tick: 0)
  TraditionalCandidatePolicy: 1.000 (tick: 0)
```

### 词典条目（表格格式）
```
[Simplified]
----------------------------------------------------------------------------------------------------
Pinyin               Phrase                         Commit   Status      Dee   
----------------------------------------------------------------------------------------------------
ni'hao               你好                           3        [ACTIVE]    0.85  
wo'men               我们                           1        [FIX:1]     0.73  
----------------------------------------------------------------------------------------------------
Subtotal: 2 entries (1 active, 1 fixed, 0 deleted)
```

### 字段说明

- **Pinyin**: 拼音（简拼）
- **Phrase**: 词语（中文）
- **Commit**: 上屏次数
- **Status**: 条目状态；`[ACTIVE]` 表示正常条目，`[FIX:n]` 表示固定到位置 n，`[DELETED]` 表示逻辑删除
- **Dee**: 排序权重（浮点数），用于候选排序

## 错误处理

### 错误 1: Cannot open shared memory

**原因**:
- 缺少提供匹配旧共享布局的进程
- 用户词典未初始化
- 共享内存名称不匹配

**解决方法**:
1. 确认有进程提供该工具需要的旧共享布局
2. 在 demo 或测试程序中先输入几个词，触发用户词典初始化
3. 检查共享内存名称（默认 `CORAL_1.0_userlexicon`）
   
  （此工具默认连接名称为 `CORALPY_1.0_userlexicon`，与 langengine 中的构建规则保持一致）

### 错误 2: User lexicon data not found

**原因**:
- 共享内存存在，但数据结构未初始化
- 数据对象名称不匹配（应为 `UserLexiconDataRootCrossArch`）

**解决方法**:
- 等待 langengine 完全初始化
- 按任意键重试

## 使用场景

### 场景 1: 调试用户词典

在开发过程中，运行此工具监控用户词典的变化：

```bash
# 终端 1: 运行 demo
cd build/win-x64-debug/coralpinyin/Debug
demo.exe

# 终端 2: 运行监控工具
coral_history_inspector.exe

# 在 demo 中输入 "nihao" 并选择 "你好"，监控工具会立即显示新增的词条
```

### 场景 2: 验证词频更新

测试词频更新逻辑是否正确：

1. 运行监控工具
2. 在输入法中多次上屏某个词
3. 观察 Commit 字段的变化

### 场景 3: 检查固定位置功能

验证固定位置（pin to position）功能：

1. 运行监控工具
2. 在输入法中将某个词固定到首位
3. 观察 Index 和 Fixed 字段的变化

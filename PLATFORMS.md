# 平台支持

当前 CMake 工程支持 Windows，使用 Visual Studio 2019 与 Windows SDK。x64 Debug/Release 已验证；x86 目前仅验证 Release 的 `coraltsf32` 目标。

公开候选不包含历史 Android、Linux、macOS 和旧语音接入模块，也不包含原 Git 历史。不能从此候选的 Git 中恢复这些模块。

新增的 `modules/speech/` 为独立跨平台语音识别基础模块，提供独立 CMake 入口、腾讯云录音转文字和命令行联调工具，不依赖 Windows API。Windows 麦克风、设置与桌面小窗单独位于 `platform/windows/speech/` 及现有设置/渲染入口。其他平台尚待实机编译验证，不代表整个产品已跨平台，见 [语音基础模块](docs/speech_input.md) 和 [Windows 语音输入](docs/speech_windows.md)。

引擎、数据格式和会话代码可作为其他平台接入的起点，但当前实现含 Windows 共享内存、资源路径和进程约定，未验证直接跨平台构建。移植需另行提供平台适配与构建配置，不能将现有目录当作已支持的平台。

现有入口见 [构建](docs/build.md)、[架构](docs/architecture.md) 和 [数据工具](docs/data-tools.md)。

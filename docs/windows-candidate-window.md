# Windows 候选窗维护说明

当前实现由 TSF 控制器维护候选状态，独立渲染模块接收快照并绘制。构建入口和公开测试见 [维护说明](maintenance.md)，组件关系见 [架构说明](architecture.md)。

## 状态与加载

`src/platform/windows/coraltsf/src/candidatecontroller.h/.cpp` 的 `CandidateController` 在所属线程处理输入与候选选择，使用 `CandidateStateModel` 保存列表。后台加载通过专用窗口消息回到控制器线程；处理返回结果时须核对当前请求，避免旧输入的结果覆盖新候选。新增异步路径应沿用版本检查和线程边界。

当前控制器实现中的 `kFetchSize` 为 60，`kPrefetchThreshold` 为 15，`kMaxCandidateCount` 为 600；这些是实现细节，调整时同时检查预取、边界和分页。初始页大小为 5。剪贴板列表有自己的页面和屏幕行数，不能直接套用拼音候选的分页值。

## 渲染边界

`src/platform/windows/renderer/candidate_view.h` 定义 `CandidateSkin` 等视图结构，`include/coral/composition/presentation_gateway.h` 定义跨组件交互。控制器负责业务状态与提交，渲染器负责窗口和绘制；改动快照字段时同时核对发送端、接收端与相关测试。

默认皮肤参数包括面板宽度 530、可见候选行数 5、选择动画 100 ms、滚动动画 140 ms。皮肤参数和控制器分页不是同一个配置对象，修改一方后要核对另一方的实际呈现。

## 验证

先运行公开状态、渲染及配置相关测试，再按 [Windows 系统界面回归清单](windows-system-ui-regression.md) 实机检查：连续快速输入、翻页边界、旧请求迟到、焦点切换、剪贴板列表、DPI 与多屏定位。自动测试通过不代表已完成实际安装和桌面应用交互验收。

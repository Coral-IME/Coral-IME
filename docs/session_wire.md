# 会话协议 CSW4

无平台编码实现在 `modules/transport/session_wire.cpp`，契约在 `include/coral/transport/session_wire.h`；Windows 管道适配和宿主在 `platform/windows/session_host/`。`coral::session_wire` 不包含 Windows API，也不连接词典实现。Windows 客户端 target 是 `coral::pipe_session_client`。

协议边界由第五批 A 实现；第五批 B1 已将会话领域更新为 coral::composition 下的 SessionPort/SessionResult，并重写本地状态机。B2a 已改为宿主内静态核心，完整模型与宿主生命周期属于 B2b。宿主调度、取消和进程启动逻辑仍保留既有实现。

## 字节格式

整数均为固定宽度、小端；有符号 32 位整数采用二进制补码。禁止复制 C++ 结构体布局。信封固定 72 字节，偏移为十进制：

| 偏移 | 字节数 | 字段 | 要求 |
|---|---|---|---|
| 0 | 4 | magic | ASCII `CSW4` |
| 4 | 2 | version | 必须为 6（新增两阶段候选操作，旧版本明确拒绝） |
| 6 | 2 | envelope_size | 必须为 72 |
| 8 | 2 | operation | 1—18，见下表 |
| 10 | 2 | direction | 请求 0，回复 1，无其他标志 |
| 12 | 4 | payload_size | 至多 1,048,576；帧长度必须精确匹配 |
| 16 | 8 | request_id | 请求关联标识 |
| 24 | 8 | session_id | 会话标识 |
| 32 | 8 | session_generation | 会话代次 |
| 40 | 8 | server_generation | 宿主代次 |
| 48 | 8 | client_id | 客户端关联标识 |
| 56 | 8 | deadline_tick_ms | 请求截止值；回复必须为 0 |
| 64 | 8 | reserved | 必须为 0 |

字符串是 `uint32` 字节长度加原始字节，单项至多 262,144 字节，允许空串和内嵌零，不附加终止字符。文本的 UTF-8、拼音及词语有效性属于会话层，协议层不保证字符串是有效文本。bool 用一个字节，仅接受 0、1。未用业务字段不写入请求载荷，尾部垃圾一律拒绝。

## 请求载荷

字段按表中顺序编码。`text` 是上面的长度前缀字节串，`i32`、`u32`、`u64` 为固定宽度整数，`bool` 为单字节。

| operation | 载荷 |
|---|---|
| 1 Hello、2 OpenSession、3 CloseSession | 空 |
| 4 send_key | codepoint:u32、key:u32、shift:bool、ctrl:bool、alt:bool、vk:u32、process_name:text |
| 5 query_page | condition:text、pinyin:text、offset:i32、page_size:i32、mode:i32 |
| 6 select_candidate | candidate_index:i32 |
| 7 reset_session、8 flush_history | 空 |
| 9 Cancel | cancel_request_id:u64 |
| 10 Ping | 空 |
| 11 record_selection | pinyin:text、word:text、condition_id:u32 |
| 12 revert_selection | word:text |
| 13 apply_candidate_action | pinyin:text、word:text、candidate_action:u8、variant:u8 |
| 14 clear_cache、15 refresh_lexicon | 空 |
| 16 apply_settings | revision:u64、u_mode:bool、v_mode:bool、smart_prediction:bool、pinyin_correction:bool、full_width:bool、traditional_chinese:bool、show_infos:bool、limit_count:i32、combined_lexicon_version:i32、fuzzy_count:u32、fuzzy_choices:i32[] |
| 17 refine_candidates、18 cancel_refinement | query_request_id:u64；必须非零，指向同一会话的基础查询传输编号 |

编解码共用相同业务边界：key 为现有 Key 的 0—14，codepoint 为 0 或 Unicode 标量；offset 非负；page_size 和 limit_count 为 1—512；mode 为全拼 0 或双拼 1；candidate_index 为 0—511；cancel_request_id 非零；模糊选项最多 64 个。候选动作值保留现有词汇领域契约，脚本变体仅简体/繁体；没有把既有词库类别编码为脚本变体。模糊选项的含义与组合词库版本仍由配置/会话层解释。

## 回复载荷

候选动作的线值：PinFirst=0、UnpinFirst=1、Disable=2；脚本变体：Simplified=0、Traditional=1。这些值是 CSW4 的词汇领域契约，禁止因 C++ 枚举重排而改变。

status:u32、result:bool、value:u32、consumed:bool、candidates_visible:bool、offset:i32、has_next_page:bool、composition:text、commit_text:text、message:text、candidate_count:u32，然后逐个候选写入 id:u64、word:text、pinyin:text、display_text:text。

status 只接受现有 SessionStatus 的 0—5，offset 非负，候选最多 512 个。即使每个字符串都未超限，全部字段累计超过 1MiB 也拒绝。解码先检查剩余字节可容纳声明的最小条目数，再分配条目容器。

ParseFull 回复的 value=1 表示有可选的第二阶段优化，基础候选已经可用。操作 17 的 value=1 表示后台任务尚未完成，value=2 表示完整优化候选，value=0 表示取消、过期、不适用或预算内无法完成；后两种未提供候选的情况均保留前端基础列表。操作 18 只取消其指定基础查询，不得取消之后的新查询。优化首次轮询才启动独立计算，所有轮询和取消本身不等待模型。重连不恢复、不重放旧基础快照的优化。宿主与 TSF 必须成套更新。

## 失败与 Windows 传输

状态线值：Ok=0、InvalidSession=1、Unavailable=2、Timeout=3、ProtocolError=4、InternalError=5。新增状态需要显式升级协议，不能让旧客户端猜测含义。

无效输入返回 false 和错误信息，编码目标帧、解码目标消息保持原值；成功清空旧错误。只有完整验证后才提交消息。内存不足等标准库异常不属于无效帧的 bool 返回契约。

Windows 地址是 `\\.\pipe\CoralPinyin.Session.v4.<Windows会话编号>`，启动锁是 `Local\CoralPinyin.SessionHost.v4.<Windows会话编号>`。旧 CRAL 帧、版本 1—5 和未知未来版本均拒绝。所有运行中的客户端和宿主需要成套更新；旧 renderer 和 UI/service 管道不受这次会话协议替换影响。

客户端检查操作、client_id、request_id、session_id 和已知代次的关联关系，一般错配返回 ProtocolError，宿主明确返回代次变更时返回 InvalidSession 并清握手缓存；迟到回复返回 Timeout，候选不交给调用方。缺失或异常管道按既有 Windows 错误映射处理，不能把握手失败一概描述为“宿主不存在”。首次握手与业务请求仍使用既有分别生成的截止值，尚未改为一个涵盖两者的总时间预算。

截止值使用同一 Windows 机器的 GetTickCount64 时间基准，不是跨机器时间戳。取消为尽力通知：已进入执行的词典或学习操作可能有副作用；Timeout 不保证未执行、回滚或恰好执行一次。B2b2 已实现进程/client_id/代次归属、重连失效、固定八个工作槽和可中断传输；当前数量限制、停止事件和在途操作限制见 [宿主生命周期](session_host_lifecycle.md)。

本地端口文本政策及初始化责任见 [会话契约](composition_sessions.md)。历史改名保持字节布局；两阶段候选新增操作 17、18，将版本升级为 6，信封与普通候选回复的布局不变。

## 验证范围

`session_wire` 用人工固定字节夹具检查信封、请求和回复字节序，覆盖每个操作、截断、非法字段、方向/版本/长度/保留位、累计上限及失败状态保持。`pipe_session_client` 使用隔离的真实 Windows 管道，覆盖候选响应、缺失服务、关联错配、宿主代次变化、旧版本和迟到回复。此客户端测试服务器使用进程内独立线程；B2b2 新增 session_host_process 验证实际宿主多进程、普通查询、会话归属和慢连接下正常关闭，完整焦点与特殊输入/UI 仍需专项验收。

本批未转换词典、模型或正式数据，未安装、签名或发布。最终构建与回归数量记录在 [迁移计划](code-separation.md)。

B2b2 新增内部租约释放语义：CloseSession 的 session_id/session_generation 同为 0 时，只释放管道实际进程/client_id 在当前宿主代次的全部会话。编号/帧布局保留，旧宿主无此语义，需成套更新；公开 SessionPort.close_session(0) 仍为空操作，传输端口析构才发送这个控制帧。释放一次、有截止值、尽力完成；详情见 [宿主生命周期](session_host_lifecycle.md)。

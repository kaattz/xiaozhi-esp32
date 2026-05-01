# 003 需求：Grouped Wake Arbitration

## 背景

当前唤醒仲裁是全局锁：谁先上报 `/wake-detected`，谁占用会话。这个规则能避免多台设备同时回复，但不能判断哪台设备离人更近，也会把不同房间的正常并发唤醒互相拦住。

这次 feature 只解决唤醒仲裁本身，不继续做 HA 主动询问确认链路。

## 入口判定

这是中大型功能：

| 原因 | 说明 |
|---|---|
| 跨仓库 | 需要改 `xiaozhi-esp32`、`xiaozhi-gateway`、`ha-mcp-for-xiaozhi`。 |
| 接口契约 | `/wake-detected` 要新增 `wake_rms_dbfs`，gateway 要新增分组仲裁语义。 |
| 状态流 | active context 从单个全局状态变成多设备会话状态。 |
| 并发边界 | 不同 wake group 可以并发，但 HA 房间上下文不能猜。 |
| 配置结构 | `devices.yaml` 需要 `wake_group`、`priority`、`mic_gain_offset_db`。 |

## 用户故事

| 编号 | 用户故事 |
|---|---|
| US-1 | 作为家庭用户，我在客厅喊小智时，希望只让离我最近、听得最清楚的设备响应。 |
| US-2 | 作为家庭用户，我不希望客厅一喊小智，餐厅和卧室的小智也一起回答。 |
| US-3 | 作为家庭用户，如果两个互不串音的房间差不多同时喊小智，我希望两个房间都能进入各自会话。 |
| US-4 | 作为维护者，我希望官方小智云无法区分 MCP 调用来源设备时，系统显式报冲突，不能把 HA 命令执行到错误房间。 |

## 功能需求

| 编号 | 需求 |
|---|---|
| REQ-1 | ESP32 必须在唤醒词检测成功后计算唤醒片段的 `wake_rms_dbfs`，并随 `/wake-detected` 上报。 |
| REQ-2 | `wake_rms_dbfs` 必须来自已有唤醒 PCM 数据，不上传额外音频片段到 gateway。 |
| REQ-3 | gateway 的 `DeviceMapping` 必须支持 `wake_group`、`priority`、`mic_gain_offset_db`；未配置 `wake_group` 的设备必须默认成为独立分组。 |
| REQ-4 | 同一 `wake_group` 内多个设备在仲裁窗口内唤醒时，gateway 必须按校准后的响度选择获胜设备；当校准响度差距小于 `close_rms_threshold_db` 时必须按 `priority` 排序。 |
| REQ-5 | 同一 `wake_group` 单设备唤醒时，如果该组只有一个设备，gateway 可以直接允许，不等待窗口。 |
| REQ-6 | 不同 `wake_group` 的设备唤醒不能互相抢占语音会话。 |
| REQ-7 | gateway 必须显式拒绝缺少 `wake_rms_dbfs` 的仲裁请求，不能退回“谁先到谁赢”。 |
| REQ-8 | gateway 必须支持多 active session，并允许按 `device_id` 结束指定 session。 |
| REQ-9 | `GET /active-context` 在只有一个 active session 时返回该上下文；多个 active session 时必须返回明确的 `multiple_active_contexts`，不能任选一个。 |
| REQ-10 | `ha-mcp-for-xiaozhi` 遇到 `multiple_active_contexts` 时，不能注入默认房间上下文；需要返回明确错误或只允许显式房间参数的 HA 工具调用。 |
| REQ-11 | 仲裁失败、未知设备、缺少响度、超时、非获胜设备都必须有明确返回状态。 |
| REQ-12 | 第一版不做音频指纹、声纹识别、本地 ASR，也不上传唤醒音频到 gateway。 |
| REQ-13 | 仲裁窗口必须有明确上限；默认窗口为 300ms，包含网络在内的用户感知额外延迟目标不得超过 500ms。 |
| REQ-14 | 多设备组仲裁窗口关闭时，只比较已到达候选设备；不得等待未上报、离线或网络超时设备。 |
| REQ-15 | active session 必须有 TTL，过期后自动清理，避免多 active session 永久堆积。 |
| REQ-16 | gateway 必须记录可观测仲裁日志，至少包含 `wake_group`、winner、loser、校准前后响度、priority、reason；日志不得包含用户语音内容。 |

## 验收标准

| 编号 | 验收 |
|---|---|
| AC-1 | 两台同组设备 300ms 内同时上报时，校准后响度更高的设备收到 `allow_session`，另一台收到 `deny_session`。 |
| AC-2 | 同组校准响度差小于 `close_rms_threshold_db` 时，`priority` 高的设备获胜。 |
| AC-3 | 不同 `wake_group` 的两台设备同时上报时，两台都可以收到 `allow_session`。 |
| AC-4 | `/wake-detected` 缺少 `wake_rms_dbfs` 时返回 422 或明确错误，不创建 active session。 |
| AC-5 | 多个 active session 存在时，`GET /active-context` 不返回任意一个房间，而是返回 `{"active": false, "status": "multiple_active_contexts"}`。 |
| AC-6 | 多个 active session 存在且用户没有显式说房间时，`ha-mcp-for-xiaozhi` 不调用 `HassTurnOn` 等房间默认工具。 |
| AC-7 | 一个 active session 存在时，现有按房间注入 `preferred_area_id` 的 HA 控制继续可用。 |
| AC-8 | ESP32 固件构建通过；gateway 和 ha-mcp 单元测试通过。 |
| AC-9 | 同组仅一台已配置设备唤醒时，gateway 不等待仲裁窗口并立即返回 `allow_session`。 |
| AC-10 | 结束指定 `device_id` 的 session 后，不影响其他 active session。 |
| AC-11 | gateway 仲裁超时时返回 503；ESP32 不进入会话，静默回到待机并重新启用唤醒。 |
| AC-12 | ESP32 不向 gateway 上传唤醒音频；`/wake-detected` 只包含结构化字段和 `wake_rms_dbfs`。 |
| AC-13 | gateway 仲裁日志能看出 winner/loser、响度差、priority 和 reason。 |

## 非目标

| 非目标 | 原因 |
|---|---|
| 不做音频指纹 | 第一版先用已有 PCM 算响度，避免引入上传音频和特征提取复杂度。 |
| 不做声纹识别 | 声纹不等于距离判断，而且会引入隐私和训练问题。 |
| 不让 gateway 猜 HA 房间 | 官方小智云 MCP 调用当前没有可靠来源设备字段。 |
| 不改官方小智云协议 | 该 feature 只改本地固件、gateway、HA MCP 集成。 |
| 不解决主动询问确认 | 询问链路已暂停，不纳入本 feature。 |

## 界面约束

| 项 | 约束 |
|---|---|
| ESP32 配网页 | 保留现有“启用 Wake Arbitration”开关，不新增复杂 UI。 |
| gateway 配置 | 第一版通过 `devices.yaml` 配置 `wake_group`、`priority`、`mic_gain_offset_db`。 |
| HA 配置 | 不新增新的小智云 MCP 接入点。 |

## 需求追踪

| 需求 | Spec 章节 | 验证方式 |
|---|---|---|
| REQ-1 | ESP32 wake energy | ESP32 静态测试 + 构建 |
| REQ-2 | ESP32 wake energy | 代码 review |
| REQ-3 | Gateway config model | gateway model/config 测试 |
| REQ-4 | Group arbitration | gateway 仲裁单测 |
| REQ-5 | Group arbitration | gateway 仲裁单测 |
| REQ-6 | Multi-session state | gateway API 测试 |
| REQ-7 | Gateway API | gateway 422 测试 |
| REQ-8 | Multi-session state | gateway session store 测试 |
| REQ-9 | Active context API | gateway API 测试 |
| REQ-10 | HA MCP ambiguity handling | ha-mcp 测试 |
| REQ-11 | Error handling | gateway + ESP32 + ha-mcp 测试 |
| REQ-12 | 非目标检查 | drift check |
| REQ-13 | Group arbitration | gateway 延迟/窗口测试 |
| REQ-14 | Group arbitration | gateway 候选窗口测试 |
| REQ-15 | Multi-session state | gateway TTL 测试 |
| REQ-16 | Observability | 日志断言或 code review |

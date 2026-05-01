# 003 Spec：Grouped Wake Arbitration

## 目标

把唤醒仲裁从“全局谁先到谁赢”改成“同组短窗口竞争，按唤醒响度选择”。不同 `wake_group` 可以并发语音会话，但 HA 默认房间上下文不能在多会话时猜测。

核心原则：

| 原则 | 说明 |
|---|---|
| 响度来自设备端 | ESP32 用已有唤醒 PCM 算 `wake_rms_dbfs`。 |
| gateway 只做仲裁 | 不上传唤醒音频，不做指纹。 |
| 同组竞争 | 只让容易串音的一组设备互相竞争。 |
| 不猜 HA 上下文 | 多 active session 时，HA MCP 默认房间注入必须失败得清楚。 |

## 代码证据

| 仓库/文件 | 证据 |
|---|---|
| `xiaozhi-esp32/main/audio/wake_word.h` | `WakeWord` 当前只暴露唤醒词和 Opus 数据，没有响度接口。 |
| `xiaozhi-esp32/main/audio/wake_words/afe_wake_word.cc` | `StoreWakeWordData(res->data, ...)` 已保存约 2 秒唤醒 PCM，可计算 RMS。 |
| `xiaozhi-esp32/main/audio/wake_words/custom_wake_word.cc` | 自定义唤醒词也保存 `wake_word_pcm_`，需要同样暴露 RMS。 |
| `xiaozhi-esp32/main/audio/wake_words/esp_wake_word.cc` | ESP WakeWord 当前不保存唤醒 PCM；为降低内存占用，应补滚动 RMS 统计，不补完整 PCM ring。 |
| `xiaozhi-esp32/main/audio/audio_service.cc` | `AudioService::GetLastWakeWord()` 已转发唤醒词，可增加 `GetLastWakeRmsDbfs()`。 |
| `xiaozhi-esp32/main/wake_arbiter_client.cc` | `/wake-detected` payload 当前只有 `device_id/client_id/wake_word`。 |
| `xiaozhi-gateway/app/arbitration.py` | 当前 `decide_wake()` 使用单个 `session_store`，是全局锁。 |
| `xiaozhi-gateway/app/models.py` | `DeviceMapping` 没有 `wake_group/priority/mic_gain_offset_db`；`WakeDetectedRequest` 没有 `wake_rms_dbfs`。 |
| `xiaozhi-gateway/app/session_store.py` | 当前只持久化单个 `ActiveContext`，不能表达多 active session。 |
| `ha-mcp-for-xiaozhi/custom_components/ws_mcp_server/server.py` | `_fetch_active_context()` 只 GET `/active-context`，MCP 会话里没有来源小智 device_id。 |
| `ha-mcp-for-xiaozhi/custom_components/ws_mcp_server/websocket_transport.py` | `LLMContext(device_id=None)`，当前 WebSocket 接入点没有把小智设备身份传进 HA。 |

## 总体流程

```text
ESP32 local wake word detected
  -> compute wake_rms_dbfs from wake PCM
  -> POST gateway /wake-detected
       device_id, client_id, wake_word, wake_rms_dbfs
  -> gateway records receive time and finds device wake_group
  -> if group has one device: allow
  -> if group has multiple devices: wait arbitration_window_ms
  -> choose winner by adjusted_rms / priority / deterministic tie-breaker
  -> winner gets allow_session
  -> losers get deny_session
  -> allowed device opens normal Xiaozhi cloud audio session
```

HA MCP context flow:

```text
One active session:
  ha-mcp GET /active-context
  -> active=true with room
  -> inject preferred_area_id

Multiple active sessions:
  ha-mcp GET /active-context
  -> active=false,status=multiple_active_contexts
  -> do not inject default room
  -> explicit room commands may proceed without gateway context
```

## ESP32 Wake Energy

### 新接口

`WakeWord` 增加：

```cpp
virtual float GetLastWakeRmsDbfs() const = 0;
```

`AudioService` 增加：

```cpp
float GetLastWakeRmsDbfs() const;
```

`WakeArbiterClient::RequestSession` 改为：

```cpp
bool RequestSession(const std::string& wake_word, float wake_rms_dbfs);
```

### RMS 计算

从最近唤醒 PCM 或滚动统计计算：

```text
rms = sqrt(sum(sample * sample) / sample_count)
wake_rms_dbfs = 20 * log10(max(rms, 1) / 32768.0)
```

规则：

| 场景 | 行为 |
|---|---|
| 有唤醒 PCM | 返回负数 dBFS，例如 `-18.5`。 |
| PCM/统计为空 | 返回 NaN 或显式 invalid 状态，不能填 0。 |
| 计算结果非有限数 | ESP32 不发起仲裁请求，记录错误并重新启用唤醒。 |

### 支持范围

| 实现 | 要求 |
|---|---|
| AFE WakeWord | 必须支持，可从 `wake_word_pcm_` 或同等滚动统计计算。 |
| Custom WakeWord | 必须支持，可从 `wake_word_pcm_` 或同等滚动统计计算。 |
| ESP WakeWord | 必须用最近检测窗口的滚动 `sum_squares + sample_count` 计算；不得为了 RMS 保存完整唤醒音频。 |

## Gateway 配置模型

`devices.yaml` 示例：

```yaml
devices:
  living_room_xiaozhi:
    device_id: "aa:bb:cc:dd:ee:ff"
    client_id: "livingroom_xiaozhi"
    room_id: "living_room"
    room_name: "客厅"
    ha_area_id: "living_room"
    wake_group: "public_area"
    priority: 100
    mic_gain_offset_db: 0.0

  dining_room_xiaozhi:
    device_id: "11:22:33:44:55:66"
    room_id: "dining_room"
    room_name: "餐厅"
    ha_area_id: "dining_room"
    wake_group: "public_area"
    priority: 80
    mic_gain_offset_db: -3.0
```

字段规则：

| 字段 | 默认 | 说明 |
|---|---:|---|
| `wake_group` | device key | 默认单设备一组。 |
| `priority` | 0 | 响度接近时的次级排序。 |
| `mic_gain_offset_db` | 0.0 | 不同设备麦克风增益校准。 |

校准响度：

```text
adjusted_wake_rms_dbfs = wake_rms_dbfs + mic_gain_offset_db
```

## Gateway API

### `POST /wake-detected`

Request:

```json
{
  "device_id": "aa:bb:cc:dd:ee:ff",
  "client_id": "livingroom_xiaozhi",
  "wake_word": "你好小智",
  "wake_rms_dbfs": -18.5,
  "timestamp": 1777440000.0
}
```

`timestamp` 只用于诊断。仲裁窗口以 gateway 收到请求的本地时间为准，不能依赖 ESP32 时钟。

Response allow:

```json
{
  "type": "allow_session",
  "device_id": "aa:bb:cc:dd:ee:ff",
  "room_id": "living_room",
  "room_name": "客厅",
  "ha_area_id": "living_room",
  "expires_at": 1777440120.0
}
```

Response deny:

```json
{
  "type": "deny_session",
  "reason": "lower_wake_rms",
  "winner_device_id": "aa:bb:cc:dd:ee:ff"
}
```

错误：

| 场景 | 返回 |
|---|---|
| 未知设备 | 404 `device not found` |
| 缺少 `wake_rms_dbfs` | 422 `wake_rms_dbfs required` |
| `wake_rms_dbfs` 非有限数 | 422 `invalid wake_rms_dbfs` |
| 仲裁内部超时 | 503 `arbitration_timeout` |

## Group Arbitration

配置：

| 参数 | 初始值 |
|---|---:|
| `arbitration_window_ms` | 300 |
| `close_rms_threshold_db` | 3.0 |
| `request_timeout_ms` | 800 |
| `active_session_ttl_seconds` | 120 |

约束：

| 参数 | 约束 |
|---|---|
| `arbitration_window_ms` | 默认 300ms；配置上限 500ms。 |
| 用户感知额外延迟 | 目标不超过 500ms，包含局域网请求和仲裁等待。 |

判定：

| 条件 | 行为 |
|---|---|
| group 只有一个设备 | 直接允许。 |
| group 多个设备 | 第一个候选到达后等待 `arbitration_window_ms`。 |
| 仲裁窗口关闭 | 只比较已到达候选；不等待未上报、离线或网络超时设备。 |
| 最高响度与第二名差距 >= 阈值 | 最高 `adjusted_wake_rms_dbfs` 获胜。 |
| 差距 < 阈值 | `priority` 高者获胜。 |
| priority 仍相同 | `adjusted_wake_rms_dbfs` 高者获胜。 |
| 仍相同 | `device_id` 字典序作为确定性 tie-breaker。 |

实现建议：

| 模块 | 说明 |
|---|---|
| `WakeArbitrationStore` | 维护每个 group 的候选窗口、条件变量和结果。 |
| 同步边界 | FastAPI sync route 可用 `threading.Condition`，不要用 sleep 轮询。 |
| 清理 | 结果保留短 TTL，避免请求线程错过结果。 |

### 可观测性

gateway 每次仲裁必须记录结构化日志，字段至少包括：

| 字段 | 说明 |
|---|---|
| `wake_group` | 参与仲裁的组。 |
| `winner_device_id` | 获胜设备。 |
| `loser_device_ids` | 被拒设备。 |
| `wake_rms_dbfs` | 原始响度。 |
| `adjusted_wake_rms_dbfs` | 校准后响度。 |
| `priority` | 参与排序的 priority。 |
| `reason` | `higher_rms`、`higher_priority`、`tie_breaker` 等。 |

日志不得包含用户语音内容或唤醒音频。

## Multi-session Active Context

`SessionStore` 从单个 context 改为多设备 context。

active session 使用 TTL，默认 120 秒。读取 active context、创建新 session、结束 session 时都要清理过期项。

### `GET /active-context`

无 query：

| 状态 | 返回 |
|---|---|
| 0 个 active | `{"active": false}` |
| 1 个 active | `{"active": true, ...context}` |
| 多个 active | `{"active": false, "status": "multiple_active_contexts"}` |

按设备 query：

```text
GET /active-context?device_id=aa:bb:cc:dd:ee:ff
```

返回该设备 context 或 inactive。

### `POST /session/end`

按 `device_id` 结束指定 session，不清空其他设备 session。

## ha-mcp-for-xiaozhi

当前官方小智云 MCP 接入点没有把来源设备传入 HA。多 active session 时不能知道“这次工具调用来自哪台小智”。

规则：

| 场景 | 行为 |
|---|---|
| 单 active session | 保持现有行为，注入 `preferred_area_id`。 |
| 多 active session + 工具参数有显式 room/area | 跳过 gateway context，按用户显式房间调用 HA 工具。 |
| 多 active session + 工具参数无显式 room/area | 返回 `active_context_ambiguous`，不调用 HA 工具。 |
| gateway 不可用 | 保持现有明确错误，不猜房间。 |

需要调整 `server.py`：在判断参数有显式房间时，不应先强制 `_fetch_active_context()`。

## 错误处理

| 层 | 错误 | 行为 |
|---|---|---|
| ESP32 | RMS 无效 | 不发 `/wake-detected`，重新启用唤醒。 |
| ESP32 | gateway 422/503 | 不进入会话，静默回到待机并重新启用唤醒。 |
| gateway | 缺少响度 | 422，不创建 session。 |
| gateway | 仲裁超时 | 503，不创建 session。 |
| gateway | lower RMS loser | 200 deny，不创建 session。 |
| gateway | 多 active context | `/active-context` 明确返回 `multiple_active_contexts`。 |
| ha-mcp | active context ambiguous | 不调用 HA 工具。 |

## 验证策略

| 层 | 验证 |
|---|---|
| ESP32 static | `wake_rms_dbfs` 字段存在；无效 RMS 不请求 gateway。 |
| ESP32 build | `idf.py build`。 |
| gateway unit | 同组响度胜出、priority 胜出、不同组并发、缺字段 422、单设备组立即允许、只比较已到达候选。 |
| gateway API | active context 0/1/多 session 返回正确；session TTL 和指定 device end 正确。 |
| ha-mcp unit | 多 active context 不注入默认房间；显式房间可绕过 context。 |
| 手工联调 | 客厅/餐厅同组串音只响应一台；不同组可同时唤醒。 |

## 需求追踪

| 需求 | Spec 章节 | 实施任务 | 验证 |
|---|---|---|---|
| REQ-1 | ESP32 Wake Energy | Task 2 | ESP32 static + build |
| REQ-2 | ESP32 Wake Energy | Task 2 | code review |
| REQ-3 | Gateway 配置模型 | Task 3 | gateway config tests |
| REQ-4 | Group Arbitration | Task 4 | gateway arbitration tests |
| REQ-5 | Group Arbitration | Task 4 | gateway arbitration tests |
| REQ-6 | Multi-session Active Context | Task 5 | gateway API tests |
| REQ-7 | Gateway API | Task 4 | gateway 422 tests |
| REQ-8 | Multi-session Active Context | Task 5 | session store tests |
| REQ-9 | Multi-session Active Context | Task 5 | active-context tests |
| REQ-10 | ha-mcp-for-xiaozhi | Task 6 | ha-mcp tests |
| REQ-11 | 错误处理 | Task 4/5/6 | negative tests |
| REQ-12 | 非目标 | Task 8 | drift check |
| REQ-13 | Group Arbitration | Task 4 | window/latency tests |
| REQ-14 | Group Arbitration | Task 4 | arrived candidates tests |
| REQ-15 | Multi-session Active Context | Task 5 | TTL tests |
| REQ-16 | 可观测性 | Task 4/8 | log assertions / review |

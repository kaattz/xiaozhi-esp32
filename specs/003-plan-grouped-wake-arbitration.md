# 003 实施计划：Grouped Wake Arbitration

## 执行规则

| 规则 | 要求 |
|---|---|
| 先确认 | 未经用户明确确认本 req/spec/plan，不写功能代码。 |
| 不提交 | 提交、推送、刷机前必须单独确认。 |
| 不新 worktree | 不私自创建新 worktree。 |
| 不猜上下文 | 多 active session 时不得任选一个 HA 房间上下文。 |
| 不上传音频 | 第一版不上传唤醒音频到 gateway。 |
| 不做询问 | 不恢复主动询问设计。 |

## Task 0：实施前检查

<files>

- `specs/003-req-grouped-wake-arbitration.md`
- `specs/003-spec-grouped-wake-arbitration.md`
- `C:\Code\xiaozhi-esp32\main\audio\wake_word.h`
- `C:\Code\xiaozhi-esp32\main\audio\wake_words\afe_wake_word.cc`
- `C:\Code\xiaozhi-esp32\main\audio\wake_words\custom_wake_word.cc`
- `C:\Code\xiaozhi-esp32\main\audio\wake_words\esp_wake_word.cc`
- `C:\Code\xiaozhi-esp32\main\wake_arbiter_client.cc`
- `C:\Code\xiaozhi-gateway\app\arbitration.py`
- `C:\Code\xiaozhi-gateway\app\session_store.py`
- `C:\Code\xiaozhi-gateway\app\models.py`
- `C:\Code\ha-mcp-for-xiaozhi\custom_components\ws_mcp_server\server.py`
- `C:\Code\ha-mcp-for-xiaozhi\custom_components\ws_mcp_server\gateway_context.py`

<action>

- 逐条读取 req/spec。
- 确认三个仓库当前状态。
- 确认当前 ESP32 问询播报链路不纳入本次改动。
- 确认 HA MCP 无来源小智 device_id，不能支持多 active 默认房间猜测。

<verify>

- `git -C C:\Code\xiaozhi-esp32 status -sb`
- `git -C C:\Code\xiaozhi-gateway status -sb`
- `git -C C:\Code\ha-mcp-for-xiaozhi status -sb`
- `rg -n "wake_rms_dbfs|wake_group|mic_gain_offset_db|multiple_active_contexts" C:\Code\xiaozhi-esp32 C:\Code\xiaozhi-gateway C:\Code\ha-mcp-for-xiaozhi`

<done>

- 明确本次只改 003 范围。
- 不覆盖用户未提交修改。

## Task 1：测试先行

<files>

- Create/Modify: `C:\Code\xiaozhi-gateway\tests\test_arbitration.py`
- Create/Modify: `C:\Code\xiaozhi-gateway\tests\test_active_context.py`
- Create/Modify: `C:\Code\ha-mcp-for-xiaozhi\tests\test_gateway_context.py`
- Create: `C:\Code\xiaozhi-esp32\tests\test_wake_arbitration_static.py`

<action>

- 先写失败测试，覆盖：
  - 同组高响度获胜。
  - 同组响度接近时 priority 获胜。
  - 不同组同时允许。
  - 缺少 `wake_rms_dbfs` 拒绝。
  - 单设备组立即允许。
  - 多设备组只比较仲裁窗口内已到达候选。
  - 仲裁超时返回 503。
  - 多 active context 返回 `multiple_active_contexts`。
  - 指定 `device_id` 结束 session 不影响其他 session。
  - active session TTL 过期后自动清理。
  - ha-mcp 多 active 无显式房间不调用 HA 工具。
  - `/wake-detected` 不包含唤醒音频字段。

<verify>

- `cd C:\Code\xiaozhi-gateway; uv run pytest tests/test_arbitration.py tests/test_active_context.py -v`
- `cd C:\Code\ha-mcp-for-xiaozhi; pytest tests/test_gateway_context.py -v`
- `cd C:\Code\xiaozhi-esp32; python -m pytest tests/test_wake_arbitration_static.py -v`

<done>

- 测试先失败，失败点对应 003 需求。

## Task 2：ESP32 计算并上报 wake_rms_dbfs

<files>

- Modify: `C:\Code\xiaozhi-esp32\main\audio\wake_word.h`
- Modify: `C:\Code\xiaozhi-esp32\main\audio\audio_service.h`
- Modify: `C:\Code\xiaozhi-esp32\main\audio\audio_service.cc`
- Modify: `C:\Code\xiaozhi-esp32\main\audio\wake_words\afe_wake_word.h`
- Modify: `C:\Code\xiaozhi-esp32\main\audio\wake_words\afe_wake_word.cc`
- Modify: `C:\Code\xiaozhi-esp32\main\audio\wake_words\custom_wake_word.h`
- Modify: `C:\Code\xiaozhi-esp32\main\audio\wake_words\custom_wake_word.cc`
- Modify: `C:\Code\xiaozhi-esp32\main\audio\wake_words\esp_wake_word.h`
- Modify: `C:\Code\xiaozhi-esp32\main\audio\wake_words\esp_wake_word.cc`
- Modify: `C:\Code\xiaozhi-esp32\main\application.cc`
- Modify: `C:\Code\xiaozhi-esp32\main\wake_arbiter_client.h`
- Modify: `C:\Code\xiaozhi-esp32\main\wake_arbiter_client.cc`

<action>

- 增加 `GetLastWakeRmsDbfs()` 接口。
- AFE/Custom 从 `wake_word_pcm_` 或同等滚动统计计算 RMS。
- ESP WakeWord 使用滚动 `sum_squares + sample_count` 统计计算 RMS，不补完整 PCM ring。
- `ContinueWakeWordArbitration()` 读取 RMS，非有限数直接停止本次唤醒。
- `/wake-detected` payload 加 `wake_rms_dbfs`。

<verify>

- `cd C:\Code\xiaozhi-esp32; python -m pytest tests/test_wake_arbitration_static.py -v`
- `cd C:\Code\xiaozhi-esp32; . C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1; idf.py build`

<done>

- ESP32 上报有效 `wake_rms_dbfs`。
- 固件构建通过。

## Task 3：gateway 配置模型

<files>

- Modify: `C:\Code\xiaozhi-gateway\app\models.py`
- Modify: `C:\Code\xiaozhi-gateway\app\config.py`
- Modify: `C:\Code\xiaozhi-gateway\config\devices.yaml`
- Modify: `C:\Code\xiaozhi-gateway\tests\test_config.py` if exists, otherwise add focused config tests.

<action>

- `DeviceMapping` 增加 `wake_group`、`priority`、`mic_gain_offset_db`。
- `WakeDetectedRequest` 增加必填 `wake_rms_dbfs`。
- 旧配置默认 `wake_group=key`、`priority=0`、`mic_gain_offset_db=0.0`。

<verify>

- `cd C:\Code\xiaozhi-gateway; uv run pytest tests -k "config or arbitration" -v`

<done>

- 配置可加载，新增字段有确定默认值。

## Task 4：gateway 同组仲裁

<files>

- Modify: `C:\Code\xiaozhi-gateway\app\arbitration.py`
- Modify: `C:\Code\xiaozhi-gateway\app\main.py`
- Modify: `C:\Code\xiaozhi-gateway\tests\test_arbitration.py`

<action>

- 新增 group candidate window。
- group 单设备直接允许。
- group 多设备等待 300ms。
- 仲裁窗口上限 500ms。
- 仲裁窗口结束时只比较已到达候选。
- 仲裁窗口使用 gateway 接收时间，不依赖 ESP32 timestamp。
- 使用 `adjusted_wake_rms_dbfs`、`priority`、`device_id` 确定获胜者。
- loser 返回明确 `deny_session` reason。
- 缺少或非法 RMS 返回 422。
- 仲裁超时返回 503。
- 记录结构化仲裁日志：group、winner、loser、原始/校准响度、priority、reason。

<verify>

- `cd C:\Code\xiaozhi-gateway; uv run pytest tests/test_arbitration.py -v`

<done>

- 同组串音只允许一台进入会话。

## Task 5：gateway 多 active session

<files>

- Modify: `C:\Code\xiaozhi-gateway\app\session_store.py`
- Modify: `C:\Code\xiaozhi-gateway\app\main.py`
- Modify: `C:\Code\xiaozhi-gateway\tests\test_active_context.py`
- Modify: `C:\Code\xiaozhi-gateway\tests\test_session_end.py`

<action>

- `SessionStore` 改成按 `device_id` 存储多个 active context。
- active session 增加 TTL，默认 120 秒。
- 读取、写入、结束 session 时清理过期项。
- `GET /active-context` 无 query 时：
  - 0 个 active 返回 inactive。
  - 1 个 active 返回 context。
  - 多个 active 返回 `multiple_active_contexts`。
- `GET /active-context?device_id=...` 返回指定设备 context。
- `/session/end` 只结束指定设备 session。

<verify>

- `cd C:\Code\xiaozhi-gateway; uv run pytest tests/test_active_context.py tests/test_session_end.py -v`

<done>

- 不同 wake group 可以并发 session，但无 query 的 active context 不猜。

## Task 6：ha-mcp active context ambiguity

<files>

- Modify: `C:\Code\ha-mcp-for-xiaozhi\custom_components\ws_mcp_server\gateway_context.py`
- Modify: `C:\Code\ha-mcp-for-xiaozhi\custom_components\ws_mcp_server\server.py`
- Modify: `C:\Code\ha-mcp-for-xiaozhi\tests\test_gateway_context.py`

<action>

- `parse_active_context()` 识别 `multiple_active_contexts`。
- 有显式 room/area 参数时，HA 工具调用跳过 active context 获取。
- 无显式 room/area 且 gateway 返回 multiple active 时，返回/抛出明确 `active_context_ambiguous`，不调用 HA 工具。

<verify>

- `cd C:\Code\ha-mcp-for-xiaozhi; pytest tests/test_gateway_context.py tests/test_pending_confirmation_tools.py -v`

<done>

- 多设备并发时不会把 HA 默认房间命令执行到错误房间。

## Task 7：文档

<files>

- Modify: `C:\Code\xiaozhi-esp32\docs\plans\2026-04-28-wake-arbitration-options.md`
- Modify: `C:\Code\xiaozhi-esp32\FLASHING_README.md`
- Modify: `C:\Code\xiaozhi-gateway\README.md`
- Modify: `C:\Code\ha-mcp-for-xiaozhi\README.md`

<action>

- 更新方案文档：第一阶段改为 `wake_group + wake_rms_dbfs + mic_gain_offset_db`。
- 说明 `devices.yaml` 配置例子。
- 说明多 active session 时 HA 默认房间上下文会显式冲突。
- 说明不做音频指纹。

<verify>

- `rg -n "wake_rms_dbfs|wake_group|mic_gain_offset_db|multiple_active_contexts" C:\Code\xiaozhi-esp32 C:\Code\xiaozhi-gateway C:\Code\ha-mcp-for-xiaozhi`

<done>

- 文档和实现行为一致。

## Task 8：完成前漂移检查

<files>

- `specs/003-req-grouped-wake-arbitration.md`
- `specs/003-spec-grouped-wake-arbitration.md`
- `specs/003-plan-grouped-wake-arbitration.md`
- 三个仓库的本次改动文件

<action>

- 对照 REQ-1 到 REQ-12。
- 对照 REQ-13 到 REQ-16。
- 确认没有上传唤醒音频。
- 确认没有恢复主动询问链路。
- 确认没有多 active context 下猜 HA 房间。
- 确认 active session 有 TTL 并会清理。
- 确认 gateway 仲裁日志足够排查 winner/loser。
- 确认没有新增未写入 spec 的行为。

<verify>

- `cd C:\Code\xiaozhi-gateway; uv run pytest -q`
- `cd C:\Code\ha-mcp-for-xiaozhi; pytest -q`
- `cd C:\Code\xiaozhi-esp32; python -m pytest tests -q`
- `cd C:\Code\xiaozhi-esp32; . C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1; idf.py build`

<done>

- req/spec/plan/实现/测试一致。

## 分层 Review

| 层 | 检查点 |
|---|---|
| 产品 review | 同组串音只响应一台；不同组并发行为符合预期。 |
| 工程 review | RMS 计算、group 仲裁、session store 并发边界清楚。 |
| 安全 review | 多 active session 不猜 HA 房间；不上传唤醒音频。 |
| 验证 review | ESP32 build、gateway tests、ha-mcp tests 都通过。 |

## 等待确认

收到用户明确“按这个实现”后，进入编码阶段。

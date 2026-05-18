# 007 实施计划：Voice PE HA media_player 播放输出

## 执行规则

| 规则 | 要求 |
|---|---|
| 不新 worktree | 严禁私自创建新 worktree。 |
| 不提交/推送/刷机 | 提交、推送、刷机前必须单独确认。 |
| 先文档确认 | 用户确认 007 req/spec/plan 后才开始编码。 |
| 不文本 TTS | 不能把小智回复文本交给 HA `tts.speak` 代替原始音频。 |
| 不默认转码 | 第一版优先 Ogg Opus 封装；不默认 MP3/AAC 转码。 |
| 不自由抢话 | HA 外放期间只保留唤醒词/按钮打断。 |
| 不静默兜底 | HA 播放失败时明确失败并恢复状态，不偷偷切本机播放或 HA 文本 TTS。 |
| 构建命令 | Voice PE 固件优先 `python scripts/release.py home-assistant-voice-pe`；裸 `idf.py build` 只允许在确认 `sdkconfig` 已重新生成且包含目标配置后使用。 |

## 并行执行关系

| 任务 | 关系 |
|---|---|
| Task 1/2 | 固件配置和配网页，可先做。 |
| Task 3 | 固件 client 可用 fake gateway 并行开发。 |
| Task 5 | gateway session/Ogg Opus 可用 fake ESP32 并行开发。 |
| Task 4a/4b | 依赖 Task 1 和 Task 3；4a 先做音频路由，4b 再做状态恢复。 |
| Task 6 | 依赖 Task 5 的 stream URL 和状态机。 |
| Task 7/8 | 依赖固件和 gateway 主链路贯通。 |

## Task 0：实施前检查

<files>

- `specs/007-req-voice-pe-ha-media-playback.md`
- `specs/007-spec-voice-pe-ha-media-playback.md`
- `main/application.cc`
- `main/application.h`
- `main/audio/audio_service.cc`
- `main/audio/audio_service.h`
- `main/gateway_url.h`
- `main/wake_arbiter_client.cc`
- `main/announcement_audio_client.*`
- `main/home_assistant_manager.*`
- `local_components/esp-wifi-connect/wifi_configuration_ap.cc`
- `local_components/esp-wifi-connect/include/wifi_configuration_ap.h`
- `local_components/esp-wifi-connect/assets/wifi_configuration.html`
- `C:\Code\xiaozhi-gateway\app\main.py` 或当前 gateway 入口文件

<action>

- 确认当前 `OnIncomingAudio()` 只支持本地 decode/playback。
- 确认当前 `tts start/stop` drain 逻辑不能直接代表 HA 播放结束。
- 确认 gateway URL 读取路径可复用。
- 确认 HA MQTT 只发布实体和文本，不直接调用 HA `media_player`。
- 确认 gateway 仓库位置、框架和现有 `/wake-detected`、`/session/end` API。
- 确认目标播放器所在 HA 能访问 gateway 返回的 stream URL；如果 gateway 在 Docker/NAS 后面，必须有可配置 external base URL。
- 确认 `AudioStreamPacket.payload` 到达 `Application::OnIncomingAudio()` 时已经完成小智协议层解密，后续上传 gateway 的必须是裸 Opus packet。

<verify>

- `git status --short`
- `rg -n "OnIncomingAudio|PushPacketToDecodeQueue|tts_output|ha_media_player|gateway_url|wake_arb_url|AnnouncementAudioClient|HomeAssistantManager" main local_components tests specs`
- 在 gateway 仓库运行现有测试命令。

<done>

- 明确 007 只新增第三播放输出，不改小智协议、不改 006 音频前端分工。

## Task 1：新增 HA playback 配置模型

<files>

- Create: `main/ha_playback_settings.h`
- Create: `main/ha_playback_settings.cc`
- Modify: `main/CMakeLists.txt`
- Test: `tests/test_ha_playback_static.py`

<action>

- 新增 `HaPlaybackSettings`，读取 NVS namespace `ha_playback`。
- 字段：
  - `tts_output`
  - `media_player_entity_id`
  - `timeout_ms`
  - `restore_listening`
  - `barge_in_mode`
  - `stream_format`
  - `initial_buffer_ms`
  - `local_volume_when_ha_output`
- 严格校验：
  - `tts_output` 只允许 `local` 或 `ha_media_player`
  - `ha_media_player` 模式下 `media_player_entity_id` 必填
  - `timeout_ms` 范围 10000..120000
  - `barge_in_mode` 第一版只允许 `wake_word_only`
  - `stream_format` 第一版只允许 `ogg_opus`
  - `initial_buffer_ms` 范围 300..1000
  - `local_volume_when_ha_output` 范围 0..100；该字段只用于 HA 外放期间提示音/异常本机输出保护，不代表同段 TTS 会走本机播放
- 无效配置必须让 HA 输出不可用，不能自动切文本 TTS。

<verify>

- `python -m pytest tests/test_ha_playback_static.py -q`
- `idf.py build` 或 release 构建时确认新增文件参与编译。

<done>

- 固件能读取和验证 HA playback 配置，默认仍是 `local`。

## Task 2：扩展配网页配置

<files>

- Modify: `local_components/esp-wifi-connect/include/wifi_configuration_ap.h`
- Modify: `local_components/esp-wifi-connect/wifi_configuration_ap.cc`
- Modify: `local_components/esp-wifi-connect/assets/wifi_configuration.html`
- Test: `tests/test_ha_playback_static.py`

<action>

- 在 `/advanced/config` 返回 `ha_playback` 对象。
- 在 `/advanced/submit` 保存 `ha_playback` 对象。
- 在高级配置 UI 新增 “HA playback” 小节，复用现有 input/select/checkbox 样式。
- UI 字段：
  - `tts_output`
  - `ha_media_player_entity_id`
  - `ha_playback_timeout_ms`
  - `restore_listening_after_playback`
  - `local_volume_when_ha_output`
- 文案必须说明 gateway URL 复用当前 gateway 配置。
- 不做 HA 实体列表拉取，不做播放器下拉搜索。

<verify>

- `python -m pytest tests/test_ha_playback_static.py -q`
- 手动打开配网页，确认移动端不溢出。
- POST 后重启，配置能从 NVS 读回。

<done>

- 用户可以不重新编译固件就切换 `local` / `ha_media_player`。

## Task 3：新增固件侧 HA playback client

<files>

- Create: `main/ha_playback_client.h`
- Create: `main/ha_playback_client.cc`
- Modify: `main/CMakeLists.txt`
- Test: `tests/test_ha_playback_static.py`

<action>

- 新增 `HaPlaybackClient`。
- 支持：
  - `CreateSession(settings, sample_rate, frame_duration_ms)`
  - `StartUpload()`
  - `SendFrame(packet_payload)`
  - `Finish()`
  - `Cancel()`
  - `WaitForResult(timeout_ms)`
- 创建 session 使用 `gateway_url::GetWakeArbitrationGatewayUrl()` 拼 `/playback/sessions`。
- 请求必须带 `Device-Id`、`Client-Id`。
- 通过 WebSocket `/playback/sessions/{session_id}/upload` 上传 binary Opus payload，并接收 `ha_playback_started/finished/failed`。
- 客户端必须记录低频日志：session id、frames、audio_ms、started/finished/failed、timeout。
- WebSocket 或 gateway 失败时返回失败，不本地兜底播放。

<verify>

- `python -m pytest tests/test_ha_playback_static.py -q`
- 构建确认无缺失依赖。
- 使用本地 fake gateway 验证创建 session、上传帧、收到 finished。

<done>

- 固件具备把小智 Opus 音频帧流式上传 gateway 的能力。

## Task 4a：接入 OnIncomingAudio 输出路由

<files>

- Modify: `main/application.h`
- Modify: `main/application.cc`
- Test: `tests/test_ha_playback_static.py`
- Test: `tests/test_home_assistant_voice_pe_static.py`

<action>

- `Application` 初始化或每次会话读取 `HaPlaybackSettings`。
- `OnIncomingAudio()`：
  - `local` 模式继续 `audio_service_.PushPacketToDecodeQueue()`。
  - `ha_media_player` 模式把已解密的裸 Opus payload 发送给 `HaPlaybackClient`，不得进入本地 decode/playback queue。
- 记录 HA 模式下发送帧数、音频毫秒数、首帧时间。
- 若 `HaPlaybackClient` 不存在或 session 未创建，明确失败并恢复状态，不本地兜底播放。

<verify>

- `python -m pytest tests/test_ha_playback_static.py tests/test_home_assistant_voice_pe_static.py -q`
- 静态检查：`ha_media_player` 分支不调用 `PushPacketToDecodeQueue()`。
- fake gateway：验证收到的是 binary Opus payload，不是文本、不带额外 header、不加密。

<done>

- HA 模式下同段小智 TTS 不进入本机播放队列。

## Task 4b：接入 TTS start/stop 状态管理

<files>

- Modify: `main/application.h`
- Modify: `main/application.cc`
- Modify if needed: `main/audio/audio_service.cc`
- Test: `tests/test_ha_playback_static.py`
- Test: `tests/test_home_assistant_voice_pe_static.py`

<action>

- `tts start`：
  - `local` 模式保持现有逻辑。
  - `ha_media_player` 模式创建 HA playback session；如果已有未完成 session，先 cancel 旧 session。
  - 记录当前本机音量，仅在需要防止提示音/异常本机输出时设置 `local_volume_when_ha_output`。
- `tts stop`：
  - `local` 模式保持现有 drain。
  - `ha_media_player` 模式发送 `Finish()`，等待 gateway `finished/failed/timeout` 后恢复状态。
- 结束、失败、取消时恢复原本机音量。
- `current_tts_text_` 和 assistant 文本发布继续保留，仅用于显示和日志，不用于 HA 重新 TTS。

<verify>

- `python -m pytest tests/test_ha_playback_static.py tests/test_home_assistant_voice_pe_static.py -q`
- fake gateway：模拟 `ha_playback_finished`，确认设备恢复 listening/idle。
- fake gateway：模拟 `ha_playback_failed`，确认设备不卡 speaking。
- fake gateway：模拟旧 session 未完成时新 `tts start`，确认旧 session 被 cancel。

<done>

- 同一段小智 TTS 可以按配置走本机或 HA 输出，状态恢复受 gateway 事件驱动。

## Task 5：实现 gateway playback session 和 Ogg Opus stream

<files>

- Modify/Create in `C:\Code\xiaozhi-gateway\app\*`
- Create/Modify tests in `C:\Code\xiaozhi-gateway\tests\*`

<action>

- 新增 session store，状态：`created`、`buffering`、`starting`、`playing`、`finished`、`failed`、`cancelled`。
- 同一 `device_id + client_id` 只允许一个非终态 session；新 session 创建时 cancel 旧 session，并向旧连接发送 `ha_playback_failed`，reason=`superseded`。
- 实现 `POST /playback/sessions`。
- 实现 `WebSocket /playback/sessions/{session_id}/upload`。
- 实现 `GET /playback/sessions/{session_id}/stream.ogg`。
- 实现 `DELETE /playback/sessions/{session_id}`。
- 实现 Ogg Opus muxer：
  - 写 `OpusHead`
  - 写 `OpusTags`
  - 按帧写 Ogg page
  - 按 48kHz 时间基累加 granule position，例如 60ms 帧增加 2880 samples
  - `OpusHead` 的 input sample rate 只作为来源信息，不能作为 Ogg granule position 时间基
- 初始缓冲默认 500ms。
- 播放器首次成功读取 `stream.ogg` body 数据后，向 Voice PE 发 `ha_playback_started`；HEAD、Range 探测或只建立连接不算 started。
- 输入 end 且 stream 被读完后，向 Voice PE 发 `ha_playback_finished`。
- 不拉流、HA 调用失败、stream 中断、超时，向 Voice PE 发 `ha_playback_failed`。
- 记录延迟时间点：session 创建、首帧、buffer ready、HA 调用、首次 body read、finished/failed。

<verify>

- 在 gateway 仓库运行：`pytest -q`
- 新增测试：
  - 创建 session 返回 `session_id/upload_url/stream_url`
  - 上传 Opus binary 后 `stream.ogg` 可读到 OggS header
  - 用 `opusinfo` 或 `ffprobe` 验证生成的 Ogg Opus 合规；若环境没有工具，用测试解析 `OpusHead`、`OpusTags`、Ogg page sequence 和 granule position
  - 不拉流会超时 failed
  - cancel 后 stream 终止
  - end 后能 finished
  - 同设备新 session 会 supersede 旧 session

<done>

- gateway 能边收 Voice PE Opus 帧，边向 HA/播放器提供 Ogg Opus 流。

## Task 6：gateway 调用 Home Assistant media_player

<files>

- Modify/Create in `C:\Code\xiaozhi-gateway\app\*`
- Modify gateway config, for example `C:\Code\xiaozhi-gateway\config\*.yaml`
- Create/Modify tests in `C:\Code\xiaozhi-gateway\tests\*`

<action>

- 增加 gateway 配置：
  - HA base URL
  - HA long-lived access token
  - public stream base URL，必须是 HA 和播放器能访问的地址，不能是 `127.0.0.1`、容器内地址或 ESP32 专用地址
- 在 session 初始缓冲达到阈值后调用 HA：
  - service: `media_player.play_media`
  - target/entity_id: `media_player_entity_id`
  - media_content_id: `stream_url`
  - media_content_type: `music` 或实测确认的 audio type
- HA REST 返回成功不等于播放开始；必须等 `stream.ogg` 被播放器请求。
- HA 调用失败必须进入 failed。
- 日志必须包含 entity id、stream URL host、session id、状态变化。
- 启动或首次创建 session 时校验 `public_stream_base_url`；缺失或明显不可用时返回 failed，不生成本地不可达 URL。

<verify>

- gateway `pytest -q`
- fake HA 测试：验证 REST 请求路径和 payload。
- public URL 测试：配置 Docker/NAS 场景下的 `public_stream_base_url`，确认 HA payload 不含 `127.0.0.1` 或容器内 host。
- 真 HA 手工测试：目标 `media_player` 收到播放请求。

<done>

- gateway 可以代表 ESP32 调 HA 播放指定播放器。

## Task 7：HA 外放期间监听、打断和超时

<files>

- Modify: `main/application.cc`
- Modify: `main/application.h`
- Modify if needed: `main/audio/audio_service.cc`
- Modify/Create: `main/ha_playback_client.*`
- Test: `tests/test_ha_playback_static.py`

<action>

- HA 外放期间设备状态为 speaking，但不上传 ASR 音频。
- 保留本地 wake word detection；mute 打开时仍阻止唤醒。
- 按钮或唤醒词打断时：
  - 调 `HaPlaybackClient::Cancel()`
  - gateway 停止 HA stream
  - 执行现有 `AbortSpeaking()` 或等价打断路径
  - 进入新一轮小智交互
- `timeout_ms` 到期还没 finished/failed 时，固件主动 cancel 并恢复状态。
- 恢复 listening/idle 时不能清错本地 decode queue；HA 模式下本地 TTS queue 应本来为空。

<verify>

- `python -m pytest tests/test_ha_playback_static.py tests/test_home_assistant_voice_pe_static.py -q`
- fake gateway：不返回 finished，确认 timeout 后恢复。
- 硬件：HA 播放期间说唤醒词，当前外放停止并进入新会话。
- 硬件：HA 播放期间按按钮，当前外放停止并进入新会话。

<done>

- HA 外放不会卡状态，且第一版打断边界清楚。

## Task 8：构建和硬件验收

<files>

- No source changes unless verification exposes defects.

<action>

- 先跑静态测试。
- 构建 Voice PE。
- 刷机前单独确认。
- 配置：
  - gateway URL
  - `tts_output=ha_media_player`
  - `ha_media_player_entity_id=media_player.xxx`
- 依次测试：
  - local 模式本机播放
  - HA 模式指定播放器播放
  - Ogg Opus 不支持时明确失败
  - gateway 不可用时恢复状态
  - HA 调用失败时恢复状态
  - 播放期间不上传 ASR
  - 按钮/唤醒词打断
  - 播放结束恢复 listening/idle
  - 记录首字延迟；超过 3 秒时拆分 Voice PE 首帧、gateway buffer、HA REST、播放器首次 body read 的耗时

<verify>

- `python -m pytest tests/test_ha_playback_static.py tests/test_home_assistant_voice_pe_static.py tests/test_runtime_settings_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- gateway 仓库：`pytest -q`
- 串口日志记录：session id、frames、audio_ms、started/finished/failed、timeout。
- gateway 日志记录：HA play_media、stream pull、状态变化。
- gateway 日志记录：首字延迟拆分时间点。

<done>

- AC-1..AC-11 全部通过。

## Task 9：完成前漂移检查

<files>

- `specs/007-req-voice-pe-ha-media-playback.md`
- `specs/007-spec-voice-pe-ha-media-playback.md`
- `specs/007-plan-voice-pe-ha-media-playback.md`
- `main/application.*`
- `main/ha_playback_*`
- `local_components/esp-wifi-connect/*`
- `C:\Code\xiaozhi-gateway\app\*`
- `tests/test_ha_playback_static.py`

<action>

- 逐条核对 REQ-1..REQ-24。
- 确认没有引入 HA 文本 TTS 代替小智音频。
- 确认没有默认转码 MP3/AAC。
- 确认没有实现 HA 外放自由抢话。
- 确认没有让 ESP32 直接持有 HA token 或直接调用 HA API。
- 确认 Voice PE 上传 gateway 的是已解密裸 Opus packet。
- 确认同设备并发 session 有 supersede/cancel 策略。
- 确认 gateway 返回的 stream URL 使用 `public_stream_base_url`。
- 确认首字延迟有观测日志。
- 确认普通板卡和 Voice PE `local` 模式不受影响。
- Review 查 Bug，然后按第一性原理分析是否有更简单、更稳健的实现。

<verify>

- `python -m pytest tests/test_ha_playback_static.py tests/test_home_assistant_voice_pe_static.py tests/test_runtime_settings_static.py -q`
- gateway 仓库：`pytest -q`
- `rg -n "tts\\.speak|text-to-speech|media_player|ha_media_player|PushPacketToDecodeQueue|HaPlayback|ogg_opus|mp3|aac|transcode" main local_components tests specs C:\Code\xiaozhi-gateway`

<done>

- 代码、gateway、req、spec、plan 和硬件验收一致。

## 分层 Review

| Review | 检查 |
|---|---|
| 产品 review | 是否真的是第三播放输出，而不是 HA 重新 TTS |
| 音频 review | 是否保留小智原始 TTS 音色，是否优先 Ogg Opus 封装 |
| 状态机 review | Voice PE 是否只在 gateway finished/failed/timeout 后恢复状态 |
| Gateway review | 是否有 started/playing/finished/failed/timeout 闭环 |
| HA review | `media_player.play_media` 是否由 gateway 调用，ESP32 是否不持有 HA token |
| AEC/打断 review | HA 外放期间是否暂停 ASR，只保留唤醒词/按钮打断 |
| UI review | 配网页是否复用现有高级配置页，没有新建复杂前端 |
| 验证 review | 静态测试、gateway 单测、构建、硬件 AC 是否都有结果 |

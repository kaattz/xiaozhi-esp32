# 007 需求：Voice PE HA media_player 播放输出

## 背景

Voice PE 现在有两种本地播放输出：板载喇叭和 3.5mm 外接音箱。007 新增第三种播放输出：把小智服务器下发的 TTS 音频流交给 Home Assistant 的某个 `media_player` 播放，例如 Beosound、Sonos 或 HA 管理的蓝牙音箱。

007 的核心不是把回复文本交给 HA 重新 TTS，而是保留小智原始 TTS 音频流和音色，只改变播放设备。

## 入口判定

这是中大型 feature：

| 原因 | 说明 |
|---|---|
| 跨系统 | 涉及 Voice PE 固件、playback gateway、Home Assistant 和外部 `media_player` |
| 改变播放状态机 | TTS 不再只由本机 decode/playback queue 决定完成时间 |
| 涉及持久化配置 | 需要配置输出模式、目标实体、超时和监听恢复策略 |
| 涉及外部输入输出 | ESP32 需要把 TTS 音频帧流式上传到 gateway，gateway 再给 HA 播放 |
| 涉及 AEC/打断边界 | HA 外部音箱播放时 XU316 拿不到播放 reference，不能按本机喇叭路径做自由抢话 |

## 用户故事

| 编号 | 用户故事 |
|---|---|
| US-1 | 作为用户，我希望 Voice PE 可以把小智回复播放到 HA 里的指定音箱，而不是只能用本机喇叭或 3.5mm。 |
| US-2 | 作为用户，我希望 HA 播放时仍然保留小智 TTS 的原始音色。 |
| US-3 | 作为用户，我希望可以配置目标 `media_player`，例如 `media_player.beosound2`。 |
| US-4 | 作为用户，我希望 HA 音箱播放结束后，Voice PE 能自动恢复到正确的 listening 或 idle 状态。 |
| US-5 | 作为维护者，我希望 gateway 能追踪 HA 播放开始、播放中、结束、失败和超时，而不是只把 URL 发给 HA。 |

## 功能需求

| 编号 | 需求 |
|---|---|
| REQ-1 | Voice PE 必须支持 `local` 和 `ha_media_player` 两种 TTS 输出模式。 |
| REQ-2 | 默认输出模式必须是 `local`，不能影响现有板载喇叭和 3.5mm 输出。 |
| REQ-3 | `ha_media_player` 模式必须保留小智服务器下发的原始 TTS 音频内容和音色，不能把回复文本交给 HA `tts.speak` 重合成。 |
| REQ-4 | 第一版 HA 输出必须优先使用小智 Opus 帧的流式封装，例如 Ogg Opus，不默认转码 MP3/AAC。 |
| REQ-5 | 如果目标播放器不支持 Ogg Opus，第一版必须明确失败并记录原因；转码兼容可以作为后续能力，不能偷偷兜底。 |
| REQ-6 | Voice PE 必须把 TTS 音频边收边转发给 gateway，不能等待完整 TTS 收完再播放。 |
| REQ-7 | gateway 必须边接收 Voice PE 音频帧，边暴露可被 HA/播放器拉取的流式 URL。 |
| REQ-8 | gateway 必须调用 HA `media_player.play_media`，让指定 `media_player` 播放流式 URL。 |
| REQ-9 | gateway 必须追踪播放状态：开始播放、播放中、播放结束、播放失败、超时。 |
| REQ-10 | gateway 必须通知 Voice PE：`ha_playback_started`、`ha_playback_finished`、`ha_playback_failed`。 |
| REQ-11 | Voice PE 必须基于 gateway 状态事件决定何时恢复 listening/idle，不能只靠本地 TTS stop。 |
| REQ-12 | HA 外放期间 Voice PE 必须暂停 ASR 音频上传。 |
| REQ-13 | HA 外放期间第一版只允许唤醒词或按钮打断，不支持自由抢话。 |
| REQ-14 | HA 外放被唤醒词或按钮打断时，Voice PE 必须通知 gateway 停止 HA 播放，并中断当前小智 speaking。 |
| REQ-15 | HA 外放期间同一段 TTS 不得进入本机 decode/playback queue；如果存在提示音或其它本机音频副作用，必须被静音或明确阻止，避免和 HA 外放混音。 |
| REQ-16 | 配置页必须能设置 `tts_output` 和 `ha_media_player_entity_id`。 |
| REQ-17 | 第一版必须复用当前 gateway URL 配置；如果新增独立字段，必须说明优先级和空值行为。 |
| REQ-18 | 配置必须包含 HA 播放超时，默认 60000ms。 |
| REQ-19 | 配置必须包含播放结束后是否恢复监听，默认 true。 |
| REQ-20 | HA 外放失败或超时时，Voice PE 必须停止等待并恢复到安全状态，不能卡在 speaking。 |
| REQ-21 | 普通非 Voice PE 板卡必须保持原有本地播放流程。 |
| REQ-22 | 007 不改变小智服务器协议，不要求小智云支持 HA 播放。 |
| REQ-23 | 007 不改变 006 的 XU316 麦克风前端 DSP 分工和 channel 分工。 |
| REQ-24 | 第一版不设硬性首字延迟指标；如果实测用户说完话到 HA 音箱首字出声超过 3 秒，必须记录各环节耗时并评估是否降低初始缓冲或优化 gateway/HA 播放路径。 |

## 验收标准

| 编号 | 验收 |
|---|---|
| AC-1 | 默认配置下，Voice PE 仍走本机喇叭/3.5mm 本地播放。 |
| AC-2 | 配置 `tts_output=ha_media_player` 和目标实体后，小智回复从 HA 指定 `media_player` 发声。 |
| AC-3 | HA 播放的小智回复音色与本机播放同源，不是 HA 重新 TTS 的声音。 |
| AC-4 | gateway 日志能看到同一次播放的 session 创建、HA play_media 调用、播放器拉流、播放结束或失败。 |
| AC-5 | Voice PE 日志能看到 `ha_playback_started` 和 `ha_playback_finished` 后恢复 listening/idle。 |
| AC-6 | HA 播放期间无人说话时，Voice PE 不上传 ASR 音频。 |
| AC-7 | HA 播放期间按按钮或说本地唤醒词能停止当前 HA 播放并进入新交互。 |
| AC-8 | gateway 不可用、HA 调用失败、播放器不拉流或播放超时时，Voice PE 不会卡在 speaking。 |
| AC-9 | Ogg Opus 不被目标播放器支持时，系统明确失败；不静默转成文本 TTS。 |
| AC-10 | 普通板卡和 Voice PE `local` 模式现有播放测试通过。 |
| AC-11 | HA 外放实测记录首字延迟；如果超过 3 秒，日志能拆出至少 `tts_start`、首帧到达、session 创建、HA 调用、播放器首个 body read、播放完成耗时。 |

## 界面约束

| 项 | 约束 |
|---|---|
| 页面位置 | 复用现有配网页/高级配置页，不新建独立前端。 |
| 样式 | 复用 `local_components/esp-wifi-connect/assets/wifi_configuration.html` 现有表单样式。 |
| 字段 | 至少包含 `tts_output`、`ha_media_player_entity_id`、`ha_playback_timeout_ms`、`restore_listening_after_playback`；如果保留本机音量保护配置，必须说明它不是用来静音同段 TTS，而是防止提示音/误入本地播放路径混音。 |
| 非目标 | 不做播放器选择器，不拉取 HA 实体列表，不做实时播放进度 UI。 |

## 非目标

| 非目标 | 原因 |
|---|---|
| 不把文本交给 HA 重新 TTS | 会丢失小智音色。 |
| 第一版不默认转码 MP3/AAC | 转码增加延时和服务端复杂度，先验证 Ogg Opus 低延时路径。 |
| 第一版不做自由抢话 | HA 外部音箱播放时 XU316 拿不到播放 reference。 |
| 第一版不做 `both` 同时播放 | 本机和外部音箱不同步，会放大回声和状态复杂度。 |
| 不让 ESP32 直连 Sonos/Beosound/蓝牙音箱 | 协议复杂，应该交给 HA 管理。 |
| 不让 ESP32 直接调用 HA API | 认证、URL 暴露、HTTPS 和服务调用不应压到 ESP32 上。 |
| 不改小智协议 | 小智仍只负责下发 TTS 音频，输出目标由设备/gateway 决定。 |

## 需求追踪

| 需求 | Spec 章节 | 验证方式 |
|---|---|---|
| REQ-1..REQ-2 | 输出模式 | AC-1/AC-10 |
| REQ-3..REQ-5 | 音频格式 | AC-3/AC-9 |
| REQ-6..REQ-8 | 流式播放架构 | AC-2/AC-4 |
| REQ-9..REQ-11 | 状态闭环 | AC-4/AC-5 |
| REQ-12..REQ-15 | HA 外放监听和打断 | AC-6/AC-7 |
| REQ-16..REQ-19 | 配置和界面 | 静态检查/手工配置 |
| REQ-20 | 错误处理 | AC-8 |
| REQ-21 | 回归 | AC-10 |
| REQ-22..REQ-23 | 非目标和边界 | drift check |
| REQ-24 | 延迟观测 | AC-11 |

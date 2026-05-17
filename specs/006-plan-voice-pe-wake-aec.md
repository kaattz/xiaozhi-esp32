# 006 实施计划：Voice PE 本地唤醒词与 AEC

## 执行规则

| 规则 | 要求 |
|---|---|
| 不新 worktree | 严禁私自创建新 worktree。 |
| 不提交/推送/刷机 | 提交、推送、刷机前必须单独确认。 |
| 不改协议 | 不改小智 WebSocket/MQTT 协议，不新增 Voice PE 专用协议。 |
| 不做非目标 | 不做 Grove、电源扩展、XMOS DFU、耳机路由、自定义唤醒词。 |
| 不假装 AEC | 没有真实 reference channel，不允许启用或宣称 AEC。 |
| 先唤醒后 AEC | 先证明“你好小智”本地唤醒，再接入 AEC reference。 |
| 硬件失败即暂停 | 唤醒模型缺失、reference 无效或 AFE 输入格式错误时，暂停并更新 Spec。 |
| 构建命令 | 006 的有效构建必须让 `config.json` 写入 `sdkconfig`。优先用 `python scripts/release.py home-assistant-voice-pe`；裸 `idf.py build` 只允许在确认 `sdkconfig` 已重新生成且包含 006 配置后使用。手工等价验证时必须按 `release.py` 顺序先写入 `CONFIG_BOARD_TYPE_HOME_ASSISTANT_VOICE_PE=y`，再写入 `sdkconfig_append`，否则 `CONFIG_USE_DEVICE_AEC=y` 会因依赖不满足被 Kconfig 改回关闭。 |

## Task 0：实施前检查

<files>

- `specs/006-req-voice-pe-wake-aec.md`
- `specs/006-spec-voice-pe-wake-aec.md`
- `docs/plans/2026-05-17-voice-pe-wake-aec-design.md`
- `main/boards/home-assistant-voice-pe/config.json`
- `main/boards/home-assistant-voice-pe/config.h`
- `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc`
- `main/audio/audio_service.cc`
- `main/audio/wake_words/afe_wake_word.cc`
- `main/audio/processors/afe_audio_processor.cc`
- `managed_components/espressif__esp-sr/Kconfig.projbuild`

<action>

- 读取 006 req/spec/design。
- 确认“你好小智”模型存在。
- 确认 Voice PE 当前 `CONFIG_WAKE_WORD_DISABLED=y`、`input_reference_=false`、`input_channels_=1`。
- 确认 005 mute 仍是本地唤醒的硬门禁。
- 确认 AEC 必须使用 `M,R` 输入，不接受单 mic 通道。
- 确认 Voice PE 通道分工和实测差异：官方 Voice PE `voice_assistant` 使用 channel 0 / AGC，但本项目 004 阶段已经存在主观背景噪声，006 不能只靠切换到 channel 1 / NS 宣称解决。必须先记录 raw mic RMS 和 AFE output RMS。
- 确认 `Board` 当前没有通用 mute 查询接口，需要新增默认 false 的 `IsMicrophoneMuted()`。
- 确认启动日志可记录 free PSRAM；WakeNet + AFE + reference buffer 后 PSRAM 不足时暂停。
- 确认 `AudioService` 现有重采样器使用 `esp_ae_rate_cvt_open/process`，006 reference 重采样复用同一组件。

<verify>

- `git status --short`
- `rg -n "NIHAOXIAOZHI|CONFIG_WAKE_WORD_DISABLED|CONFIG_USE_AFE_WAKE_WORD|CONFIG_USE_DEVICE_AEC|input_reference_|input_channels_" main managed_components specs`
- 硬件启动日志：记录 free PSRAM。

<done>

- 明确施工只做本地唤醒词和 AEC。

## Task 1：启用“你好小智”预置唤醒词

<files>

- Modify: `main/boards/home-assistant-voice-pe/config.json`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 移除 `CONFIG_WAKE_WORD_DISABLED=y`。
- 增加：
  - `CONFIG_USE_AFE_WAKE_WORD=y`
  - `CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y`
  - `CONFIG_SEND_WAKE_WORD_DATA=y`
- 明确不增加 `CONFIG_USE_CUSTOM_WAKE_WORD`。
- 静态测试检查 Voice PE 没有继续禁用 wake word，且启用“你好小智”模型。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`

<done>

- 固件构建包含“你好小智”本地唤醒模型。

## Task 2：接入本地唤醒与 mute 互斥验收

<files>

- Modify if needed: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `main/boards/common/board.h`
- Modify if needed: `main/application.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 优先复用现有 `Application` wake word flow，不新增协议。
- 在 `Board` 增加 `virtual bool IsMicrophoneMuted() { return false; }`。
- Voice PE override `IsMicrophoneMuted()`，返回 debounced mute 状态。
- 在 `Application::HandleWakeWordDetectedEvent()` 开头读取 `Board::GetInstance().IsMicrophoneMuted()`；muted 为 true 时关闭 wake word detection 并返回，不进入 listening，不上传音频。
- Voice PE mute 状态变为 true 时，在主任务停止 wake word detection；mute 关闭且设备处于 idle/speaking 时恢复 wake word detection。
- 不改变中间按钮现有行为。
- 硬件测试记录 30 分钟 idle 误唤醒次数；如果折算每小时超过 3 次，暂停并评估 WakeNet 配置。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 硬件：idle 说“你好小智”进入 listening 并完成一次小智回复。
- 硬件：mute 打开后说“你好小智”不进入 listening。
- 硬件：中间按钮单击仍可问答。
- 硬件：30 分钟安静/日常环境观察误唤醒次数。

<done>

- 本地唤醒词可用，且受 mute 保护。

## Task 3：实现 playback reference 缓冲

<files>

- Modify: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.h`
- Modify: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 在 `VoicePeAudioCodec` 中增加 48 kHz 到 16 kHz 的 ESP audio resampler，用于 reference。
- `Write()` 将实际播放 PCM 写入 reference ring buffer。
- reference 存储为 16 kHz int16 mono。
- reference ring buffer 初始容量为 3200 samples，即 200ms @ 16 kHz；不额外增加播放路径延迟，按官方小智软件 reference 方式在播放写入后立即进入 FIFO；溢出时丢最旧数据并限频记录。
- Task 3 只实现 reference buffer、重采样和 RMS 诊断；不得修改 `input_reference_`、`input_channels_`，不得让 `Read()` 输出双通道。
- reference buffer 只代表已送往扬声器的数据；不从服务器包或固定波形伪造。
- 保持 mic 的 `SaturateMicSample()` 和增益不变；wake word、voice processing 和 audio testing 先使用 channel 1 / NS。不要同时降低输入增益和切通道；若安静时 `raw_rms` 也高，再单独评估输入增益；若 `raw_rms` 低但 `out_rms` 高，再回查 AFE/AEC 配置。
- 增加 reference RMS 诊断日志。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 硬件：播放测试音时 reference RMS 非零。
- 硬件：无播放时 reference RMS 接近零。

<done>

- AEC 所需 reference channel 有真实数据来源。

## Task 4：切换 Voice PE AFE 输入为 `M,R`

<files>

- Modify: `main/boards/home-assistant-voice-pe/config.h`
- Modify: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc`
- Modify if needed: `main/audio/processors/afe_audio_processor.cc`
- Modify if needed: `main/audio/wake_words/afe_wake_word.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 设置 `AUDIO_INPUT_REFERENCE true`。
- `VoicePeAudioCodec` 设置：
  - `input_reference_=true`
  - `input_channels_=2`
- `Read()` 在本任务才切换为输出 interleaved `mic,reference`，避免 Task 3/4 中间状态让 AFE 按单通道误读双通道数据。
- 对齐策略：每次 `Read()` 读到 N 帧 mic 后，从 reference FIFO 中直接取 N 帧；不足补 0 并计数，超量保留最近 200ms；超过 300ms 没有新播放 PCM 时清空旧 reference。
- 在 AFE 初始化日志里打印 input format，必须能看到 `MR`。
- 确认 wake word 和 voice processor 都按双通道 feed size 处理。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 硬件日志：AFE input format 为 `MR` 或等价明确输出。
- 硬件：本地唤醒仍成功。

<done>

- AFE 收到 mic + reference，不再是单 mic 假 AEC。

## Task 5：启用设备端 AEC

<files>

- Modify: `main/boards/home-assistant-voice-pe/config.json`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 增加 `CONFIG_USE_DEVICE_AEC=y`。
- 确认没有启用 `CONFIG_USE_SERVER_AEC`。
- 确认 `AfeAudioProcessor` 初始化时 AEC enabled，且输入格式为 `MR`。
- `WaitForPlaybackQueueEmpty()` 必须等待当前 active playback chunk 结束，并循环复查迟到的 decode/playback 包；在 `input_reference()` 设备上保留 700ms 播放尾音保护窗，防止 speaking 刚切 listening 就上传本机 TTS 尾音。
- listening 状态也必须接收服务器迟到的 TTS 音频包；只要本地仍有 decode/playback queue、active playback，或 `input_reference()` 设备仍处于 700ms 播放尾音保护窗内，就暂停麦克风上传。
- 进入 speaking 状态时不能清空 decode/playback queue；服务器 TTS 音频包可能先于 `tts start` JSON 到达，清队列会造成“只播一个字”或句子截断。
- `EnableVoiceProcessing(true)` 不能隐式清空 decode/playback queue；清播放队列必须由明确的新会话入口或停止路径显式完成，否则会清掉 listening 状态刚接收的迟到 TTS 音频。
- TTS stop 处理必须先 drain playback，再切 listening/idle；不能先切 listening 后等待，否则迟到的 TTS 音频会被 `OnIncomingAudio` 按 listening 状态丢弃。
- Voice PE 当前默认 `auto` 收音模式，speaking 阶段不运行服务器上传链路；TTS 播放收尾后、切回 listening 前，必须重置本地 voice processor/AFe 缓冲，并清理上行 encode queue 和 send queue 中的残留语音帧。当前阶段只保留本地唤醒词或中心按钮打断。自由边播边听必须等 reference 延迟实测或硬件回采证明可靠后再启用。
- 遵循官方小智 realtime 连续流语义：只有新开收音窗口、播放提示音入口或本地 voice processor 未运行时才发送 `listen/start` 并启动 voice processor；从 TTS stop 回到 `listening` 时如果 processor 已在运行，不能重复重置流、清空上行队列或重新发送 `listen/start`，否则会打断多段助手回复和工具结果。
- 如果 reference RMS 或输入格式不满足条件，撤回本任务并更新 Spec。
- 记录纯播放无人说话 30 秒内是否产生用户 ASR 文本；产生则 AEC 验收失败。
- 播放期间限频记录 output peak/RMS/volume；用户报告播放中电流声时，先用 peak 判断是否数字削波。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 硬件：播放期间无人说话时，小智不应把自身播报识别成用户语音。
- 硬件：播放期间说“你好小智”或按中心按钮，仍可进入新的交互。
- 硬件：纯播放无人说话 30 秒内，小智 Server 不返回用户 ASR 文本。
- 硬件：听到播放中电流声时，记录同时间段 `output probe` 的 peak/RMS/volume。

<done>

- 设备端 AEC 可用，且有日志证明 reference 真实存在。

## Task 6：硬件回归

<files>

- Modify only if needed: `main/boards/home-assistant-voice-pe/*`

<action>

- 刷机前单独确认。
- 依次验收：
  - 本地“你好小智”唤醒。
  - mute 打开阻止本地唤醒。
  - 按钮问答。
  - AEC reference RMS。
  - 播放期间说话的 AEC 主观效果。
  - 纯播放无人说话 30 秒 ASR 空结果。
  - 30 分钟误唤醒观察。
  - 005 LED/mute/旋钮。

<verify>

- 串口日志记录：
  - WakeNet 模型。
  - wake word detected。
  - AFE input format。
  - reference RMS。
  - free PSRAM。
  - 误唤醒次数。
  - 小智连接和 TTS 播放。

<done>

- AC-1..AC-8 全部通过，误唤醒未超过每小时 3 次。

## Task 7：完成前漂移检查

<files>

- `specs/006-req-voice-pe-wake-aec.md`
- `specs/006-spec-voice-pe-wake-aec.md`
- `specs/006-plan-voice-pe-wake-aec.md`
- `main/boards/home-assistant-voice-pe/*`
- `main/audio/*`
- `tests/test_home_assistant_voice_pe_static.py`

<action>

- 逐条核对 REQ-1..REQ-15。
- 逐条核对 REQ-16。
- 确认没有加入自定义唤醒词。
- 确认没有做 Grove、电源扩展、XMOS DFU、耳机路由。
- 确认没有改小智协议。
- 确认没有改变 mic 转换、输入增益和 RMS 口径；确认 Voice PE 唤醒/STT slot 分工仍与官方一致。
- 确认 AEC 不是单通道假实现。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- `rg -n "CONFIG_USE_CUSTOM_WAKE_WORD|CONFIG_USE_SERVER_AEC|CONFIG_WAKE_WORD_DISABLED|CONFIG_USE_DEVICE_AEC|NIHAOXIAOZHI|input_reference_|input_channels_|IsMicrophoneMuted|esp_ae_rate_cvt" main/boards/home-assistant-voice-pe main/boards/common main/audio tests specs/006-req-voice-pe-wake-aec.md specs/006-spec-voice-pe-wake-aec.md specs/006-plan-voice-pe-wake-aec.md`

<done>

- 代码、req、spec、plan 和硬件验收一致。

## 分层 Review

| Review | 检查 |
|---|---|
| 产品 review | 006 是否只做“你好小智”本地唤醒和 AEC |
| 工程 review | 是否复用 ESP-SR/AfeWakeWord/Application 现有流程 |
| 音频 review | reference 是否真实、16 kHz、interleaved `M,R` |
| 硬件 review | 唤醒、mute、AEC reference 和播放期间说话是否通过实机 |
| 验证 review | 静态测试、构建、硬件 AC 是否都有结果 |

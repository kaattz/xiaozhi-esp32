# 006 实施计划：Voice PE 本地唤醒词与 XU316 前端 DSP

## 执行规则

| 规则 | 要求 |
|---|---|
| 不新 worktree | 严禁私自创建新 worktree。 |
| 不提交/推送/刷机 | 提交、推送、刷机前必须单独确认。 |
| 不改协议 | 不改小智 WebSocket/MQTT 协议，不新增 Voice PE 专用协议。 |
| 不做非目标 | 不做 Grove、电源扩展、XMOS DFU、耳机路由、自定义唤醒词。 |
| 不冒充 XU316 AEC | 不启用 ESP32 device AEC 或 server AEC 来冒充 XU316 前端 DSP。 |
| 先边界后效果 | 先证明 XU316 初始化和 channel stage 正确，再验收播放参考路径和回声效果。 |
| 对齐官方 channel | 唤醒必须走 XMOS channel 1 / NS；语音处理和上传必须走 XMOS channel 0 / AGC。 |
| 硬件失败即暂停 | 唤醒模型缺失、XU316 初始化失败、stage 写入失败、播放参考路径无法证明时，暂停并更新 Spec。 |
| 构建命令 | 006 的有效构建必须让 `config.json` 写入 `sdkconfig`。优先用 `python scripts/release.py home-assistant-voice-pe`；裸 `idf.py build` 只允许在确认 `sdkconfig` 已重新生成且包含 006 配置后使用。Voice PE 最终配置必须显式排除 `CONFIG_USE_DEVICE_AEC` 和 `CONFIG_USE_SERVER_AEC`。 |

## Task 0：实施前检查

<files>

- `specs/006-req-voice-pe-wake-aec.md`
- `specs/006-spec-voice-pe-wake-aec.md`
- `docs/plans/2026-05-17-voice-pe-wake-aec-design.md`
- `main/boards/home-assistant-voice-pe/config.json`
- `main/boards/home-assistant-voice-pe/config.h`
- `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc`
- `main/boards/home-assistant-voice-pe/voice_pe_xmos.*`
- `main/audio/audio_service.cc`
- `main/audio/wake_words/afe_wake_word.cc`
- `main/audio/processors/afe_audio_processor.cc`
- `managed_components/espressif__esp-sr/Kconfig.projbuild`

<action>

- 读取 006 req/spec/design。
- 确认“你好小智”模型存在。
- 确认 Voice PE 当前已启用本地唤醒。
- 确认当前代码仍有 ESP32 AFE AEC 路径：`CONFIG_USE_DEVICE_AEC`、`AUDIO_INPUT_REFERENCE`、`input_reference_`、`input_channels_=2`、reference FIFO 或等价 `MR` 输入。
- 确认目标实现必须把 Voice PE 主上传链路改回 XU316 处理后的单路 mic，不继续把 playback reference 喂给 ESP32 AFE 做 AEC。
- 确认 `duplex_` 需要和 `input_reference_` 解耦：保留同时播放/采集能力，但 `input_reference_=false`、`input_channels_=1` 或等价实现。
- 确认官方 Voice PE 通道分工：`micro_wake_word` 使用 channel 1 / NS，`voice_assistant` 使用 channel 0 / AGC。
- 确认 XU316 pipeline stage 目标：channel 0 = AGC，channel 1 = NS。
- 确认本 feature 只切换用途对应的 slot，不同时修改输入增益、32-bit 到 int16 转换和 RMS 口径。若 AGC 通道噪声偏高，先记录 raw mic RMS 和 AFE output RMS，再另开调参。
- 确认 005 mute 仍是本地唤醒的硬门禁。
- 确认 `Board` 当前没有通用 mute 查询接口，需要新增默认 false 的 `IsMicrophoneMuted()`。
- 确认启动日志可记录 free PSRAM；WakeNet + AFE 后 PSRAM 不足时暂停。

<verify>

- `git status --short`
- `rg -n "NIHAOXIAOZHI|CONFIG_WAKE_WORD_DISABLED|CONFIG_USE_AFE_WAKE_WORD|CONFIG_USE_DEVICE_AEC|CONFIG_USE_SERVER_AEC|AUDIO_INPUT_REFERENCE|input_reference_|input_channels_|kWakeWordMicSlot|kVoiceMicSlot" main managed_components specs`
- 硬件启动日志：记录 free PSRAM。

<done>

- 明确施工只做本地唤醒词、官方 channel 分工和 XU316 前端 DSP 边界。

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

## Task 3：对齐 XU316 初始化与 pipeline stage

<files>

- Modify if needed: `main/boards/home-assistant-voice-pe/voice_pe_xmos.h`
- Modify if needed: `main/boards/home-assistant-voice-pe/voice_pe_xmos.cc`
- Modify if needed: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 确认 XU316 初始化路径能读取版本或等价设备状态。
- 初始化后写入官方 pipeline stage：
  - channel 0 = AGC
  - channel 1 = NS
- 启动日志打印 XU316 初始化结果、版本、stage 写入结果。
- 保留 ESP32 输出 I2S 路径，让 TTS/提示音 PCM 继续走官方播放路径。
- 不实现或保留 ESP32 playback reference FIFO 作为 AEC 依据；若代码里已有 reference FIFO，只能删除或降为非 AEC 诊断，不能作为完成标准。
- 若无法证明 XU316 能使用当前播放路径作为回声参考，暂停并更新 Spec，不继续宣称 XU316 AEC 完成。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 静态测试：存在 `WritePipelineStages()` 或等价写 stage 逻辑，且默认 channel 0 = AGC、channel 1 = NS。
- 硬件日志：XU316 初始化成功，stage 写入成功。
- 硬件：TTS/提示音播放正常。

<done>

- XU316 前端 DSP 初始化边界明确，ESP32 不再用 reference FIFO 充当 AEC 实现。

## Task 4：对齐官方 channel 分工并取消 ESP32 AFE AEC 输入

<files>

- Modify: `main/boards/home-assistant-voice-pe/config.h`
- Modify: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc`
- Modify if needed: `main/audio/processors/afe_audio_processor.cc`
- Modify if needed: `main/audio/wake_words/afe_wake_word.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 对齐官方 ESPHome channel 用法：
  - `kWakeWordMicSlot = 1`
  - `kVoiceMicSlot = 0`
  - `AudioInputPurpose::kWakeWord` 选择 slot 1 / NS
  - `AudioInputPurpose::kVoiceProcessing` 选择 slot 0 / AGC
  - `AudioInputPurpose::kAudioTesting` 默认选择 slot 0 / AGC，用来测试实际上传给小智的同一路音频
- 设置或等价实现：
  - `duplex_=true`
  - `input_reference_=false`
  - `input_channels_=1`
- `Read()` 输出 XU316 处理后的单路 mic，不输出 interleaved `mic,reference`。
- 若 `AUDIO_INPUT_REFERENCE` 仍存在，必须改为 false 或拆分出新宏，不能再同时控制 full-duplex 和 ESP32 AFE reference。
- 不改 XU316 pipeline stage；继续保持 channel 0 = AGC、channel 1 = NS。
- 不改 `SaturateMicSample()`、输入增益和 RMS 口径。
- mic probe 或等价调试日志必须能看出 wake 阶段 `slot=1`、voice processing 阶段 `slot=0`。
- 记录切换前后的同口径 RMS：NS slot 1 wake raw/out RMS、AGC slot 0 voice raw/out RMS。若 AGC 通道噪声偏高，只记录并暂停调参，不在本任务内改输入增益。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 静态测试：`kWakeWordMicSlot = 1`、`kVoiceMicSlot = 0`，且 `SetInputPurpose()` 分别选择两个 slot。
- 静态测试：Voice PE 主上传路径不输出 `dest[i * input_channels_ + 1]` 或等价 reference channel。
- 静态测试：`input_reference_=false`、`input_channels_=1`、`duplex_=true` 或等价实现。
- 硬件日志：wake 阶段读取 slot 1；唤醒后 voice processing 读取 slot 0。
- 硬件日志：记录 channel 切换前后的 raw mic RMS 和 AFE output RMS。
- 硬件：本地唤醒仍成功。

<done>

- ESP32 接收 XU316 处理后的 channel 1/0 音频，不再给 ESP32 AFE 喂 `M,R` 做 AEC。

## Task 5：禁用 ESP32 AEC/NS/AGC 并保护 TTS 播放

<files>

- Modify: `main/boards/home-assistant-voice-pe/config.json`
- Modify if needed: `main/audio/processors/afe_audio_processor.cc`
- Modify if needed: `main/audio/audio_service.cc`
- Modify if needed: `main/application.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 移除 `CONFIG_USE_DEVICE_AEC=y`。
- 确认没有启用 `CONFIG_USE_SERVER_AEC`。
- 确认 Voice PE 主链路不在 XU316 后叠加第二套 ESP32 AEC/NS/AGC。
- 如果 wake word 仍必须经过 ESP-SR AFE，输入只能来自 XU316 NS channel 1，不得要求 ESP32 `MR` 作为 AEC 前提。
- `WaitForPlaybackQueueEmpty()` 必须等待当前 active playback chunk 结束，并循环复查迟到的 decode/playback 包。
- 从 `speaking` 回到 `listening` 前，保留 `kPlaybackTailGuardMs = 200ms` 播放尾音保护窗，防止刚播完的本机尾音被上传。
- listening 状态也必须接收服务器迟到的 TTS 音频包；只要本地仍有 decode/playback queue、active playback，或 playback tail guard 仍激活，就暂停麦克风上传。
- 进入 speaking 状态时不能清空 decode/playback queue；服务器 TTS 音频包可能先于 `tts start` JSON 到达，清队列会造成“只播一个字”或句子截断。
- `EnableVoiceProcessing(true)` 不能隐式清空 decode/playback queue；清播放队列必须由明确的新会话入口或停止路径显式完成。
- TTS stop 处理必须先 drain playback，再切 listening/idle；不能先切 listening 后等待。
- Voice PE 当前默认 `auto` 收音模式，speaking 阶段不运行服务器上传链路；TTS 播放收尾后、切回 listening 前，必须重置本地 voice processor/AFe 缓冲，并清理上行 encode queue 和 send queue 中的残留语音帧。
- 遵循官方小智 realtime 连续流语义：只有新开收音窗口、播放提示音入口或本地 voice processor 未运行时才发送 `listen/start` 并启动 voice processor；从 TTS stop 回到 `listening` 时如果 processor 已在运行，不能重复重置流、清空上行队列或重新发送 `listen/start`。
- 记录纯播放无人说话 30 秒内是否产生用户 ASR 文本；产生则验收失败。
- 播放期间限频记录 output peak/RMS/volume；用户报告播放中电流声时，先用 peak 判断是否数字削波。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 静态测试：Voice PE config 不包含 `CONFIG_USE_DEVICE_AEC`、`CONFIG_USE_SERVER_AEC`。
- 静态测试：TTS stop 先 drain playback，再切状态。
- 硬件：播放期间无人说话时，小智不应把自身播报识别成用户语音。
- 硬件：播放期间说“你好小智”或按中心按钮，仍可进入新的交互。
- 硬件：纯播放无人说话 30 秒内，小智 Server 不返回用户 ASR 文本。
- 硬件：听到播放中电流声时，记录同时间段 `output probe` 的 peak/RMS/volume。

<done>

- ESP32 前端 DSP 叠加处理被移除，TTS 播放可靠性约束仍保留。

## Task 6：硬件回归

<files>

- Modify only if needed: `main/boards/home-assistant-voice-pe/*`

<action>

- 刷机前单独确认。
- 依次验收：
  - XU316 初始化成功。
  - XU316 pipeline stage：channel 0 = AGC，channel 1 = NS。
  - 本地“你好小智”唤醒。
  - mute 打开阻止本地唤醒。
  - 按钮问答。
  - wake 阶段 slot 1 / NS。
  - voice upload 阶段 slot 0 / AGC。
  - 纯播放无人说话 30 秒 ASR 空结果。
  - 播放期间说话的主观回声效果。
  - 30 分钟误唤醒观察。
  - 005 LED/mute/旋钮。

<verify>

- 串口日志记录：
  - WakeNet 模型。
  - XU316 初始化和 stage 写入。
  - wake word detected。
  - wake slot = 1。
  - voice slot = 0。
  - Voice PE 未启用 ESP32 AEC。
  - free PSRAM。
  - 误唤醒次数。
  - 小智连接和 TTS 播放。

<done>

- AC-1..AC-10 全部通过，误唤醒未超过每小时 3 次。

## Task 7：完成前漂移检查

<files>

- `specs/006-req-voice-pe-wake-aec.md`
- `specs/006-spec-voice-pe-wake-aec.md`
- `specs/006-plan-voice-pe-wake-aec.md`
- `main/boards/home-assistant-voice-pe/*`
- `main/audio/*`
- `tests/test_home_assistant_voice_pe_static.py`

<action>

- 逐条核对 REQ-1..REQ-19。
- 确认没有加入自定义唤醒词。
- 确认没有做 Grove、电源扩展、XMOS DFU、耳机路由。
- 确认没有改小智协议。
- 确认没有改变 mic 转换、输入增益和 RMS 口径。
- 确认 Voice PE slot 分工与官方一致：wake word = channel 1 / NS，voice processing/upload = channel 0 / AGC。
- 确认 Voice PE 主链路没有 ESP32 device AEC、server AEC 或二次 NS/AGC。
- 确认 full-duplex 没有被 `input_reference_` 误关。
- Review 查 Bug，然后按第一性原理分析是否有更简单、更稳健的实现。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- `rg -n "CONFIG_USE_CUSTOM_WAKE_WORD|CONFIG_WAKE_WORD_DISABLED|CONFIG_USE_DEVICE_AEC|CONFIG_USE_SERVER_AEC|NIHAOXIAOZHI|AUDIO_INPUT_REFERENCE|input_reference_|input_channels_|duplex_|IsMicrophoneMuted|kWakeWordMicSlot|kVoiceMicSlot|AppendReferenceSamples|PopReferenceSamples|reference_ring_buffer_" main/boards/home-assistant-voice-pe main/boards/common main/audio tests specs/006-req-voice-pe-wake-aec.md specs/006-spec-voice-pe-wake-aec.md specs/006-plan-voice-pe-wake-aec.md`

<done>

- 代码、req、spec、plan 和硬件验收一致。

## 分层 Review

| Review | 检查 |
|---|---|
| 产品 review | 006 是否只做“你好小智”本地唤醒、官方 channel 分工和 XU316 前端 DSP 边界 |
| 工程 review | 是否复用 ESP-SR/AfeWakeWord/Application 现有流程 |
| 音频 review | XU316 是否负责 AEC/NS/AGC/远场前处理，ESP32 是否只拿处理后的 channel 1/0 音频 |
| 播放 review | TTS 播放是否完整，stop/drain/tail guard/迟到包是否受保护 |
| 硬件 review | 唤醒、mute、XU316 stage、channel 切换、纯播放 ASR 空结果是否通过实机 |
| 验证 review | 静态测试、构建、硬件 AC 是否都有结果 |

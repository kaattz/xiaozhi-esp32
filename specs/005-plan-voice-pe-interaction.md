# 005 实施计划：Voice PE 板载交互功能

## 执行规则

| 规则 | 要求 |
|---|---|
| 不新 worktree | 严禁私自创建新 worktree。 |
| 不提交/推送/刷机 | 提交、推送、刷机前必须单独确认。 |
| 不动主链路 | 不改 WebSocket/MQTT 协议，不改小智 Server 配置。 |
| 不做非目标 | 不启用本地唤醒词，不启用 AEC，不做耳机路由切换。 |
| 不静默降级 | LED/GPIO/旋钮失败必须暴露，不用假成功绕过。 |
| 先实测方向 | mute、jack、旋钮方向如与预期不同，先记录再修 spec/实现。 |
| 硬件验收独立 | Task 6 可统一验收硬件；单个功能失败会阻塞最终通过，但不阻塞其他独立功能继续验证。 |

## Task 0：实施前检查

<files>

- `specs/005-req-voice-pe-interaction.md`
- `specs/005-spec-voice-pe-interaction.md`
- `docs/plans/2026-05-17-voice-pe-interaction-design.md`
- `main/boards/home-assistant-voice-pe/config.h`
- `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- `main/led/circular_strip.h`
- `main/led/circular_strip.cc`
- `main/boards/common/knob.h`
- `main/boards/common/knob.cc`
- `main/audio/audio_codec.cc`

<action>

- 读取 005 req/spec/design。
- 确认 004 小智问答链路相关文件不需要重构。
- 确认 `CircularStrip`、`Knob`、`AudioCodec::SetOutputVolume()` 可复用。
- 确认当前只允许改 Voice PE 板卡文件和必要静态测试。
- 确认 `tests/test_home_assistant_voice_pe_static.py` 是 Python pytest 静态文本检查；它不能替代 ESP-IDF 构建和真实硬件验收。

<verify>

- `git status --short`
- `rg -n "home-assistant-voice-pe|VoicePeAudioCodec|CircularStrip|Knob" main specs docs`

<done>

- 明确 005 不修改协议、不改音频采样率、不启用 wake/AEC。

## Task 1：补硬件常量

<files>

- Modify: `main/boards/home-assistant-voice-pe/config.h`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 新增：
  - `VOICE_PE_LED_DATA_GPIO GPIO_NUM_21`
  - `VOICE_PE_LED_POWER_GPIO GPIO_NUM_45`
  - `VOICE_PE_LED_COUNT 12`
  - `VOICE_PE_MUTE_GPIO GPIO_NUM_3`
  - `VOICE_PE_MUTE_ACTIVE_LEVEL 1`
  - `VOICE_PE_ENCODER_A_GPIO GPIO_NUM_16`
  - `VOICE_PE_ENCODER_B_GPIO GPIO_NUM_18`
  - `VOICE_PE_JACK_DETECT_GPIO GPIO_NUM_17`
  - `VOICE_PE_JACK_INSERTED_LEVEL 1`
  - `VOICE_PE_VOLUME_STEP 10`
- 保留 `CONFIG_WAKE_WORD_DISABLED=y` 和不启用 AEC 的现有配置。
- 静态测试增加上述常量检查。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`

<done>

- 测试能证明 005 GPIO 常量存在且不影响 004 配置。

## Task 2：接入 LED 状态环

<files>

- Modify: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- include `led/circular_strip.h`。
- 增加 `CircularStrip* led_strip_ = nullptr;`。
- 新增 `InitializeLed()`：
  - 配置 GPIO45 为 output。
  - GPIO45 拉高给 LED 供电。
  - `led_strip_ = new CircularStrip(VOICE_PE_LED_DATA_GPIO, VOICE_PE_LED_COUNT);`
- 构造函数中在 `InitializeXmos()` 成功后、`InitializeButtons()` 前调用 `InitializeLed()`，不影响 I2C/XMOS/audio 初始化。
- override `GetLed()` 返回 `led_strip_`。
- 静态测试检查 `CircularStrip`、GPIO45、GPIO21、`GetLed()`。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 硬件：启动、联网、听音、播报时 LED 可见变化。

<done>

- LED 状态环跟随现有 Application 状态变化。

## Task 3a：mute GPIO 状态读取和 debounce

<files>

- Modify: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 增加 `bool muted_` 和读取 `VOICE_PE_MUTE_GPIO` 的 helper。
- 初始化 GPIO3 为 input pull-up，按 ESPHome 官方配置和硬件实测启动态 active-high 读取。
- 在启动时打印 raw mute level 和 interpreted muted 状态。
- 使用 50ms `esp_timer` 轮询 mute GPIO，不使用 GPIO 中断。
- 连续两个采样周期状态一致才接受变化，约 100ms debounce。
- mute 状态变化时只更新板卡字段并打印日志，不在 timer 回调里直接改应用状态。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 硬件：拨动 mute 时串口能看到 raw/interpreted 状态变化，且没有抖动刷屏；若后续实测硬件批次方向相反，只改 `VOICE_PE_MUTE_ACTIVE_LEVEL` 并更新记录。

<done>

- mute GPIO 方向和稳定状态可观测。

## Task 3b：mute 行为接入听音状态

<files>

- Modify: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 在中间按钮 OnClick 前检查 muted：
  - muted 为 true 时只记录日志，不调用 `ToggleChatState()`。
- 当 debounced mute 状态从 false 变 true，使用 `Application::Schedule()` 回到主任务：
  - 如果当前状态是 listening，调用 `Application::StopListening()`。
  - 如果当前状态是 connecting，停止当前听音入口或回到 idle，不能继续收音。
  - 如果当前状态是 speaking，不调用 `AbortSpeaking()`，不打断当前 TTS。
- 不调用 `SetOutputVolume(0)`。
- 不改 `VoicePeAudioCodec` 输入增益。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 硬件：mute 打开后单击中间按钮不进入 listening。
- 硬件：listening/connecting 中打开 mute 会停止听音。
- 硬件：speaking 中打开 mute 不停止当前 TTS；TTS 后 mute 仍阻止新听音。

<done>

- mute 是麦克风隐私静音，不是喇叭静音。

## Task 4：接入旋钮音量

<files>

- Modify: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- include `knob.h` 或正确的 common board include 路径。
- 增加 `std::unique_ptr<Knob> knob_;`。
- 新增 `InitializeKnob()`：
  - `knob_ = std::make_unique<Knob>(VOICE_PE_ENCODER_A_GPIO, VOICE_PE_ENCODER_B_GPIO);`
  - `OnRotate()` 里只捕获方向，然后使用 `Application::Schedule()` 回到主任务。
  - 主任务里读取 `GetAudioCodec()->output_volume()`。
  - clockwise 加 10，counter-clockwise 减 10。
  - clamp 到 0..100。
  - 调用 `SetOutputVolume(volume)`。
  - 打印新音量。
- 如实机方向反，改方向映射并更新 spec。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 硬件：旋钮日志显示音量变化。
- 硬件：测试音或 TTS 主观音量随旋钮变化。

<done>

- 旋钮只控制输出音量，不影响麦克风链路。

## Task 5：接入耳机检测日志

<files>

- Modify: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `tests/test_home_assistant_voice_pe_static.py`

<action>

- 初始化 GPIO17 为 input pull-up，按硬件实测 active-high 读取。
- 启动时打印 raw jack level 和 interpreted inserted 状态。
- 插拔变化时打印 `jack inserted` / `jack removed`。
- 不改 AIC3204 寄存器。
- 不关闭内置扬声器。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- 硬件：插拔耳机时串口日志变化；若后续实测硬件批次方向相反，只改 `VOICE_PE_JACK_INSERTED_LEVEL` 并更新记录。

<done>

- 005 只证明耳机检测 GPIO 可用。

## Task 6：硬件回归

<files>

- Modify only if needed: `main/boards/home-assistant-voice-pe/*`

<action>

- 刷机前单独确认。
- 在真实 Voice PE 上依次验收：
  - LED 状态。
  - mute 阻止听音。
  - 旋钮音量。
  - 耳机插拔日志。
  - 小智一次问答。

<verify>

- 串口日志记录：
  - LED 初始化。
  - mute raw/interpreted 状态。
  - volume 变化。
  - jack raw/interpreted 状态。
  - 小智连接和 TTS 播放。

<done>

- AC-1..AC-8 全部通过。

## Task 7：完成前漂移检查

<files>

- `specs/005-req-voice-pe-interaction.md`
- `specs/005-spec-voice-pe-interaction.md`
- `specs/005-plan-voice-pe-interaction.md`
- `main/boards/home-assistant-voice-pe/*`
- `tests/test_home_assistant_voice_pe_static.py`

<action>

- 逐条核对 REQ-1..REQ-15。
- 确认没有启用本地唤醒词。
- 确认没有启用 AEC。
- 确认没有新增耳机路由切换。
- 确认没有改 WebSocket/MQTT 协议。
- 确认没有改 004 音频采样率、XMOS、AIC3204 初始化边界。

<verify>

- `python -m pytest tests/test_home_assistant_voice_pe_static.py -q`
- `python scripts/release.py home-assistant-voice-pe`
- `rg -n "CONFIG_USE_DEVICE_AEC|CONFIG_WAKE_WORD_DISABLED|VOICE_PE_LED|VOICE_PE_MUTE|VOICE_PE_ENCODER|VOICE_PE_JACK" main/boards/home-assistant-voice-pe specs/005-req-voice-pe-interaction.md specs/005-spec-voice-pe-interaction.md specs/005-plan-voice-pe-interaction.md`

<done>

- 代码、req、spec、plan 和硬件验收一致。

## 分层 Review

| Review | 检查 |
|---|---|
| 产品 review | 四个功能是否都是用户确认的 005 范围 |
| 工程 review | 是否复用现有 `CircularStrip`、`Knob`、`AudioCodec` |
| 硬件 review | GPIO、active level、方向是否经实机验证 |
| 验证 review | 静态测试、构建、硬件 AC 是否都有结果 |

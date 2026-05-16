# 004 实施计划：Home Assistant Voice PE 支持小智

## 执行规则

| 规则 | 要求 |
|---|---|
| 先确认 | 未收到用户明确“开始实施”前，不写固件代码。 |
| 不提交 | 提交、推送、刷机前必须单独确认。 |
| 不新 worktree | 不私自创建新 worktree。 |
| 不兜底 | XMOS、I2S、AIC3204 失败必须暴露，不用假成功绕过。 |
| 不开 AEC | 第一阶段不启用 `CONFIG_USE_DEVICE_AEC`。 |
| 不做非目标 | LED、旋钮、Grove、耳机检测、本地唤醒词后置。 |
| 硬件失败暂停 | Task 3/5a/5b 任一硬件验证失败时，暂停后续任务，记录串口日志和测量结果，先更新 Spec/Plan 再继续。 |

## Task 0：实施前检查

<files>

- `specs/004-req-home-assistant-voice-pe.md`
- `specs/004-spec-home-assistant-voice-pe.md`
- `docs/plans/2026-05-16-home-assistant-voice-pe-design.md`
- `docs/custom-board.md`
- `main/CMakeLists.txt`
- `main/Kconfig.projbuild`
- `main/audio/audio_codec.h`
- `main/audio/codecs/no_audio_codec.*`
- `main/audio/codecs/box_audio_codec.*`
- `main/boards/esp-box-3/*`
- `main/boards/m5stack-core-s3/*`

<action>

- 读取 req/spec/design。
- 确认工作区干净。
- 确认不创建 worktree。
- 确认 `partitions/v2/16m.csv` 已存在。
- 确认 WiFi/NVS 复用现有 `WifiBoard`、`esp-wifi-connect` 和 `wifi` NVS。
- 确认第一阶段只做 `home-assistant-voice-pe` 新板卡和音频最小闭环。

<verify>

- `git status --short`
- `Test-Path partitions/v2/16m.csv`
- `rg -n "BOARD_TYPE_HOME_ASSISTANT_VOICE_PE|home-assistant-voice-pe|VoicePeAudioCodec" main specs docs`

<done>

- 明确当前没有同名板卡和同名音频类。

## Task 1：官方代码取证

<files>

- Create: `docs/plans/2026-05-16-home-assistant-voice-pe-source-evidence.md`
- Reference: `https://raw.githubusercontent.com/esphome/home-assistant-voice-pe/dev/home-assistant-voice.yaml`
- Reference: `https://github.com/esphome/home-assistant-voice-pe/tree/dev/esphome/components/voice_kit`
- Reference: `https://github.com/esphome/esphome/blob/35631be260c0fd6fae1e4c945f16790979ba777c/esphome/components/aic3204/aic3204.cpp`
- Reference: `https://github.com/esphome/esphome/blob/35631be260c0fd6fae1e4c945f16790979ba777c/esphome/components/aic3204/aic3204.h`
- Reference: `https://github.com/esphome/esphome/blob/35631be260c0fd6fae1e4c945f16790979ba777c/esphome/components/aic3204/audio_dac.py`

<action>

- 把官方 Voice PE GPIO、XMOS 协议、AIC3204 初始化来源写入本地 evidence 文件。
- evidence 文件必须包含：来源 URL、commit 或分支名、摘录的常量、对应本项目字段。
- 不把 ESPHome `voice_assistant`、`sendspin`、灯效脚本搬进本项目。

<verify>

- 复核以下值：GPIO5/6、GPIO4、GPIO13/14/15、GPIO8/7/10、GPIO47、AIC3204、XMOS `0x42`。

<done>

- `docs/plans/2026-05-16-home-assistant-voice-pe-source-evidence.md` 存在，且所有硬件常量都有来源。

## Task 2：新增板卡构建入口

<files>

- Create: `main/boards/home-assistant-voice-pe/config.json`
- Create: `main/boards/home-assistant-voice-pe/config.h`
- Create: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `main/Kconfig.projbuild`
- Modify: `main/CMakeLists.txt`

<action>

- 新增 `BOARD_TYPE_HOME_ASSISTANT_VOICE_PE`。
- CMake 映射 `BOARD_TYPE` 为 `home-assistant-voice-pe`。
- `config.json` 使用 `esp32s3`、16MB flash、16m partition、`CONFIG_LANGUAGE_ZH_CN=y`、`CONFIG_WAKE_WORD_DISABLED=y`。
- 板卡类继承 `WifiBoard`，先保留最小按钮和音频入口。

<verify>

- `rg -n "BOARD_TYPE_HOME_ASSISTANT_VOICE_PE|home-assistant-voice-pe" main/CMakeLists.txt main/Kconfig.projbuild main/boards/home-assistant-voice-pe`

<done>

- 新板卡能被 Kconfig/CMake 找到。

## Task 3：实现 XMOS 最小控制

<files>

- Create: `main/boards/home-assistant-voice-pe/voice_pe_xmos.h`
- Create: `main/boards/home-assistant-voice-pe/voice_pe_xmos.cc`
- Modify: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `main/boards/home-assistant-voice-pe/config.h`

<action>

- 初始化 internal I2C：SDA GPIO5、SCL GPIO6。
- GPIO4 拉高至少 10ms，再拉低释放 XMOS。
- reset 释放 500ms 后开始读版本。
- 每 250ms 重试一次，直到读版本成功或 reset 释放后 4000ms 超时。
- 通过同一条 internal I2C 总线读取 `0x42` 的 XMOS 版本或等价健康状态。
- 失败直接日志报错，不继续假定音频可用。

<verify>

- 构建通过。
- 硬件串口日志能看到 XMOS reset 和 version read。
- 如果 I2C `0x42` 读不到，暂停后续任务，保存日志，更新 `004-spec` 的硬件假设或引脚/协议判断，不继续 Task 4 及后续任务。

<done>

- XMOS I2C 通信成功。

## Task 4：实现 AIC3204 最小驱动

<files>

- Create: `main/boards/home-assistant-voice-pe/aic3204_audio_dac.h`
- Create: `main/boards/home-assistant-voice-pe/aic3204_audio_dac.cc`
- Modify: `main/boards/home-assistant-voice-pe/config.h`

<action>

- 按 ESPHome AIC3204 组件固定 commit `35631be260c0fd6fae1e4c945f16790979ba777c` 翻译寄存器初始化。
- 寄存器写入顺序以 `esphome/components/aic3204/aic3204.cpp` 为准，头文件以同 commit 的 `aic3204.h` 为准，`audio_dac.py` 只作为 schema/codegen 参考。
- 支持初始化、解静音、设置输出音量。
- 不做不明寄存器降级。

<verify>

- 构建通过。
- I2C 写入失败时有明确日志。

<done>

- AIC3204 初始化可被 `VoicePeAudioCodec` 调用。

## Task 5a：I2S RX 和麦克风读取

<files>

- Create: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.h`
- Create: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc`
- Modify: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `main/boards/home-assistant-voice-pe/config.h`

<action>

- 建立 I2S RX：BCLK GPIO13、LRCLK GPIO14、DIN GPIO15。
- 第一阶段从 stereo input 中选择一个有效 mic 通道。
- `input_sample_rate_ = 16000`。
- 从选定 mic 通道读取 signed 32-bit PCM，算术右移 12 bit 后饱和到 `INT16_MIN..INT16_MAX`。
- 增加受控 mic probe 日志，只验证 RX 原始链路，不接入小智端到端。

<verify>

- 构建通过。
- 串口日志能看到非零 mic 数据。
- 如果数据全零、固定或 I2S 读超时，暂停后续任务，保存日志，更新 Spec/Plan 后再继续。

<done>

- I2S RX 能读到非零麦克风数据。

## Task 5b：I2S TX、AIC3204 和功放播放

<files>

- Modify: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.h`
- Modify: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc`
- Modify: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `main/boards/home-assistant-voice-pe/config.h`

<action>

- 建立 I2S TX：BCLK GPIO8、LRCLK GPIO7、DOUT GPIO10。
- `output_sample_rate_ = 48000`。
- `EnableOutput(true)` 初始化 AIC3204 并使能 GPIO47。
- 播放 1 kHz 测试音，测试音必须走真实 I2S TX + AIC3204 + amp 链路。
- TTS/解码输出依赖现有 `AudioService` 输出重采样器转到 48k；后续 `VoicePeAudioCodec::Write` 只接收 48k PCM。
- `VoicePeAudioCodec::Write` 不实现 16k->48k 重采样；如果 `AudioService` 输出重采样器未创建成功，播放验证失败。

<verify>

- 构建通过。
- 内置扬声器能播放 1 kHz 测试音。
- 如果无声、变速或 AIC3204/I2S 写失败，暂停后续任务，保存日志，更新 Spec/Plan 后再继续。

<done>

- I2S TX、AIC3204、GPIO47 功放链路单独验证通过。

## Task 5c：集成为 VoicePeAudioCodec

<files>

- Modify: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.h`
- Modify: `main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc`
- Modify: `main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc`
- Modify: `main/boards/home-assistant-voice-pe/config.h`

<action>

- 继承 `AudioCodec` 并接入板卡 `GetAudioCodec()`。
- 第一阶段 `input_reference_=false`。
- `input_channels_ = 1`，`output_channels_` 按现有播放链路设置。
- `Read` 返回 Task 5a 验证过的 int16 PCM。
- `Write` 只接收 Task 5b 验证过的 48k PCM 输出路径。
- `Read` / `Write` 不做静默失败。

<verify>

- 构建通过。
- `AudioService` 能拿到 `VoicePeAudioCodec`。
- 串口日志能打印 mic RMS。
- 1 kHz 测试音仍能播放。

<done>

- 输入、输出和 `AudioCodec` 集成路径都验证通过。

## Task 6：麦克风 RMS 硬件验证

<files>

- Modify only if needed: `main/boards/home-assistant-voice-pe/*`

<action>

- 临时或受控地打印 mic RMS。
- 人在 30cm 正常说话，int16 PCM 说话窗口平均 RMS 必须比安静窗口高至少 200。
- 如果 RMS 全零、固定或只有噪声，停止后续联调，回查 I2S/XMOS。

<verify>

- 串口日志截图或记录：安静 RMS、说话 RMS、差值，差值必须 >= 200。

<done>

- 输入链路可信。

## Task 7：小智端到端联调

<files>

- Modify only if needed: `main/boards/home-assistant-voice-pe/*`

<action>

- 构建并刷入 Voice PE。
- 使用现有 `WifiBoard` / `esp-wifi-connect` 配网，凭据写入 `wifi` NVS。
- 连接小智 Server。
- 发起一次语音问答。
- 确认 TTS 从内置扬声器播放。

<verify>

- 串口日志显示协议连接成功。
- 用户说一句短句，设备播放小智回复。

<done>

- 第一阶段目标完成。

## Task 8：完成前漂移检查

<files>

- `specs/004-req-home-assistant-voice-pe.md`
- `specs/004-spec-home-assistant-voice-pe.md`
- `specs/004-plan-home-assistant-voice-pe.md`
- 本次实现文件

<action>

- 逐条核对 REQ-1 到 REQ-16。
- 确认没有启用 AEC。
- 确认没有引入 ESPHome WebSocket 改造。
- 确认没有实现非目标功能。
- 确认没有新增 Voice PE 专用 NVS schema。
- 确认失败路径会暴露错误。

<verify>

- `git diff --check`
- `rg -n "CONFIG_USE_DEVICE_AEC|BOARD_TYPE_HOME_ASSISTANT_VOICE_PE|VoicePeAudioCodec|home-assistant-voice-pe" main specs docs`
- 运行可用的构建命令。

<done>

- req/spec/plan/实现/验证一致。

## 实测进度

| 任务 | 2026-05-16 状态 |
|---|---|
| Task 2 板卡构建入口 | 已实现并构建通过 |
| Task 3 XMOS 最小控制 | 已验证：串口读到 XMOS firmware `1.3.1` |
| Task 4 AIC3204 最小驱动 | 已验证：初始化成功，扬声器链路可播放 |
| Task 5a I2S RX 和麦克风读取 | 已验证：`channel1=NS` 有效，固定 24 倍增益 |
| Task 5b I2S TX、AIC3204 和功放播放 | 已验证：中间按钮双击可听到 1 kHz 测试音 |
| Task 5c 集成为 VoicePeAudioCodec | 已验证：`AudioService` 正常调用输入/输出 |
| Task 6 麦克风 RMS 硬件验证 | 已验证：安静 RMS 约 `405..613`，说话 RMS 约 `7971..10248` |
| Task 7 小智端到端联调 | 已验证：WiFi、MQTT、小智问答、TTS 播放成功；用户确认“小智回复成功” |
| Task 8 完成前漂移检查 | 已执行：静态测试、diff 检查、Voice PE 非目标漂移检查、ESP-IDF build 均通过 |

## 最终验证命令

| 命令 | 结果 |
|---|---|
| `python -m pytest tests/test_home_assistant_voice_pe_static.py` | 3 passed |
| `git diff --check` | 无空白错误，仅有 CRLF 提示 |
| `rg` 检查 Voice PE 目录的 AEC、ESPHome `voice_assistant`、灯效、旋钮、耳机、Grove 关键字 | 无实现漂移 |
| `idf.py build` | 通过，`xiaozhi.bin` 仍小于 16MB app 分区 |

## 分层 Review

| 层 | 检查点 |
|---|---|
| 产品 review | 第一阶段只要求一次小智问答，不扩大范围。 |
| 工程 review | 板卡身份独立，音频类不污染通用抽象。 |
| 硬件 review | XMOS、I2S、AIC3204、功放引脚与官方资料一致。 |
| 验证 review | XMOS、mic、speaker、小智端到端逐层通过。 |

## 等待确认

收到用户明确 **“开始实施”** 后，进入编码阶段。

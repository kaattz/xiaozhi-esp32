# 004 需求：Home Assistant Voice PE 支持小智

## 背景

目标是在施工仓库 `C:\Code\xiaozhi-voice-pe` 中，让 Home Assistant Voice Preview Edition 作为 xiaozhi-esp32 的一块独立板卡运行。

第一阶段只做最小闭环：刷入固件后，设备能联网、连接小智 Server，并完成一次语音问答。灯效、旋钮、耳机检测、Grove、本地唤醒词和 AEC 后置。

## 入口判定

这是中大型功能：

| 原因 | 说明 |
|---|---|
| 硬件适配 | 需要新增 ESP32-S3 板卡目录和构建入口。 |
| 音频链路 | Voice PE 是 ESP32-S3 + XMOS XU316 + AIC3204，不是普通 I2S 麦克风板。 |
| 外部输入输出 | 需要接入小智 Server 并播放 TTS。 |
| 长期维护 | 板卡名、分区、OTA 身份必须独立，不能覆盖已有板。 |

## 用户故事

| 编号 | 用户故事 |
|---|---|
| US-1 | 作为用户，我希望 Home Assistant Voice PE 能刷入小智固件，而不是只能跑 ESPHome Assist。 |
| US-2 | 作为用户，我希望第一版先证明能问答，不被灯效、旋钮、AEC 等非核心功能拖慢。 |
| US-3 | 作为维护者，我希望 Voice PE 是独立板卡，后续可以单独构建、测试和发布。 |

## 功能需求

| 编号 | 需求 |
|---|---|
| REQ-1 | 必须新增独立板卡 `home-assistant-voice-pe`，不能复用或覆盖 `esp-box-3` 等现有板卡名。 |
| REQ-2 | 板卡构建必须使用 ESP32-S3、16MB flash 和 `partitions/v2/16m.csv`。 |
| REQ-3 | 第一阶段默认使用中文语言包，并禁用本地唤醒词。 |
| REQ-4 | 必须新增 Voice PE 专用音频实现，不得把 `NoAudioCodec` 或 `BoxAudioCodec` 作为最终适配。 |
| REQ-5 | 必须通过同一条 internal I2C 总线 GPIO5/6 控制 XMOS，并通过 GPIO4 复位 XMOS。 |
| REQ-6 | 必须在 GPIO4 复位并等待 XMOS 启动后，通过同一条 internal I2C 总线读到地址 `0x42` 的版本或等价健康状态。 |
| REQ-7 | 必须实现麦克风 I2S 输入：BCLK GPIO13、LRCLK GPIO14、DIN GPIO15。 |
| REQ-8 | 麦克风输入必须按送入小智链路前的 int16 PCM 计算 RMS；30cm 正常说话窗口平均 RMS 必须比安静窗口高至少 200，且不能接受全零或固定噪声作为通过。 |
| REQ-9 | 必须实现 AIC3204 初始化，并通过扬声器 I2S 输出：BCLK GPIO8、LRCLK GPIO7、DOUT GPIO10。 |
| REQ-10 | 必须控制内部功放 GPIO47，使测试音或 TTS 可以从内置扬声器播放。 |
| REQ-11 | WiFi 配网必须复用现有 `WifiBoard` / `esp-wifi-connect` 流程，凭据继续存入现有 `wifi` NVS 命名空间。 |
| REQ-12 | 必须复用现有小智 Server 配置和设备身份持久化方案，第一阶段不新增 Voice PE 专用 NVS schema。 |
| REQ-13 | 必须能连接小智 Server 并完成一次语音问答。 |
| REQ-14 | 第一阶段不得引入假 AEC 或伪造 reference channel。 |
| REQ-15 | 第一阶段不做 XMOS DFU 更新流程，只假定设备已有可工作的 XMOS 固件。 |
| REQ-16 | LED 环、旋钮、mute 开关、耳机检测、Grove、电源扩展不作为第一阶段验收项。 |

## 验收标准

| 编号 | 验收 |
|---|---|
| AC-1 | `python scripts/release.py home-assistant-voice-pe` 或等价构建流程能选择新板卡并进入编译。 |
| AC-2 | 启动日志能看到 XMOS 复位和 I2C 通信成功。 |
| AC-3 | 串口日志能看到 int16 PCM RMS：说话窗口平均值比安静窗口高至少 200。 |
| AC-4 | AIC3204 初始化后，设备能播放 1 kHz 测试音或等价测试音频。 |
| AC-5 | 设备能连上小智 Server。 |
| AC-6 | 用户说一句短句后，小智 Server 返回 TTS，设备能完整播放。 |
| AC-7 | 第一阶段不开本地唤醒词、不启用 AEC、不要求 LED/旋钮功能。 |

## 启动顺序

| 顺序 | 动作 |
|---|---|
| 1 | 初始化板级 GPIO 和 internal I2C：SDA GPIO5、SCL GPIO6。 |
| 2 | GPIO4 复位 XMOS，并等待 XMOS 启动完成。 |
| 3 | 通过同一条 internal I2C 总线读取 XMOS `0x42` 健康状态。 |
| 4 | 初始化 AIC3204 和扬声器功放控制。 |
| 5 | 初始化 I2S RX/TX 和 `VoicePeAudioCodec`。 |
| 6 | 进入现有 WiFi 配网/连接流程。 |
| 7 | 连接小智 Server，执行一次语音问答。 |

## 非目标

| 非目标 | 原因 |
|---|---|
| 不改 ESPHome WebSocket 协议 | 这会被 HA Assist 业务层绑定，成本更高。 |
| 不做本地唤醒词 | 先证明硬件音频链路。 |
| 不做 AEC | reference channel 未证明前不能开启。 |
| 不做 XMOS DFU | 先假定原厂固件可用，降低第一阶段风险。 |
| 不做完整交互体验 | LED、旋钮、耳机检测后续再补。 |
| 不做专用配网/NVS | 复用现有 `esp-wifi-connect`、`wifi` NVS 和小智配置持久化流程。 |

## 需求追踪

| 需求 | Spec 章节 | 验证方式 |
|---|---|---|
| REQ-1 | 板卡接入 | 构建和代码 review |
| REQ-2 | 构建配置 | `config.json` 检查和构建 |
| REQ-3 | 构建配置 | `config.json` 检查 |
| REQ-4 | 音频架构 | 代码 review |
| REQ-5 | XMOS 控制 | 串口日志 |
| REQ-6 | XMOS 控制 | I2C 版本读取 |
| REQ-7 | 麦克风输入 | RMS 实验 |
| REQ-8 | 麦克风输入 | RMS 实验 |
| REQ-9 | 播放输出 | 测试音 |
| REQ-10 | 播放输出 | 测试音 |
| REQ-11 | WiFi 配网 | WiFi 配网/重连 |
| REQ-12 | 持久化配置 | NVS/配置 review |
| REQ-13 | 小智联调 | 一次问答 |
| REQ-14 | 音频架构 | drift check |
| REQ-15 | XMOS 控制 | drift check |
| REQ-16 | 非目标 | drift check |

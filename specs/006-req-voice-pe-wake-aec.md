# 006 需求：Voice PE 本地唤醒词与 XU316 前端 DSP

## 背景

004 已完成 Voice PE 小智语音问答最小闭环，005 已完成 LED、mute、旋钮和耳机检测。006 只做两件事：本地唤醒词和对齐官方 XU316 麦克风前端 DSP 分工。

本地唤醒词固定为“你好小智”，使用 ESP-SR 已内置的 WakeNet 预置模型，不做自定义唤醒词训练。

Voice PE 的音频前端由 XU316 负责：AEC 回声消除、NS 噪声抑制、AGC 自动增益和远场拾音前处理。ESP32-xiaozhi 负责联网、唤醒/对话状态机、音频上传、TTS 播放、HA 控制、LED、按键和设备状态。

## 入口判定

这是高风险 feature：

| 原因 | 说明 |
|---|---|
| 改变语音入口 | 从按钮触发扩展到本地唤醒触发 |
| 改动音频处理边界 | ESP32 侧必须从软件/device AEC 退回协议和状态机职责，不能和 XU316 重复做前端 DSP |
| 需要硬件验收 | 唤醒率、误唤醒、XU316 前端处理效果都必须在真实 Voice PE 上听和看日志 |
| 不能破坏 004/005 | 按钮问答、mute、旋钮、LED 必须继续可用 |

## 用户故事

| 编号 | 用户故事 |
|---|---|
| US-1 | 作为用户，我希望不按按钮，直接说“你好小智”就能让 Voice PE 开始听我说话。 |
| US-2 | 作为用户，我希望小智正在播报时，设备不要把自己的喇叭声音当成我的指令。 |
| US-3 | 作为维护者，我希望 XU316 负责麦克风前端 DSP，ESP32 不再用软件 AEC/NS/AGC 叠加处理。 |
| US-4 | 作为维护者，我希望 Voice PE 的麦克风通道选择对齐官方 ESPHome：唤醒用 NS 通道，语音上传用 AGC 通道。 |

## 功能需求

| 编号 | 需求 |
|---|---|
| REQ-1 | Voice PE 必须启用本地唤醒词检测。 |
| REQ-2 | 本地唤醒词必须固定为 ESP-SR 预置的“你好小智”模型。 |
| REQ-3 | 006 不支持用户自定义唤醒词，不接入 Multinet 自定义命令词路径。 |
| REQ-4 | idle 状态下说“你好小智”必须触发小智听音流程，不需要按中间按钮。 |
| REQ-5 | 唤醒成功后必须继续复用现有小智协议和 `Application` 唤醒流程，不新增 Voice PE 专用 WebSocket/MQTT 协议。 |
| REQ-6 | mute 打开时，本地唤醒词不得触发听音或上传麦克风音频。 |
| REQ-7 | XU316 必须作为 Voice PE 的麦克风前端 DSP，负责 AEC、NS、AGC 和远场拾音前处理。 |
| REQ-8 | ESP32-xiaozhi 不得在 Voice PE 主语音上传链路上启用 ESP32 software/device AEC、server AEC、二次 NS 或 AGC。 |
| REQ-9 | ESP32 必须正确初始化 XU316，并保持官方默认 pipeline：channel 0 = AGC，channel 1 = NS。 |
| REQ-10 | ESP32 必须通过 I2S 读取 XU316 处理后的麦克风音频，语音上传不得改走未处理原始双麦数据。 |
| REQ-11 | ESP32 必须通过官方播放输出路径把 TTS/提示音 PCM 送到 Voice PE 音频硬件，使 XU316 有机会使用播放参考；如果无法证明该路径成立，必须暂停并更新 Spec，不能宣称 XU316 AEC 完成。 |
| REQ-12 | XU316 分工必须使用 32-bit Q31 到 int16 Q15 的基础转换，也就是右移 16 位；语音上传通道必须对齐官方 `voice_assistant.volume_multiplier: 1`，不得套用唤醒通道增益。 |
| REQ-13 | 006 实施后必须保持按钮触发问答可用。 |
| REQ-14 | 006 实施后必须保持 005 的 LED、mute、旋钮功能可用。 |
| REQ-15 | 006 不做 Grove、电源扩展、XMOS DFU、耳机音频路由、远程唤醒仲裁。 |
| REQ-16 | 第一版不设量化唤醒率指标；如果实测误唤醒明显影响使用，例如每小时超过 3 次，必须记录日志并评估阈值或模型配置，不能直接放行。 |
| REQ-17 | Voice PE 必须对齐官方 ESPHome channel 用法：本地唤醒读取 XMOS channel 1 / NS，语音处理和上传读取 XMOS channel 0 / AGC。 |
| REQ-18 | `AudioInputPurpose::kWakeWord` 必须选择 slot 1，`AudioInputPurpose::kVoiceProcessing` 必须选择 slot 0；不能继续让唤醒和语音上传固定走同一个 slot。 |
| REQ-19 | 本 feature 只切换输入用途到官方 channel 分工，并保持语音上传通道 1:1 增益；不得修改 32-bit Q31 到 int16 Q15 的转换口径、RMS 口径或 XU316 pipeline stage。 |

## 验收标准

| 编号 | 验收 |
|---|---|
| AC-1 | 启动日志能看到 ESP-SR WakeNet 模型加载，且唤醒词包含“你好小智”。 |
| AC-2 | idle 状态下说“你好小智”，设备进入 listening，并完成一次小智回复。 |
| AC-3 | mute 打开时说“你好小智”，设备不进入 listening。 |
| AC-4 | 中间按钮单击仍能触发原有问答流程。 |
| AC-5 | 启动日志能看到 XU316 初始化成功，并写入 `channel0=AGC channel1=NS`。 |
| AC-6 | 静态检查证明 Voice PE 未启用 `CONFIG_USE_DEVICE_AEC` 或 `CONFIG_USE_SERVER_AEC`，ESP32 主语音链路不再输出 `MR` 给 AFE 做软件 AEC。 |
| AC-7 | 播放 TTS 或测试音时，设备不会明显把自身播报回声上传成用户语音；纯播放且无人说话 30 秒内，小智 Server 不应返回用户 ASR 文本。 |
| AC-8 | 004 小智一次问答和 005 LED/mute/旋钮回归通过。 |
| AC-9 | 静态测试证明 `kWakeWordMicSlot = 1`、`kVoiceMicSlot = 0`，且 `SetInputPurpose()` 按用途切换。 |
| AC-10 | 实机日志能区分 wake 阶段读取 slot 1、voice processing 阶段读取 slot 0；切换后一次唤醒问答成功。 |

## 非目标

| 非目标 | 原因 |
|---|---|
| 不做自定义唤醒词 | “你好小智”已有预置模型，第一版不引入训练和阈值调参风险。 |
| 不做多个唤醒词 | 会扩大误唤醒和模型选择范围。 |
| 不做服务端唤醒 | 006 目标是本地唤醒。 |
| 不做 ESP32 AEC / server AEC | 006 目标是 XU316 负责前端 DSP，ESP32 不重复做 AEC。 |
| 不做 XMOS DFU | 属于维护能力，不是唤醒/AEC 主链路。 |
| 不做 Grove / 电源扩展 | 与本地唤醒和 XU316 前端 DSP 分工无关。 |
| 不改小智协议 | 复用现有 `SendWakeWordDetected()` 和音频流。 |
| 不做经验性输入增益调参 | 语音上传通道只对齐官方 1:1；不按主观听感继续加后处理增益。 |
| 不改 XU316 stage | 官方默认已是 `channel0=AGC`、`channel1=NS`，本 feature 只改 ESP32 读取哪个 slot。 |

## 需求追踪

| 需求 | Spec 章节 | 验证方式 |
|---|---|---|
| REQ-1 | 本地唤醒词 | 启动日志/硬件唤醒 |
| REQ-2 | 本地唤醒词 | config/static check |
| REQ-3 | 非目标 | config/static check |
| REQ-4 | 本地唤醒词 | AC-2 |
| REQ-5 | 应用流程 | 代码 review |
| REQ-6 | Mute 互斥 | AC-3 |
| REQ-7 | XU316 前端 DSP | AC-5/AC-7 |
| REQ-8 | XU316 前端 DSP | static check |
| REQ-9 | XU316 前端 DSP | 启动日志/static check |
| REQ-10 | 音频链路 | 代码 review/硬件日志 |
| REQ-11 | 音频链路 | 硬件验收/review |
| REQ-12 | 音频边界 | drift check |
| REQ-13 | 回归 | AC-4 |
| REQ-14 | 回归 | AC-8 |
| REQ-15 | 非目标 | drift check |
| REQ-16 | 唤醒质量 | 误唤醒观察 |
| REQ-17 | XMOS channel 分工 | AC-9/AC-10 |
| REQ-18 | XMOS channel 分工 | static check/硬件日志 |
| REQ-19 | 音频边界 | drift check |

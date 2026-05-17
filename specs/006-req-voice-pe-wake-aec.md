# 006 需求：Voice PE 本地唤醒词与 AEC

## 背景

004 已完成 Voice PE 小智语音问答最小闭环，005 已完成 LED、mute、旋钮和耳机检测。006 只做两件事：本地唤醒词和设备端 AEC。

本地唤醒词固定为“你好小智”，使用 ESP-SR 已内置的 WakeNet 预置模型，不做自定义唤醒词训练。

## 入口判定

这是高风险 feature：

| 原因 | 说明 |
|---|---|
| 改变语音入口 | 从按钮触发扩展到本地唤醒触发 |
| 改动音频处理链路 | AEC 依赖 mic + playback reference 双通道输入 |
| 需要硬件验收 | 唤醒率、误唤醒、回声消除都必须在真实 Voice PE 上听和看日志 |
| 不能破坏 004/005 | 按钮问答、mute、旋钮、LED 必须继续可用 |

## 用户故事

| 编号 | 用户故事 |
|---|---|
| US-1 | 作为用户，我希望不按按钮，直接说“你好小智”就能让 Voice PE 开始听我说话。 |
| US-2 | 作为用户，我希望小智正在播报时，设备不要把自己的喇叭声音当成我的指令。 |
| US-3 | 作为维护者，我希望 AEC 必须基于真实 playback reference，不接受只打开配置但没有 reference 的假实现。 |

## 功能需求

| 编号 | 需求 |
|---|---|
| REQ-1 | Voice PE 必须启用本地唤醒词检测。 |
| REQ-2 | 本地唤醒词必须固定为 ESP-SR 预置的“你好小智”模型。 |
| REQ-3 | 006 不支持用户自定义唤醒词，不接入 Multinet 自定义命令词路径。 |
| REQ-4 | idle 状态下说“你好小智”必须触发小智听音流程，不需要按中间按钮。 |
| REQ-5 | 唤醒成功后必须继续复用现有小智协议和 `Application` 唤醒流程，不新增 Voice PE 专用 WebSocket/MQTT 协议。 |
| REQ-6 | mute 打开时，本地唤醒词不得触发听音或上传麦克风音频。 |
| REQ-7 | AEC 必须使用真实 reference channel；reference 只能来自实际播放到扬声器的 PCM。 |
| REQ-8 | AEC reference 采样率必须与 AFE 输入采样率一致，即 16 kHz int16 PCM。 |
| REQ-9 | Voice PE 的 AFE 输入格式必须明确为 mic + reference；启用 AEC 后不能仍然只喂单通道 mic。 |
| REQ-10 | 启用 AEC 前必须有日志证明：播放期间 reference RMS 非零，静音期间 reference RMS 接近零。 |
| REQ-11 | 如果 reference channel 无法证明真实有效，必须暂停 AEC 实施并更新 Spec，不能提交假 AEC。 |
| REQ-12 | AEC 不得改变 004 已确定的麦克风 32-bit 到 int16 转换方式、24 倍主观等效增益和 RMS 口径。 |
| REQ-13 | 006 实施后必须保持按钮触发问答可用。 |
| REQ-14 | 006 实施后必须保持 005 的 LED、mute、旋钮功能可用。 |
| REQ-15 | 006 不做 Grove、电源扩展、XMOS DFU、耳机音频路由、远程唤醒仲裁。 |
| REQ-16 | 第一版不设量化唤醒率指标；如果实测误唤醒明显影响使用，例如每小时超过 3 次，必须记录日志并评估阈值或模型配置，不能直接放行。 |

## 验收标准

| 编号 | 验收 |
|---|---|
| AC-1 | 启动日志能看到 ESP-SR WakeNet 模型加载，且唤醒词包含“你好小智”。 |
| AC-2 | idle 状态下说“你好小智”，设备进入 listening，并完成一次小智回复。 |
| AC-3 | mute 打开时说“你好小智”，设备不进入 listening。 |
| AC-4 | 中间按钮单击仍能触发原有问答流程。 |
| AC-5 | 播放测试音或 TTS 时，日志显示 reference RMS 非零；无播放时 reference RMS 接近零。 |
| AC-6 | 启用 AEC 后，speaking/播放期间对着设备说话，设备不会明显把自身播报回声上传成用户语音；纯播放且无人说话 30 秒内，小智 Server 不应返回用户 ASR 文本。 |
| AC-7 | 旋钮调节音量后，AEC reference 仍来自实际播放 PCM，不因音量变化变成固定假数据。 |
| AC-8 | 004 小智一次问答和 005 LED/mute/旋钮回归通过。 |

## 非目标

| 非目标 | 原因 |
|---|---|
| 不做自定义唤醒词 | “你好小智”已有预置模型，第一版不引入训练和阈值调参风险。 |
| 不做多个唤醒词 | 会扩大误唤醒和模型选择范围。 |
| 不做服务端唤醒 | 006 目标是本地唤醒。 |
| 不做 server AEC | 006 只验证设备端 AEC。 |
| 不做 XMOS DFU | 属于维护能力，不是唤醒/AEC 主链路。 |
| 不做 Grove / 电源扩展 | 与本地唤醒和 AEC 无关。 |
| 不改小智协议 | 复用现有 `SendWakeWordDetected()` 和音频流。 |

## 需求追踪

| 需求 | Spec 章节 | 验证方式 |
|---|---|---|
| REQ-1 | 本地唤醒词 | 启动日志/硬件唤醒 |
| REQ-2 | 本地唤醒词 | config/static check |
| REQ-3 | 非目标 | config/static check |
| REQ-4 | 本地唤醒词 | AC-2 |
| REQ-5 | 应用流程 | 代码 review |
| REQ-6 | Mute 互斥 | AC-3 |
| REQ-7 | AEC reference | RMS 日志 |
| REQ-8 | AEC reference | 代码 review/日志 |
| REQ-9 | AFE 输入格式 | 日志/static check |
| REQ-10 | AEC 诊断 | AC-5 |
| REQ-11 | 错误处理 | review |
| REQ-12 | 音频边界 | drift check |
| REQ-13 | 回归 | AC-4 |
| REQ-14 | 回归 | AC-8 |
| REQ-15 | 非目标 | drift check |
| REQ-16 | 唤醒质量 | 误唤醒观察 |

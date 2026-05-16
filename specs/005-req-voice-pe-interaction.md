# 005 需求：Voice PE 板载交互功能

## 背景

004 阶段已经让 Home Assistant Voice PE 完成小智语音问答最小闭环。005 阶段补齐不改变语音协议和音频主链路的板载交互：LED 状态环、物理 mute 开关、旋钮音量、耳机检测。

## 入口判定

这是中型 feature：

| 原因 | 说明 |
|---|---|
| 多硬件输入输出 | 涉及 WS2812、GPIO 输入、旋钮编码器和耳机检测 |
| 跨状态流 | mute 会影响听音入口 |
| 需要硬件验收 | 每个功能都必须在真实 Voice PE 上观察 |
| 不能影响 004 | 小智一次问答必须继续成功 |

## 用户故事

| 编号 | 用户故事 |
|---|---|
| US-1 | 作为用户，我希望设备状态能通过 Voice PE 的 LED 环看出来。 |
| US-2 | 作为用户，我希望打开物理 mute 后设备不要继续听我说话。 |
| US-3 | 作为用户，我希望能用 Voice PE 旋钮调节小智回复音量。 |
| US-4 | 作为维护者，我希望先能看到耳机插拔状态，为后续音频路由做准备。 |

## 功能需求

| 编号 | 需求 |
|---|---|
| REQ-1 | 必须接入 Voice PE LED 环：GPIO45 供电、GPIO21 数据、12 颗 WS2812。 |
| REQ-2 | LED 环必须复用现有 `CircularStrip`，跟随 `Application` 设备状态变化。 |
| REQ-3 | LED 初始化失败必须暴露错误，不能静默退化为 `NoLed`。 |
| REQ-4 | 必须接入物理 mute 开关 GPIO3。 |
| REQ-5 | mute 打开时必须阻止新的按钮听音入口。 |
| REQ-6 | mute 在当前 listening/connecting 时打开，必须停止当前听音流程，不能继续上传麦克风音频；speaking 时打开 mute 不得中断当前 TTS 播放。 |
| REQ-7 | mute 必须按麦克风隐私静音处理，不得把它实现成扬声器音量为 0。 |
| REQ-8 | 必须接入旋钮 GPIO16/GPIO18 调节输出音量。 |
| REQ-9 | 旋钮每格音量变化 10，范围限制为 0..100，并复用 `AudioCodec::SetOutputVolume()` 的现有持久化。 |
| REQ-10 | 旋钮不得改变 `VoicePeAudioCodec` 输入增益、XMOS 增益或 RMS 计算口径。 |
| REQ-11 | 必须接入耳机检测 GPIO17 并记录插拔日志。 |
| REQ-12 | 005 阶段耳机检测不得切换扬声器/耳机输出路由。 |
| REQ-13 | 本阶段不得启用本地唤醒词。 |
| REQ-14 | 本阶段不得启用 AEC 或伪造 reference channel。 |
| REQ-15 | 实施后必须保持 004 已验证的小智一次语音问答成功。 |

## 验收标准

| 编号 | 验收 |
|---|---|
| AC-1 | 固件启动后，LED 环在 starting/connecting/listening/speaking/idle 状态下有可见变化。 |
| AC-2 | mute 打开后单击中间按钮不会进入 listening。 |
| AC-3 | listening 或 connecting 期间打开 mute，设备停止听音并回到安全状态。 |
| AC-4 | speaking 期间打开 mute，当前 TTS 继续播放；TTS 结束后 mute 仍阻止新听音，关闭 mute 后可再次触发正常问答。 |
| AC-5 | 旋转旋钮时串口日志显示音量变化，且播放测试音/TTS 音量随之变化。 |
| AC-6 | 音量被限制在 0..100，不会越界。 |
| AC-7 | 插拔耳机时串口日志显示 jack inserted/removed 或等价状态。 |
| AC-8 | 小智一次语音问答仍成功。 |

## 非目标

| 非目标 | 原因 |
|---|---|
| 不做本地唤醒词 | 会改变语音入口，放到后续阶段。 |
| 不做 AEC | reference channel 未验证前不能开启。 |
| 不做耳机输出路由切换 | 当前只证明检测 GPIO，不碰 AIC3204 路由。 |
| 不做 LED 远程控制/MCP 工具 | 005 只做本地状态灯。 |
| 不做 Grove / 电源扩展 | 不在当前交互主路径。 |
| 不做 XMOS DFU | 属于维护能力，不属于板载交互。 |

## 需求追踪

| 需求 | Spec 章节 | 验证方式 |
|---|---|---|
| REQ-1 | LED 状态环 | 硬件观察/静态检查 |
| REQ-2 | LED 状态环 | 状态切换观察 |
| REQ-3 | 错误处理 | 代码 review |
| REQ-4 | Mute 开关 | GPIO 日志 |
| REQ-5 | Mute 开关 | 按钮验收 |
| REQ-6 | Mute 开关 | listening/connecting/speaking 中切换验收 |
| REQ-7 | Mute 开关 | 代码 review |
| REQ-8 | 旋钮音量 | 旋转验收 |
| REQ-9 | 旋钮音量 | 日志/音量边界 |
| REQ-10 | 旋钮音量 | drift check |
| REQ-11 | 耳机检测 | 插拔日志 |
| REQ-12 | 耳机检测 | 代码 review |
| REQ-13 | 非目标 | config 检查 |
| REQ-14 | 非目标 | config/代码检查 |
| REQ-15 | 回归验证 | 小智端到端 |

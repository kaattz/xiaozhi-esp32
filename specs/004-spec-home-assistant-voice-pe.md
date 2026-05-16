# 004 Spec：Home Assistant Voice PE 支持小智

## 目标

新增一块 xiaozhi-esp32 板卡 `home-assistant-voice-pe`，让 Home Assistant Voice Preview Edition 在第一阶段完成小智语音问答最小闭环。

## 代码证据

| 文件/来源 | 证据 |
|---|---|
| `docs/custom-board.md` | 新板卡应独立目录、独立 `config.json`、独立 Kconfig/CMake 映射。 |
| `main/CMakeLists.txt` | 板卡 `.cc/.c` 文件会按 `boards/${BOARD_TYPE}` 自动 glob。 |
| `main/Kconfig.projbuild` | 板卡宏使用 `BOARD_TYPE_*`，ESP32-S3 板卡使用 `depends on IDF_TARGET_ESP32S3`。 |
| `main/audio/audio_codec.h` | `AudioCodec` 抽象足够，新增实现只需实现 `Read/Write/EnableInput/EnableOutput`。 |
| `main/audio/codecs/no_audio_codec.*` | 可参考 I2S channel 初始化，但不能直接当最终实现。 |
| `main/audio/codecs/box_audio_codec.*` | 写死 ES8311 + ES7210，不适配 Voice PE。 |
| `main/boards/esp-box-3/*` | 可参考 S3 + WifiBoard + 音频板卡结构。 |
| `main/boards/m5stack-core-s3/*` | 可参考板卡目录内多 helper 文件自动编译。 |
| Home Assistant Voice PE 官方 YAML | 提供 Voice PE GPIO、I2S、XMOS、AIC3204 真值。 |
| ESPHome `voice_kit` 组件 | 提供 XMOS reset、I2C 地址、版本读取、pipeline stage 协议。 |
| ESPHome `aic3204` 组件 | 提供 AIC3204 寄存器初始化序列；固定引用 commit `35631be260c0fd6fae1e4c945f16790979ba777c` 的 `esphome/components/aic3204/aic3204.cpp`、`aic3204.h`、`audio_dac.py`。 |
| `partitions/v2/16m.csv` | 仓库内已存在，包含 `nvs` 分区。 |
| `docs/blufi_zh.md` / `local_components/esp-wifi-connect` | 现有 WiFi 配网和凭据 NVS 存储路径。 |

## 总体流程

```text
Boot
  -> initialize board GPIO/I2C
  -> reset XMOS on GPIO4
  -> read XMOS version over I2C 0x42
  -> initialize AIC3204 and amp control
  -> initialize VoicePeAudioCodec I2S RX/TX
  -> start Wi-Fi and normal xiaozhi protocol
  -> open mic input and speaker output
  -> send audio to Xiaozhi Server
  -> play TTS through AIC3204 and internal amp
```

## 板卡接入

| 项 | 设计 |
|---|---|
| 目录 | `main/boards/home-assistant-voice-pe/` |
| Kconfig | `BOARD_TYPE_HOME_ASSISTANT_VOICE_PE` |
| C++ 类名 | `HomeAssistantVoicePeBoard` |
| build name | `home-assistant-voice-pe` |
| base class | `WifiBoard` |
| 第一阶段显示 | 不实现显示，使用无屏/基础 Display 策略，以能构建和音频问答为主 |
| 分区表 | 使用仓库已有 `partitions/v2/16m.csv`，不新增分区表 |
| WiFi 配网 | 复用 `WifiBoard` / `esp-wifi-connect` |
| 持久化 | 复用现有 `wifi` NVS 和小智配置持久化路径，不新增 Voice PE 专用 NVS schema |

`config.json` 第一阶段：

```json
{
    "target": "esp32s3",
    "builds": [
        {
            "name": "home-assistant-voice-pe",
            "sdkconfig_append": [
                "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
                "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/v2/16m.csv\"",
                "CONFIG_LANGUAGE_ZH_CN=y",
                "CONFIG_WAKE_WORD_DISABLED=y"
            ]
        }
    ]
}
```

## 硬件映射

| 功能 | 引脚/参数 |
|---|---|
| internal I2C SDA | GPIO5 |
| internal I2C SCL | GPIO6 |
| XMOS I2C address | 0x42 |
| XMOS reset | GPIO4 |
| mic BCLK | GPIO13 |
| mic LRCLK | GPIO14 |
| mic DIN | GPIO15 |
| mic sample format | 16 kHz, 32-bit, stereo, I2S secondary/slave |
| speaker BCLK | GPIO8 |
| speaker LRCLK | GPIO7 |
| speaker DOUT | GPIO10 |
| speaker sample format | 48 kHz, 32-bit, stereo, I2S secondary/slave |
| amp enable | GPIO47 |
| center button | GPIO0 |
| mute switch | GPIO3 |
| LED data | GPIO21 |
| LED power | GPIO45 |
| encoder A/B | GPIO16 / GPIO18 |
| jack detect | GPIO17 |

## XMOS 控制

实现一个小的板级 helper，例如 `voice_pe_xmos.h/.cc`：

| 行为 | 规则 |
|---|---|
| bus | 控制命令和健康检查必须使用同一条 internal I2C 总线：SDA GPIO5、SCL GPIO6 |
| reset | 初始化 I2C 后，GPIO4 拉高至少 10ms，再拉低释放 XMOS |
| version read | reset 释放 500ms 后开始读取 I2C `0x42`，DFU resource `240`，GETVERSION command `88 | 0x80` |
| retry | 每 250ms 重试一次版本读取，直到成功或 reset 释放后 4000ms 超时 |
| pipeline stage | 第一阶段只写官方默认 stage 或不写；不做 DFU |
| failure | 读版本失败直接暴露错误日志，不假装成功 |

第一阶段可以先实现最小版本读和 stage 写，DFU 留到第二阶段。

## 初始化时序

| 顺序 | 动作 | 失败处理 |
|---|---|---|
| 1 | 初始化 board GPIO 和 internal I2C | 报错并停止板级音频初始化 |
| 2 | GPIO4 复位 XMOS | 报错并停止板级音频初始化 |
| 3 | internal I2C 读取 XMOS `0x42` 健康状态 | 报错并停止进入音频联调 |
| 4 | 初始化 AIC3204 | 报错并停止播放验证 |
| 5 | 配置 I2S RX/TX 和 `VoicePeAudioCodec` | 报错并停止小智端到端联调 |
| 6 | 进入现有 WiFi 配网/连接流程 | 按现有 `WifiBoard` 行为处理 |
| 7 | 连接小智 Server | 按现有小智协议错误处理 |

## 音频架构

新增 `VoicePeAudioCodec`，不修改 `AudioCodec` 抽象。

| 子项 | 设计 |
|---|---|
| 输入 | I2S RX，16 kHz，32-bit，stereo，优先取 XMOS channel 1 / NS 通道作为 mic |
| 输出 | I2S TX，48 kHz，32-bit，stereo，写入 AIC3204 |
| I2S 主从 | 必须按官方 ESPHome `i2s_mode: secondary` 配为 ESP-IDF `I2S_ROLE_SLAVE`，BCLK/WS 由 Voice PE 外部音频硬件驱动，ESP32-S3 不能主动输出 BCLK/WS |
| `input_reference_` | 第一阶段 `false` |
| `input_channels_` | 第一阶段 `1` |
| `output_channels_` | 第一阶段按现有播放链路需要设置，优先保证 TTS 能播 |
| 小智 TTS 采样率 | WebSocket hello 里当前上报 `sample_rate=16000`，实际播放以协议包里的 `packet->sample_rate` 为准 |
| 采样率转换 | `VoicePeAudioCodec` 声明硬件真实采样率：输入 16k、输出 48k；输入不做重采样，TTS/解码输出由现有 `AudioService::SetDecodeSampleRate()` 输出重采样器转到 48k 后再调用 `Write` |
| `Write` 职责 | `VoicePeAudioCodec::Write()` 不做 16k->48k 重采样，只写入已经由 `AudioService` 转成 48k 的 PCM；如果上层没有成功创建输出重采样器，播放验证失败 |
| 重采样失败 | `AudioService` 创建输出重采样器失败时，播放验证失败，不能用变速播放或静音假通过 |
| 32-bit 转 16-bit | `Read()` 从选定 mic 通道读取 signed 32-bit PCM；NS 通道第一阶段实测固定 24 倍增益，算术右移 8 bit 后再乘以 3/2，最后饱和到 `INT16_MIN..INT16_MAX` |
| I2S read timeout | ESP-IDF 5.5 `i2s_channel_read()` timeout 参数单位是毫秒，Voice PE 代码必须直接传毫秒值，不能传 `pdMS_TO_TICKS()` 结果 |
| RMS 口径 | 对 `Read()` 产出的、送入小智链路前的 int16 PCM 计算 RMS |
| RMS 通过线 | 30cm 正常说话窗口平均 RMS - 安静窗口平均 RMS >= 200 |
| 音量 | 先用软件缩放或 AIC3204 输出音量，不能降级静音 |
| AIC3204 | 按固定 commit 的 `esphome/components/aic3204/aic3204.cpp` 寄存器顺序写入，禁止改用浮动分支 |
| amp | `EnableOutput(true)` 时使能 GPIO47，关闭输出时关闭或保留按实现验证决定 |

## AIC3204 初始化来源

| 固定来源 | 用途 |
|---|---|
| `https://github.com/esphome/esphome/blob/35631be260c0fd6fae1e4c945f16790979ba777c/esphome/components/aic3204/aic3204.cpp` | 寄存器初始化顺序和写寄存器行为的主来源 |
| `https://github.com/esphome/esphome/blob/35631be260c0fd6fae1e4c945f16790979ba777c/esphome/components/aic3204/aic3204.h` | 寄存器地址、类接口和状态定义 |
| `https://github.com/esphome/esphome/blob/35631be260c0fd6fae1e4c945f16790979ba777c/esphome/components/aic3204/audio_dac.py` | ESPHome schema/codegen 参考，不作为寄存器顺序来源 |

Task 1 的 evidence 文件必须从上述固定 commit 提取 AIC3204 寄存器写入列表；Task 4 实施时按 evidence 和 `aic3204.cpp` 逐项对照。

## 配网和持久化

| 项 | 设计 |
|---|---|
| WiFi 凭据 | 复用现有 `esp-wifi-connect`，写入 `wifi` NVS 命名空间 |
| 配网入口 | 继承 `WifiBoard` 现有配网/重连行为 |
| 小智 Server 配置 | 复用现有小智配置读取路径 |
| 设备 ID / Client ID | 复用现有 NVS/系统信息生成逻辑，不新增 Voice PE 专用身份字段 |
| 第一阶段新增 NVS key | 无 |

## 错误处理

| 错误 | 行为 |
|---|---|
| XMOS I2C 读失败 | 记录明确错误，停止继续宣称音频可用 |
| I2S RX 无数据 | RMS 验证失败，不进入小智联调 |
| AIC3204 初始化失败 | 播放验证失败，不进入 TTS 联调 |
| 小智 Server 连接失败 | 按现有协议错误处理，不改协议语义 |

## 验证策略

| 层 | 验证 |
|---|---|
| 静态 | `rg` 检查新板卡、Kconfig、CMake、禁 wake/AEC 设置 |
| 构建 | `python scripts/release.py home-assistant-voice-pe` 或等价 ESP-IDF build |
| XMOS | 串口看到 reset 和 version read 成功 |
| 麦克风 | 串口 RMS 差值：30cm 正常说话窗口平均值比安静窗口高至少 200 |
| 播放 | AIC3204 + amp 播放 1 kHz 测试音 |
| 播放入口 | Voice PE 中间按钮双击在 idle 或 WiFi 配网态触发 1 kHz 测试音，便于无服务器时验证真实扬声器链路 |
| 端到端 | 一次小智语音问答 |

## 硬件验证记录

| 项 | 2026-05-16 实测结果 |
|---|---|
| XMOS | 启动日志读到 firmware `1.3.1`，pipeline 设置为 `channel0=AGC channel1=NS` |
| 麦克风 | 选用 `channel1=NS`；24 倍固定增益下，安静 RMS 约 `405..613`，说话 RMS 约 `7971..10248` |
| 扬声器 | 中间按钮双击可听到 1 kHz 测试音 |
| WiFi | 通过现有配网页面写入凭据，重启后连接 SSID `home`，获得 IP `192.168.168.101` |
| 小智连接 | MQTT 连接 `mqtt.xiaozhi.me` 成功，状态进入 `idle` |
| 端到端 | 中间按钮单击后进入 `connecting -> listening -> speaking`，小智 Server 返回 TTS，用户确认“小智回复成功” |
| TTS 采样率 | Server 下行 `24000`，现有 `AudioService` 日志显示重采样到设备输出 `48000` |

## 需求追踪

| 需求 | Spec 章节 | 实施任务 | 验证 |
|---|---|---|---|
| REQ-1 | 板卡接入 | Task 2 | 构建/静态检查 |
| REQ-2 | 板卡接入 | Task 2 | config/build |
| REQ-3 | 板卡接入 | Task 2 | config |
| REQ-4 | 音频架构 | Task 5c | review/build |
| REQ-5 | XMOS 控制 | Task 3 | 串口日志 |
| REQ-6 | XMOS 控制 | Task 3 | I2C 版本读取 |
| REQ-7 | 音频架构 | Task 5a/6 | RMS |
| REQ-8 | 验证策略 | Task 6 | RMS |
| REQ-9 | 音频架构 | Task 4/5b | 测试音 |
| REQ-10 | 硬件映射 | Task 5b | 测试音 |
| REQ-11 | 配网和持久化 | Task 7 | WiFi 配网/重连 |
| REQ-12 | 配网和持久化 | Task 8 | NVS/配置 review |
| REQ-13 | 验证策略 | Task 7 | 端到端 |
| REQ-14 | 音频架构 | Task 8 | drift check |
| REQ-15 | XMOS 控制 | Task 8 | drift check |
| REQ-16 | 硬件映射 | Task 8 | drift check |

# xiaozhi-esp32 烧录说明

本文档记录在 Windows PowerShell 下，把 `xiaozhi-esp32` 烧录到 ESP32-S3 开发板的流程。默认以太极小派为例，ESP32-S3-BOX-3B 只需要按本文的差异配置修改板型和硬件选项。

## 1. 打开 PowerShell

不要直接用开始菜单里坏掉的 `ESP-IDF 5.5 PowerShell` 快捷方式。打开普通 PowerShell，然后手动激活 ESP-IDF：

```powershell
. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
```

确认 `idf.py` 可用：

```powershell
idf.py --version
```

如果提示找不到 `idf.py`，说明当前 PowerShell 还没有激活 ESP-IDF，重新执行上面的 `export.ps1`。

## 2. 进入项目目录

```powershell
cd C:\Code\xiaozhi-esp32
```

## 3. 确认开发板串口和 Flash 大小

先在设备管理器里确认串口号。当前这块板之前识别为 `COM3`。

用 `esptool.py` 读取芯片信息：

```powershell
esptool.py --chip esp32s3 -p COM3 flash_id
```

正常结果应类似：

```text
Chip is ESP32-S3
Features: WiFi, BLE, Embedded PSRAM 8MB
USB mode: USB-Serial/JTAG
Detected flash size: 16MB
```

如果串口不是 `COM3`，后续命令把 `COM3` 换成你的实际串口。

## 4. 设置编译目标

```powershell
idf.py set-target esp32s3
```

如果提示 `build` 目录不是 CMake build directory，可以手动删除旧的 build 目录：

```powershell
Remove-Item -Recurse -Force .\build
idf.py set-target esp32s3
```

## 5. 打开 menuconfig

```powershell
idf.py menuconfig
```

进入以下菜单：

```text
Xiaozhi Assistant
```

建议配置：

| 配置项 | 建议值 |
|---|---|
| Board Type | 按实际硬件选择，见下面“硬件差异配置” |
| Flash Assets | Flash Default Assets |
| Default Language | Chinese |
| Wake Word Implementation Type | Wakenet model with AFE |
| Send Wake Word Data | 开启 |
| Wake Arbitration Gateway URL | 按需填写，例如 `http://192.168.166.50:8125` |

确认 Flash 大小：

```text
Serial flasher config -> Flash size -> 按 `esptool.py flash_id` 检测值选择
```

不要所有板子都固定选 `16 MB`。太极小派和 ESP32-S3-BOX-3B 常见是 `16 MB`，小智AI Moji小智 / Movecall Moji 是 `8 MB`。

### 硬件差异配置

#### 太极小派 esp32s3

```text
Xiaozhi Assistant -> Board Type -> 太极小派 esp32s3
```

还需要进入：

```text
Xiaozhi Assistant -> TAIJIPAI_S3_CONFIG
```

| 板子批次 | I2S 建议 |
|---|---|
| 2025 年 7 月前 / 批次小于等于 2528 | I2S Type STD |
| 2025 年 7 月后 / 批次大于 2528 | I2S Type PDM |
| 不确定批次 | 先用 STD；如果麦克风或唤醒异常，再改 PDM 重刷 |

#### ESP32-S3-BOX-3B

项目里对应的板型是乐鑫官方 `ESP-BOX-3`：

```text
Xiaozhi Assistant -> Board Type -> Espressif ESP-BOX-3
```

建议只额外确认这些项：

| 配置项 | 建议值 |
|---|---|
| Serial flasher config -> Flash size | 16 MB |
| Xiaozhi Assistant -> Select display style | 默认消息风格，或按需选择 |
| Xiaozhi Assistant -> Enable Device-Side AEC | 开启 |
| Wake Word Implementation Type | Wakenet model with AFE |

ESP32-S3-BOX-3B 不需要配置 `TAIJIPAI_S3_CONFIG`。如果选择表情动画风格，需要按 `main/boards/esp-box-3/README.md` 额外配置自定义资源文件。

#### 小智AI Moji小智 / Movecall Moji

这块板的 `Board Type` 直接选项目里的 Movecall Moji：

```text
Xiaozhi Assistant -> Board Type -> Movecall Moji 小智AI衍生版
```

建议只额外确认这些项：

| 配置项 | 建议值 |
|---|---|
| Serial flasher config -> Flash size | 8 MB |
| Partition Table -> Custom partition CSV file | partitions/v2/8m.csv |
| Wake Word Implementation Type | Wakenet model with AFE |

如果屏幕、麦克风或按键异常，先确认卖家给的板型是否确实是 `Movecall Moji 小智AI衍生版`，不要改选 `ESP-BOX-3`。

保存退出：

```text
S -> Enter -> Q
```

## 6. 编译固件

```powershell
idf.py build
```

编译成功会看到类似：

```text
Project build complete.
Generated C:/Code/xiaozhi-esp32/build/xiaozhi.bin
```

如果出现 `_IO`、`_IOR`、`_IOW` redefined 这类 warning，目前只是 warning，不影响编译通过。

## 7. 烧录到开发板

开发板插电脑后执行：

```powershell
idf.py -p COM3 flash
```

如果串口不是 `COM3`，替换成你的实际串口。

也可以用 ESP-IDF 输出的完整 `esptool` 命令烧录 16MB Flash。下面这条只适用于 16MB Flash 的板子，8MB 的 Movecall Moji 不要用这条，直接用 `idf.py -p COM3 flash`：

```powershell
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0xd000 build\ota_data_initial.bin 0x20000 build\xiaozhi.bin 0x800000 build\generated_assets.bin
```

## 8. 查看运行日志

```powershell
idf.py -p COM3 monitor
```

退出监控：

```text
Ctrl + ]
```

正常唤醒日志类似：

```text
Wake word detected: 你好小智
```

如果开启 Wake Arbitration，还会看到：

```text
Wake arbitration cost: xxx ms
```

## 9. 进入网页端高级设置

网页端是配网模式里的页面，不是一直常驻的管理后台。

| 场景 | 进入方式 |
|---|---|
| 没配过 WiFi | 设备会自动进入配网模式 |
| WiFi 连不上 | 等连接超时后会自动进入配网模式 |
| 太极小派已配过 WiFi | 重启设备，在启动阶段短触屏幕一次进入配网模式 |

进入配网模式后：

1. 手机或电脑连接热点 `Xiaozhi-XXXX`。
2. 浏览器打开 `http://192.168.4.1`。
3. 切换到 `高级选项`。

高级设置里有：

| 设置项 | 默认值 | 说明 |
|---|---|---|
| 自定义 OTA 地址 | 空 | 用于替换 OTA 配置地址，不是关闭 OTA 的开关 |
| 启用自动固件升级 | 关闭 | 关闭时仍检查 OTA 配置，但不自动刷固件 |
| 启用 Wake Arbitration | 关闭 | 关闭时直接走原始小智云端流程 |

Wake Arbitration 只向 gateway 上报 `device_id`、`client_id`、`wake_word` 和 `wake_rms_dbfs`，不上传唤醒音频。`wake_rms_dbfs` 是本地从唤醒 PCM 或滚动 RMS 统计计算出的 dBFS 值，用于 gateway 在同组设备间选择离用户更近的一台。

## 10. 常见问题

| 问题 | 处理 |
|---|---|
| `idf.py` 找不到 | 先执行 `. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1` |
| `esptool.py` 连不上 | 检查串口号；必要时按住 BOOT 再复位进入下载模式 |
| 唤醒没反应 | 先看日志有没有 `Wake word detected`；没有则检查 I2S Type STD/PDM |
| 唤醒后不进入会话 | 先确认网页里 `启用 Wake Arbitration` 是否关闭；如果开启，确认 gateway 是否可访问 |
| 多台设备同时回复 | 确认 gateway `devices.yaml` 是否把容易串音的设备放进同一个 `wake_group`，并按设备麦克风差异调整 `mic_gain_offset_db` |
| 刷错配置 | 可以重新执行 `menuconfig`、`build`、`flash` 重刷 |

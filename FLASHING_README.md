# xiaozhi-esp32 烧录说明

本文档记录在 Windows PowerShell 下，把 `xiaozhi-esp32` 烧录到 ESP32-S3 太极小派开发板的流程。

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
| Board Type | 太极小派 esp32s3 |
| Flash Assets | Flash Default Assets |
| Default Language | Chinese |
| Wake Word Implementation Type | Wakenet model with AFE |
| Send Wake Word Data | 开启 |
| Wake Arbitration Gateway URL | 按需填写，例如 `http://192.168.166.50:8125` |

确认 Flash 大小：

```text
Serial flasher config -> Flash size -> 16 MB
```

你之前用 `esptool.py flash_id` 已确认这块 ESP32-S3 是 `16MB` Flash，所以这里必须是 `16 MB`。如果不是，改成 `16 MB` 再保存。

太极小派还需要进入：

```text
Xiaozhi Assistant -> TAIJIPAI_S3_CONFIG
```

| 板子批次 | I2S 建议 |
|---|---|
| 2025 年 7 月前 / 批次小于等于 2528 | I2S Type STD |
| 2025 年 7 月后 / 批次大于 2528 | I2S Type PDM |
| 不确定批次 | 先用 STD；如果麦克风或唤醒异常，再改 PDM 重刷 |

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

也可以用 ESP-IDF 输出的完整 `esptool` 命令烧录 16MB Flash：

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

## 10. 常见问题

| 问题 | 处理 |
|---|---|
| `idf.py` 找不到 | 先执行 `. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1` |
| `esptool.py` 连不上 | 检查串口号；必要时按住 BOOT 再复位进入下载模式 |
| 唤醒没反应 | 先看日志有没有 `Wake word detected`；没有则检查 I2S Type STD/PDM |
| 唤醒后不进入会话 | 先确认网页里 `启用 Wake Arbitration` 是否关闭；如果开启，确认 gateway 是否可访问 |
| 刷错配置 | 可以重新执行 `menuconfig`、`build`、`flash` 重刷 |

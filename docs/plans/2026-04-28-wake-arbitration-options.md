# Wake Arbitration 方案对比：wake_group + wake_rms_dbfs 与音频指纹

## 背景

多台小智设备放在不同房间后，会遇到两类唤醒冲突：

| 场景 | 期望结果 |
|---|---|
| 一个房间的人喊小智，开门导致附近房间设备也被唤醒 | 只让离人最近、声音最清楚的设备响应 |
| 不同房间不同人差不多同时喊小智 | 各自房间的小智都能响应 |

当前全局仲裁逻辑是“谁先唤醒，谁占用会话”。这个逻辑太粗，会把第二种正常并发场景误拦截。

## 方案一：wake_group + wake_rms_dbfs

### 核心思路

把容易互相串音的设备放进同一个 `wake_group`。同一组内多个设备被唤醒时，gateway 等一个很短的窗口，收集这些唤醒请求，然后选校准后唤醒响度最大的设备。

不同 `wake_group` 之间不互相竞争，可以同时响应。

### 示例配置

```yaml
devices:
  living_room:
    device_id: "aa:bb:cc:dd:ee:ff"
    room_id: "living_room"
    room_name: "客厅"
    ha_area_id: "living_room"
    wake_group: "public_area"
    priority: 100
    mic_gain_offset_db: 0.0

  dining_room:
    device_id: "11:22:33:44:55:66"
    room_id: "dining_room"
    room_name: "餐厅"
    ha_area_id: "dining_room"
    wake_group: "public_area"
    priority: 80
    mic_gain_offset_db: -2.5

  master_bedroom:
    device_id: "22:33:44:55:66:77"
    room_id: "master_bedroom"
    room_name: "主卧"
    ha_area_id: "master_bedroom"
    wake_group: "master_bedroom"
    priority: 100
    mic_gain_offset_db: 0.0
```

### 判定规则

| 条件 | 处理 |
|---|---|
| 单设备 `wake_group` | 直接允许，不等待 |
| 同组 300ms 内只有一个设备唤醒 | 允许该设备 |
| 同组 300ms 内多个设备唤醒 | 选校准后的 `wake_rms_dbfs` 最大的 |
| 最大音量和第二名差距很小 | 用 `priority` 作为次级排序 |
| 不同 `wake_group` 同时唤醒 | 各自响应，互不阻塞 |

### 数据流

```text
ESP32 检测到唤醒词
-> 读取唤醒响度 wake_rms_dbfs
-> POST /wake-detected 到 gateway
-> gateway 按 wake_group 收集短时间窗口内的候选设备
-> 同组内选 wake_rms_dbfs + mic_gain_offset_db 最大者
-> 返回 allow_session 或 deny_session
```

### 响应速度

| 环节 | 预计耗时 |
|---|---:|
| ESP32 本地唤醒检测 | 原本就有，不新增 |
| ESP32 请求 gateway | 局域网通常 10-50ms |
| gateway 等待同组候选 | 默认 300ms |
| gateway 排序计算 | 1ms 级别 |
| 总新增延迟 | 约 300-500ms |

建议默认值：

| 参数 | 建议值 |
|---|---:|
| 同组仲裁窗口 | 300ms |
| gateway HTTP 超时 | 800ms |
| 音量差阈值 | 3-6dB |

### 优点

| 项目 | 说明 |
|---|---|
| 实现简单 | ESP32 只需要多上报一个 `wake_rms_dbfs` |
| 速度稳定 | 不需要上传或分析唤醒音频 |
| 隐私好 | 不额外传语音片段到 gateway |
| 适合第一版 | 能解决大部分串音误唤醒 |

### 限制

| 问题 | 说明 |
|---|---|
| 同组内两个人同时喊 | 仍然只能选一个 |
| 音量受设备摆放影响 | 麦克风朝向、遮挡、噪声会影响判断 |
| 需要手动分组 | 用户需要配置哪些房间容易串音 |

## 方案二：gateway 音频指纹

### 核心思路

ESP32 在唤醒后把 1-2 秒唤醒音频上传给 gateway。gateway 对音频提取特征，用来判断多台设备听到的是不是同一段声音。

如果是同一段声音，说明大概率是串音误唤醒；如果不是同一段声音，说明可能是不同人分别唤醒。

### 判定规则

| 条件 | 处理 |
|---|---|
| 同组多个设备音频指纹相似 | 认为是同一次唤醒，选音量最大的设备 |
| 同组多个设备音频指纹不相似 | 认为可能是不同人，允许多个设备 |
| 不同组多个设备唤醒 | 默认各自响应，也可以跳过指纹比对 |
| 指纹提取失败 | 不继续猜，按 `wake_group + wake_rms_dbfs` 规则处理 |

### 数据流

```text
ESP32 检测到唤醒词
-> 保存唤醒音频
-> 编码或上传唤醒音频到 gateway
-> gateway 等待同组候选
-> gateway 解码并提取音频特征
-> 比较候选设备音频相似度
-> 同一段声音选音量最大，不同声音各自允许
```

### 响应速度

| 环节 | 预计耗时 |
|---|---:|
| ESP32 编码唤醒音频 | 约 200-300ms |
| 局域网上传 | 几 ms 到几十 ms |
| gateway 解码/提特征 | PC 上约 20-100ms，小主机可能更慢 |
| gateway 等待同组候选 | 推荐 500ms |
| 总新增延迟 | 约 500-900ms |

### 优点

| 项目 | 说明 |
|---|---|
| 更能区分“同一声”和“不同声” | 比单纯音量更准确 |
| 能处理同组内两个人同时喊 | 有机会允许两个设备同时响应 |
| 后续可扩展 | 可以继续接声纹或更复杂模型 |

### 限制

| 问题 | 说明 |
|---|---|
| 响应更慢 | 用户会感觉小智醒得更慢 |
| 实现复杂 | 需要音频上传、解码、特征提取、相似度比较 |
| 资源更重 | gateway 需要更多 CPU 和依赖 |
| 隐私更敏感 | 局域网内会传输唤醒语音片段 |
| 准确性不是绝对 | 混响、噪声、不同麦克风会影响指纹相似度 |

## 两个方案对比

| 维度 | wake_group + wake_rms_dbfs | gateway 音频指纹 |
|---|---|---|
| 推荐阶段 | 第一版 | 第二版增强 |
| 实现难度 | 低 | 中 |
| 新增延迟 | 约 300-500ms | 约 500-900ms |
| 是否传音频到 gateway | 否 | 是 |
| 是否能区分同组两个人同时喊 | 不能很好区分 | 有机会区分 |
| 是否适合长期主逻辑 | 适合 | 适合作为增强 |

## 推荐落地顺序

第一阶段先做 `wake_group + wake_rms_dbfs`：

```text
多设备组：等 300ms，选 wake_rms_dbfs + mic_gain_offset_db 最大
单设备组：直接允许
不同组：互不阻塞
```

第二阶段再加音频指纹：

```text
只在同一个 wake_group 内多个设备同时唤醒时启用
先用音频指纹判断是不是同一段声音
同一段声音选音量最大
不同声音允许各自响应
```

## 当前建议

先不要直接上音频指纹。第一版优先实现 `wake_group + wake_rms_dbfs + mic_gain_offset_db + priority`，因为它速度快、实现少、失败面小，已经能解决大部分“开门串音误唤醒”问题。

音频指纹适合作为第二阶段增强，用来处理“同一个 wake_group 内，确实有两个人同时喊”的更复杂场景。

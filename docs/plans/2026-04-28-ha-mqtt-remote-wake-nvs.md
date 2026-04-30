# Home Assistant MQTT Remote Wake With NVS Configuration Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a Home Assistant MQTT integration to `xiaozhi-esp32` so HA can discover the device, remotely wake Xiaozhi with a text instruction, receive state/STT/TTS text updates, and configure HA MQTT parameters through NVS-backed web configuration.

**Architecture:** Add a second MQTT connection dedicated to Home Assistant. This connection is independent from the existing Xiaozhi server MQTT/WebSocket protocol. HA writes a `text` entity to trigger `Application::WakeWordInvoke(text)`, and the existing Xiaozhi protocol still handles LLM, TTS, audio streaming, and MCP device control.

**Tech Stack:** ESP-IDF, C++, `Settings`/NVS, existing `esp-wifi-connect` captive portal, Home Assistant MQTT discovery, `johboh/homeassistantentities`, `johboh/mqttremote`.

---

## Scope

| Item | Decision |
|---|---|
| HA MQTT parameters | Stored in NVS, configured from the existing web configuration page |
| Flash support | Must work with existing 8M and 16M partition tables without changing partition sizes |
| Xiaozhi protocol | Do not change server protocol semantics beyond calling existing `WakeWordInvoke(text)` |
| HA device control | Keep existing MCP path; do not implement direct HA service calls in this feature |
| TTS | Do not add direct TTS playback; HA text input remains a remote user instruction |
| Worktree | Do not create a new worktree unless user explicitly approves |

## Data Flow

```text
HA MQTT text entity
  -> HomeAssistantManager text callback
  -> Application::Schedule(...)
  -> Application::WakeWordInvoke(text)
  -> ContinueWakeWordInvoke(text)
  -> Protocol::SendWakeWordDetected(text)
  -> Xiaozhi server LLM/TTS
  -> ESP32 audio playback

Xiaozhi server stt/tts/state
  -> Application protocol callbacks / state machine
  -> HomeAssistantManager publish methods
  -> HA MQTT entities and logbook
```

## NVS Keys

Use namespace `ha_mqtt`.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `enabled` | bool | `false` | Whether to start HA MQTT integration |
| `host` | string | empty | MQTT broker host or IP |
| `port` | int | `1883` | MQTT broker port |
| `username` | string | empty | MQTT username |
| `password` | string | empty | MQTT password |
| `client_id` | string | generated from MAC if empty | MQTT client id |
| `device_name` | string | board/device name | HA device display name |
| `model` | string | `BOARD_NAME` | HA device model |
| `manufacturer` | string | `xiaozhi-esp32` | HA manufacturer field |

## HA Entities

| Entity | HA type | Purpose |
|---|---|---|
| `wake up` | button | Remote wake without text |
| `指令` / `wake_word` | text | Remote user instruction |
| `状态` / `device_status` | sensor/string | Device state: idle/listening/speaking/etc. |
| `user` / `user_message` | sensor/string | Last STT text |
| `assistant` / `assistant_message` | sensor/string | Last assistant sentence text |
| `音量` / `volume` | number | Set output volume |
| `亮度` / `brightness` | number | Set screen brightness when backlight exists |

## Task 1: Implementation Precheck

<files>
- Read: `main/application.cc`
- Read: `main/application.h`
- Read: `main/protocols/protocol.cc`
- Read: `main/settings.cc`
- Read: `main/settings.h`
- Read: `main/idf_component.yml`
- Read: `main/CMakeLists.txt`
- Read: `local_components/esp-wifi-connect/wifi_configuration_ap.cc`
- Read: `local_components/esp-wifi-connect/include/wifi_configuration_ap.h`
- Read: `local_components/esp-wifi-connect/assets/wifi_configuration.html`
- Read: `partitions/v1/8m.csv`
- Read: `partitions/v2/8m.csv`
- Reference: `https://github.com/soulgod001/xiaozhi-esp32/blob/main/main/home_assistant_manager.cc`
- Reference: `https://github.com/soulgod001/xiaozhi-esp32/blob/main/main/home_assistant_manager.h`

<action>
- Confirm the current project already has `Application::WakeWordInvoke(const std::string&)`.
- Confirm `Protocol::SendWakeWordDetected()` sends the received string as `listen/detect/text`.
- Confirm NVS partition is present and remains `0x4000` in 8M/16M tables.
- Confirm web configuration already has an advanced config API that can be extended.

<verify>
- Run `rg -n "WakeWordInvoke|SendWakeWordDetected|Settings\\(|advanced/submit|advanced/config" main local_components`.
- Run `Get-Content partitions\v1\8m.csv; Get-Content partitions\v2\8m.csv`.

<done>
- No implementation starts until the exact integration points above are confirmed.

## Task 2: Add Dependencies

<files>
- Modify: `main/idf_component.yml`

<action>
- Add component dependencies:
  - `johboh/homeassistantentities: '>=8.0.14'`
  - `johboh/mqttremote: '>=6.0.2'`
- Keep them as normal dependencies, not board-specific dependencies.

<verify>
- Run the normal ESP-IDF dependency resolution for the selected board.
- Expected: component manager resolves both libraries without changing partition tables.

<done>
- Build configuration can find headers:
  - `MQTTRemote.h`
  - `HaBridge.h`
  - `entities/HaEntityText.h`
  - `entities/HaEntityButton.h`
  - `entities/HaEntityString.h`
  - `entities/HaEntityNumber.h`

## Task 3: Create HA MQTT Settings Wrapper

<files>
- Create: `main/ha_mqtt_settings.h`
- Create: `main/ha_mqtt_settings.cc`
- Modify: `main/CMakeLists.txt`

<action>
- Create a small value object for HA MQTT settings.
- Read from NVS namespace `ha_mqtt` using existing `Settings`.
- Validate strictly:
  - `enabled=false` means disabled, do not connect.
  - `enabled=true` requires non-empty `host`.
  - `port` must be `1..65535`.
  - empty `client_id` is allowed only if code generates a stable MAC-based id.
- Do not silently fall back to the Xiaozhi server MQTT settings.

<verify>
- Build target after adding files.
- Add compile-time use in a temporary local check or constructor path, then remove any temporary debug code.

<done>
- HA MQTT settings are read from NVS only.
- Missing config disables the feature without affecting normal Xiaozhi boot.

## Task 4: Create HomeAssistantManager

<files>
- Create: `main/home_assistant_manager.h`
- Create: `main/home_assistant_manager.cc`
- Modify: `main/CMakeLists.txt`

<action>
- Implement singleton `HomeAssistantManager`.
- Own a dedicated `MQTTRemote` instance for HA.
- Own HA discovery entities:
  - button wake up
  - text wake_word
  - string device_status
  - string user_message
  - string assistant_message
  - number volume
  - number brightness
- Publish discovery only after HA MQTT connects.
- Add public publish methods:
  - `PublishDeviceState(const char* state)`
  - `PublishUserMessage(const char* text)`
  - `PublishAssistantMessage(const char* text)`
  - `PublishVolume(int volume)`
  - `PublishBrightness(int brightness)`
- Add control callbacks:
  - wake button calls `Application::WakeWordInvoke("你好小智")` or empty wake behavior only if confirmed during implementation.
  - text entity calls `Application::WakeWordInvoke(text)`.
  - volume entity calls codec `SetOutputVolume`.
  - brightness entity calls backlight `SetBrightness` only when backlight exists.

<verify>
- Build selected ESP32 target.
- Review generated discovery topic names and entity IDs in logs.
- Confirm no HA MQTT connection is attempted when `enabled=false`.

<done>
- HA MQTT is fully independent from Xiaozhi protocol connection.
- The manager exposes narrow publish/control methods only.

## Task 5: Wire Manager Into Application Startup

<files>
- Modify: `main/application.cc`

<action>
- Include `home_assistant_manager.h`.
- After network is available and before/after protocol initialization, start `HomeAssistantManager` if HA MQTT is enabled.
- Use existing scheduling rules: callbacks from HA MQTT must enter app state through `Application::Schedule(...)`.
- Do not copy the fork's older `OnWakeWordDetected(text, true)` flow.
- Use current project flow:
  - `HomeAssistantManager::onText(text)`
  - `Application::WakeWordInvoke(text)`
  - `ContinueWakeWordInvoke(text)`

<verify>
- Build selected board.
- Manual log check:
  - HA manager starts only after network is available.
  - normal Xiaozhi protocol still starts without HA settings.

<done>
- Normal Xiaozhi boot path is unchanged when HA MQTT is disabled.

## Task 6: Publish Device State To HA

<files>
- Modify: `main/application.cc`

<action>
- Publish state from the state-machine callback area, preferably `Application::OnStateChanged(DeviceState old_state, DeviceState new_state)`.
- Convert state enum to stable strings matching existing `STATE_STRINGS` if available.
- Publish only after manager is started and enabled.
- Do not publish from every random `SetDeviceState()` call; centralize through state-change handler.

<verify>
- Build selected board.
- Manual run:
  - boot publishes `idle`.
  - remote text instruction changes to `connecting/listening/speaking/idle` as appropriate.

<done>
- HA logbook shows state transitions without duplicate spam.

## Task 7: Publish STT And Assistant Text To HA

<files>
- Modify: `main/application.cc`

<action>
- In protocol JSON callback:
  - On `type == "stt"` and valid `text`, call `PublishUserMessage(text)`.
  - On `type == "tts"` + `state == "sentence_start"` and valid `text`, call `PublishAssistantMessage(text)`.
- Keep existing display update behavior.
- Do not alter server messages or LLM flow.

<verify>
- Build selected board.
- Manual run:
  - say a phrase, HA `user` entity changes.
  - Xiaozhi replies, HA `assistant` entity changes.

<done>
- HA can observe both sides of the conversation.

## Task 8: Extend NVS Web Configuration Backend

<files>
- Modify: `local_components/esp-wifi-connect/include/wifi_configuration_ap.h`
- Modify: `local_components/esp-wifi-connect/wifi_configuration_ap.cc`

<action>
- Add HA MQTT fields to `WifiConfigurationAp` private members.
- Load `ha_mqtt` NVS namespace in `StartAccessPoint()` or a helper method:
  - enabled
  - host
  - port
  - username
  - password
  - client_id
  - device_name
  - model
  - manufacturer
- Extend `/advanced/config` JSON response to include `ha_mqtt`.
- Extend `/advanced/submit` JSON handling to save `ha_mqtt`.
- Keep max request body size valid; increase from `1024` only if the final JSON really exceeds it.
- Validate before saving:
  - if enabled, host must be non-empty.
  - port must be valid.
  - string lengths must be bounded.

<verify>
- Build selected board.
- Use config AP:
  - `GET /advanced/config` returns HA MQTT fields.
  - `POST /advanced/submit` saves HA MQTT fields.
  - reboot and re-open config shows saved values.

<done>
- HA MQTT config persists across reboot in NVS.

## Task 9: Extend Web Configuration UI

<files>
- Modify: `local_components/esp-wifi-connect/assets/wifi_configuration.html`

<action>
- Add an "Home Assistant MQTT" section to the existing advanced settings UI.
- Add inputs:
  - enable checkbox
  - host
  - port
  - username
  - password
  - client id
  - device name
  - model
  - manufacturer
- Do not add a large frontend framework.
- Reuse existing page CSS and JS style.
- On save, include `ha_mqtt` object in the existing advanced submit payload.
- Show that reboot is required for connection changes to take effect.

<verify>
- Manual browser test through captive portal.
- Confirm page still fits on mobile width.
- Confirm generated embedded HTML still builds for 8M targets.

<done>
- Users can configure HA MQTT without rebuilding firmware.

## Task 10: HA MQTT Runtime Behavior And Error Handling

<files>
- Modify: `main/home_assistant_manager.cc`
- Modify: `main/ha_mqtt_settings.cc`

<action>
- If disabled or invalid config, log one clear message and do not start.
- If connection fails, expose log but do not block Xiaozhi startup.
- Do not erase HA config on connection failure.
- Do not retry aggressively; use library reconnect behavior or a conservative retry interval.
- Never treat HA MQTT failure as Xiaozhi server failure.

<verify>
- Test three cases:
  - HA disabled: no connection attempt.
  - HA enabled with wrong host: normal Xiaozhi still boots.
  - HA enabled with correct host: entities appear in HA.

<done>
- HA integration failure is isolated from voice assistant operation.

## Task 11: JSON Safety For Remote Text

<files>
- Modify: `main/protocols/protocol.cc` if needed

<action>
- Inspect `Protocol::SendWakeWordDetected`.
- If it still builds JSON by string concatenation, replace only this message construction with `cJSON` or an existing project JSON helper.
- This prevents HA text containing quotes or backslashes from breaking JSON.
- Keep output schema identical:
  - `session_id`
  - `type: listen`
  - `state: detect`
  - `text`

<verify>
- Build selected board.
- Manual remote instruction test with Chinese punctuation and English double quotes.
- Expected: server receives valid JSON and device does not log parse/send errors.

<done>
- Remote text is safe for normal user input.

## Task 12: Build Verification

<files>
- No source changes unless build failures expose missing includes or dependencies.

<action>
- Build at least one 8M board target and one 16M board target if available in local build setup.
- If full hardware matrix is too expensive, build the user's actual target first and inspect partition/app size.

<verify>
- Run the normal project build command for selected board.
- Confirm:
  - app fits current app partition.
  - no NVS partition changes are required.
  - no duplicate symbol or missing component errors.

<done>
- Compile passes for the selected target.

## Task 13: HA Manual Verification

<files>
- No source changes.

<action>
- Configure HA MQTT broker parameters through the web page.
- Reboot ESP32.
- Open Home Assistant MQTT integration device page.
- Confirm entities appear:
  - wake up
  - 指令
  - 状态
  - user
  - assistant
  - 音量
  - 亮度, only if supported
- Write text to `指令`.
- Confirm Xiaozhi wakes and replies.
- Say a response.
- Confirm `user` updates in HA.

<verify>
- HA logbook should show state changes and text changes.
- Device serial log should show HA MQTT connected and Xiaozhi protocol session opened.

<done>
- End-to-end HA remote wake works without changing Xiaozhi server code.

## Task 14: Documentation

<files>
- Create or modify: `docs/home-assistant-mqtt.md`
- Optionally modify: `README.md`

<action>
- Document what this feature does and does not do.
- Document required HA MQTT broker setup.
- Document web configuration fields.
- Document that remote text is passed as user input, not direct TTS.
- Document that HA device operations still use MCP.
- Add troubleshooting:
  - entities do not appear
  - MQTT auth failure
  - remote instruction wakes but no speech
  - volume/brightness unsupported

<verify>
- Read docs from a fresh-user perspective.
- Confirm no claim says "direct TTS" or "guaranteed exact wording".

<done>
- A user can configure and understand the feature without reading source code.

## Completion Checklist

| Check | Expected |
|---|---|
| HA disabled | Xiaozhi works exactly as before |
| HA enabled, wrong config | Xiaozhi still works; HA logs clear error |
| HA enabled, valid config | HA device and entities appear |
| Remote wake button | Wakes or prompts device without text |
| Remote text instruction | Calls existing Xiaozhi conversation path |
| STT publish | HA `user` updates |
| assistant publish | HA `assistant` updates |
| state publish | HA `状态` updates |
| 8M flash | No partition change required |
| 16M flash | No partition change required |

## Implementation Order

1. Task 1
2. Task 2
3. Task 3
4. Task 4
5. Task 5
6. Task 6
7. Task 7
8. Task 8
9. Task 9
10. Task 10
11. Task 11
12. Task 12
13. Task 13
14. Task 14

## Review Gates

| Gate | Review focus |
|---|---|
| After Task 4 | HA manager boundaries are clean; no Xiaozhi protocol mixing |
| After Task 7 | Runtime data flow is correct |
| After Task 9 | NVS/web config does not bloat 8M targets |
| After Task 11 | Remote text JSON is safe |
| Before final | Disabled HA path leaves existing behavior unchanged |


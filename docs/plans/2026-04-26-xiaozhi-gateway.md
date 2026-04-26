# Xiaozhi Gateway Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build an internal gateway on port `8125` to handle wake arbitration and provide room context to the Home Assistant MCP integration.

**Architecture:** `xiaozhi-gateway` is a standalone Python service, not part of `xiaozhi-esp32` or `ha-mcp-for-xiaozhi`. ESP32 devices ask the gateway before entering a session. HA MCP queries the same gateway before calling Home Assistant tools so room context is centralized.

**Tech Stack:** Python, FastAPI, Uvicorn, YAML config, JSON state file, pytest, Docker Compose.

---

## Confirmed Decisions

| Item | Decision |
|---|---|
| Gateway project | Standalone project: `C:\Code\xiaozhi-gateway` |
| Deployment | Internal machine/NAS |
| Port | `8125` |
| Public exposure | Not required |
| Official Xiaozhi cloud | No changes |
| First milestone | Manual active room context, then HA MCP integration, then ESP32 arbitration |
| Failure rule | If gateway is unavailable during arbitration, do not enter session |

## Runtime Topology

```text
ESP32 device  ->  xiaozhi-gateway:8125  -> wake allow/deny
HA MCP plugin ->  xiaozhi-gateway:8125  -> active room context

ESP32 device  ->  Xiaozhi official cloud  -> normal voice session
HA MCP plugin ->  Xiaozhi official cloud  -> existing reverse MCP WebSocket
```

## Gateway API

| Endpoint | Method | Purpose |
|---|---|---|
| `/health` | GET | Health check |
| `/devices` | GET | Return configured device-room mappings |
| `/active-context` | GET | Return current active device and room |
| `/active-context` | POST | Manually set active context for initial HA MCP testing |
| `/active-context` | DELETE | Clear active context |
| `/wake-detected` | POST | ESP32 wake arbitration request |
| `/session/end` | POST | Clear or expire active session |

## Gateway Config

Create `C:\Code\xiaozhi-gateway\config\devices.yaml`:

```yaml
devices:
  living_room_xiaozhi:
    device_id: "aa:bb:cc:dd:ee:ff"
    client_id: ""
    room_id: "living_room"
    room_name: "客厅"
    ha_area_id: "living_room"
    ha_device_id: ""

  bedroom_xiaozhi:
    device_id: "11:22:33:44:55:66"
    client_id: ""
    room_id: "bedroom"
    room_name: "卧室"
    ha_area_id: "bedroom"
    ha_device_id: ""
```

## Task 1: Scaffold `xiaozhi-gateway`

**Files:**
- Create: `C:\Code\xiaozhi-gateway\pyproject.toml`
- Create: `C:\Code\xiaozhi-gateway\app\main.py`
- Create: `C:\Code\xiaozhi-gateway\app\config.py`
- Create: `C:\Code\xiaozhi-gateway\app\models.py`
- Create: `C:\Code\xiaozhi-gateway\config\devices.yaml`
- Create: `C:\Code\xiaozhi-gateway\tests\test_health.py`

**Steps:**
1. Create FastAPI app with `GET /health`.
2. Add pytest test for `/health`.
3. Run: `pytest`.
4. Run locally: `uvicorn app.main:app --host 0.0.0.0 --port 8125`.
5. Verify: `curl http://127.0.0.1:8125/health`.

## Task 2: Device Registry

**Files:**
- Modify: `C:\Code\xiaozhi-gateway\app\config.py`
- Modify: `C:\Code\xiaozhi-gateway\app\models.py`
- Modify: `C:\Code\xiaozhi-gateway\app\main.py`
- Create: `C:\Code\xiaozhi-gateway\tests\test_devices.py`

**Steps:**
1. Load `config/devices.yaml` on startup.
2. Validate duplicate `device_id`, duplicate `client_id`, and missing `room_id` as startup errors.
3. Implement `GET /devices`.
4. Run: `pytest tests/test_devices.py -v`.

## Task 3: Active Context Store

**Files:**
- Create: `C:\Code\xiaozhi-gateway\app\session_store.py`
- Modify: `C:\Code\xiaozhi-gateway\app\main.py`
- Create: `C:\Code\xiaozhi-gateway\tests\test_active_context.py`

**Steps:**
1. Implement in-memory active context.
2. Persist active context to `state.json`.
3. Implement `POST /active-context`, `GET /active-context`, `DELETE /active-context`.
4. Expire active context after a configurable TTL.
5. Run: `pytest tests/test_active_context.py -v`.

## Task 4: HA MCP Room Context Integration

**Files:**
- Modify: `C:\Code\ha-mcp-for-xiaozhi\custom_components\ws_mcp_server\config_flow.py`
- Modify: `C:\Code\ha-mcp-for-xiaozhi\custom_components\ws_mcp_server\websocket_transport.py`
- Modify: `C:\Code\ha-mcp-for-xiaozhi\custom_components\ws_mcp_server\server.py`

**Steps:**
1. Add config option for gateway URL, default `http://127.0.0.1:8125`.
2. Before HA tool execution, call `GET /active-context`.
3. Create HA `LLMContext` with `device_id` and room context when available.
4. If user explicitly passes a room/area argument, do not override it.
5. If active context is missing, fail clearly instead of guessing.
6. Manual test: set active room to living room, ask Xiaozhi to turn on the light, verify HA targets living room.

## Task 5: Wake Arbitration API

**Files:**
- Create: `C:\Code\xiaozhi-gateway\app\arbitration.py`
- Modify: `C:\Code\xiaozhi-gateway\app\main.py`
- Create: `C:\Code\xiaozhi-gateway\tests\test_arbitration.py`

**Steps:**
1. Implement `POST /wake-detected`.
2. Request body includes `device_id`, optional `client_id`, `wake_word`, and timestamp.
3. If no active session exists, return `allow_session` and set active context.
4. If another session is active, return `deny_session`.
5. If same device repeats within TTL, return `allow_session`.
6. Run: `pytest tests/test_arbitration.py -v`.

## Task 6: ESP32 Wake Arbitration Client

**Files:**
- Create: `C:\Code\xiaozhi-esp32\main\wake_arbiter_client.h`
- Create: `C:\Code\xiaozhi-esp32\main\wake_arbiter_client.cc`
- Modify: `C:\Code\xiaozhi-esp32\main\application.cc`
- Modify: `C:\Code\xiaozhi-esp32\main\application.h`
- Modify: `C:\Code\xiaozhi-esp32\main\CMakeLists.txt`

**Steps:**
1. Add client that POSTs to `http://gateway:8125/wake-detected`.
2. In `HandleWakeWordDetectedEvent()`, ask gateway before `ContinueWakeWordInvoke()`.
3. On `allow_session`, continue existing wake flow.
4. On `deny_session`, silently return to idle and re-enable wake word detection.
5. On network/error/timeout, do not enter session.
6. Build selected ESP32 target and verify no compile errors.

## Task 7: Docker Deployment

**Files:**
- Create: `C:\Code\xiaozhi-gateway\Dockerfile`
- Create: `C:\Code\xiaozhi-gateway\docker-compose.yml`
- Create: `C:\Code\xiaozhi-gateway\README.md`

**Steps:**
1. Expose container port `8125`.
2. Mount `config/devices.yaml`.
3. Mount `state.json` or state directory.
4. Run: `docker compose up -d`.
5. Verify: `curl http://NAS_IP:8125/health`.

## First Execution Recommendation

Do not start with ESP32 firmware. Execute in this order:

1. Task 1
2. Task 2
3. Task 3
4. Task 4
5. Task 5
6. Task 6
7. Task 7

This proves room context with manual active context before touching wake-word flow.

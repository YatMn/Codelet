# Codelet

English | [简体中文](README.zh-CN.md)

> A desktop attention panel for local Codex coding work, powered by a FastAPI Desktop Agent and an M5PaperS3 e-ink device.

## What It Is

Codelet answers one practical question:

```text
Which Codex projects need my attention right now, and how much quota do I have left?
```

Codelet is for solo developers running multiple local Codex threads or sessions at the same time. It turns blockers, failures, user-waiting states, long-running work, and quota risk into a glanceable desktop signal without exposing full prompts, replies, diffs, secrets, or local paths on the device.

## Architecture

```text
Codex local data
  -> Desktop Agent adapters
  -> State reducer + privacy filter + project aggregation
  -> FastAPI snapshot API
  -> M5PaperS3 HTTP polling
  -> e-ink-safe rendering + touch navigation + buzzer
```

Core boundary:

- The Desktop Agent reads and normalizes local Codex data.
- The Desktop Agent owns status inference, quota shaping, project aggregation, and privacy filtering.
- M5PaperS3 only polls `/api/v1/snapshot`, renders the already-filtered snapshot, and handles touch/navigation/buzzer behavior.
- `awaiting_user` is a first-class blocked state, not `completed`.

## Components

| Component | Role |
| --- | --- |
| Desktop Agent | Reads local Codex state, applies privacy filtering, aggregates projects and threads, and exposes the snapshot API. |
| PaperS3 Firmware | Polls the Desktop Agent, renders the e-ink interface, handles touch navigation, and controls buzzer behavior. |

## Quick Start

Install dependencies:

```bash
uv sync --dev
```

Start the Desktop Agent on localhost:

```bash
uv run uvicorn codelet_agent.main:app --host 127.0.0.1 --port 8765
```

Inspect the snapshot:

```bash
curl -s http://127.0.0.1:8765/api/v1/snapshot | python -m json.tool | head -80
```

For a real M5PaperS3 on the same LAN, start the agent on all interfaces:

```bash
uv run uvicorn codelet_agent.main:app --host 0.0.0.0 --port 8765
```

For local desktop development, `127.0.0.1` is enough. For the physical device, expose the agent on `0.0.0.0` and configure the device with the computer's LAN IP.

## API

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v1/health` | Health check |
| `GET` | `/api/v1/snapshot` | Full device-facing state snapshot |
| `POST` | `/api/v1/settings/sound` | Enable or disable global buzzer behavior |
| `POST` | `/api/v1/mute` | Set or clear `mute_until` |

The snapshot contains:

- `server`: health, version, privacy mode, sound setting, mute state, diagnostic `codex_home`
- `quota`: 5-hour and weekly bucket model where available
- `summary`: compact global counts
- `projects`: attention-sorted project cards
- `threads`: attention-sorted visible thread list
- `alerts`: device-facing alert records

`server.codex_home` is the explicit diagnostic exception and may expose the local Codex home. Other M5PaperS3-facing project/thread display fields should stay short and filtered.

## Status Priority

```text
approval_required
> failed
> awaiting_user
> stale
> long_running
> tool_running
> running
> thinking
> completed
> idle
> cancelled
> unknown
```

The e-ink UI uses shape and line treatment instead of color. Blocked or exceptional states receive stronger grayscale texture and borders; ordinary running or completed states stay quiet.

## PaperS3 Firmware

Firmware lives in `firmware/` and uses PlatformIO:

```bash
cd firmware
pio run -e PaperS3
pio run -e PaperS3 -t upload
pio device monitor -e PaperS3
```

Runtime setup is the normal device configuration path:

1. On first boot, connect to Wi-Fi AP `Codelet-Setup`.
2. Open `http://192.168.4.1`.
3. Enter Wi-Fi credentials, Desktop Agent IP, port `8765`, and optional API token.
4. The device exits Setup Mode only after it can reach the Desktop Agent.

`firmware/include/codelet_secrets.h` is local-only and ignored by git. Shareable placeholders belong in `firmware/include/codelet_secrets.example.h`.

## Privacy

Codelet is intentionally a filtered monitor, not a full Codex transcript viewer. Device-facing fields must not expose:

- complete local paths
- full prompts or full replies
- diffs
- secrets
- API keys
- private credentials

Device-facing fields should keep only short titles, project aliases, status, time, short events, and short error summaries.

---

Codelet is small by design: the Desktop Agent understands Codex state, and the PaperS3 keeps only the attention signal visible.

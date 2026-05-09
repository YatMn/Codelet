# Codelet

English | [简体中文](README.zh-CN.md)

Codelet is a local Codex attention panel: a Python/FastAPI Desktop Agent reads local Codex state, and an M5PaperS3 e-ink device shows which projects need attention.

## Status

Codelet currently contains two usable parts:

- **Desktop Agent**: FastAPI service that reads local Codex session data, normalizes thread state, applies privacy filtering, aggregates projects, models quota buckets, and exposes a device-facing snapshot API.
- **PaperS3 Firmware**: PlatformIO/Arduino firmware for M5PaperS3 that configures Wi-Fi and the agent endpoint at runtime, polls the snapshot API, renders the e-ink UI, handles touch navigation, and applies the global buzzer setting.

The MVP is intentionally local-first. It does not include OTA updates, a web dashboard, mobile notifications, multi-device sync, task cancellation, Focus Mode, read/unread tracking, or away/presence detection.

## How It Works

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

## Quick Start

Prerequisites:

- Python 3.12+
- `uv`

Install dependencies:

```bash
uv sync --dev
```

Start the Desktop Agent for a physical M5PaperS3:

```bash
uv run uvicorn codelet_agent.main:app --host 0.0.0.0 --port 8765
```

Find the computer's LAN IP and configure the device with that IP and port `8765`:

```bash
ipconfig getifaddr en0
```

Check the API from the desktop:

```bash
curl -s http://127.0.0.1:8765/api/v1/health
curl -s http://127.0.0.1:8765/api/v1/snapshot | python -m json.tool | head -80
```

Also check the LAN address before trusting a device smoke test:

```bash
curl -s http://<desktop-lan-ip>:8765/api/v1/health
```

For a persistent local run that survives the current shell:

```bash
launchctl remove codelet-agent-8765 >/dev/null 2>&1 || true
launchctl submit -l codelet-agent-8765 -- /bin/zsh -lc 'cd /Users/yatmn/Projects/Codelet && /opt/homebrew/bin/uv run uvicorn codelet_agent.main:app --host 0.0.0.0 --port 8765 >/tmp/codelet-agent-8765.log 2>&1'
lsof -nP -iTCP:8765 -sTCP:LISTEN
```

For localhost-only desktop debugging, `127.0.0.1` is enough:

```bash
uv run uvicorn codelet_agent.main:app --host 127.0.0.1 --port 8765
```

Do not configure the physical device with `127.0.0.1`; on the device that means the device itself, not the desktop running the agent. If the device shows `offline`, first verify that the agent is listening on `*:8765` or `0.0.0.0:8765`, then verify `http://<desktop-lan-ip>:8765/api/v1/health` from the same network.

## API

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v1/health` | Health check |
| `GET` | `/api/v1/snapshot` | Full device-facing state snapshot |
| `POST` | `/api/v1/settings/sound` | Enable or disable global buzzer behavior |
| `POST` | `/api/v1/mute` | Set or clear `mute_until` |

The snapshot contains:

- `server`: health, version, privacy mode, sound setting, mute state, and diagnostic `codex_home`
- `quota`: 5-hour and weekly quota buckets where available
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

The e-ink UI uses shape, texture, and line treatment instead of color. Blocked or exceptional states receive stronger grayscale texture and borders; ordinary running or completed states stay quiet.

## PaperS3 Firmware

Prerequisites:

- PlatformIO
- M5PaperS3 device for upload and manual smoke checks

Firmware commands:

```bash
cd firmware
pio test -e native
pio run -e PaperS3
pio run -e PaperS3 -t upload
pio device monitor -e PaperS3
```

Runtime setup is the normal device configuration path:

1. On first boot, connect to Wi-Fi AP `Codelet-Setup` with password `codelet-setup`.
2. Open `http://192.168.4.1`.
3. Enter Wi-Fi credentials, Desktop Agent IP, port `8765`, and optional API token.
4. The device exits Setup Mode only after it can reach the Desktop Agent.

To reconfigure an already configured device, reboot it and hold the top-left screen area for 3 seconds during startup.

`firmware/include/codelet_secrets.h` is local-only and ignored by git. It is a development fallback, not the normal daily configuration path. Shareable placeholders belong in `firmware/include/codelet_secrets.example.h`.

## Privacy

Codelet is a filtered monitor, not a full Codex transcript viewer. Device-facing fields must not expose:

- complete local paths
- full prompts or full replies
- diffs
- secrets
- API keys
- private credentials

Device-facing fields should keep only short titles, project aliases, status, time, short events, and short error summaries.

## Project Structure

| Path | Purpose |
| --- | --- |
| `src/codelet_agent/` | Desktop Agent package, API, settings, adapters, reducer, privacy filtering, and snapshot builder |
| `tests/` | Pytest coverage for the Desktop Agent, fixture adapter, real local Codex adapter, API, privacy, reducer, and models |
| `firmware/include/` | Firmware headers, runtime config, layout constants, setup flow, client, renderer, and local secrets example |
| `firmware/src/` | M5PaperS3 firmware implementation |
| `firmware/test/` | PlatformIO native tests |
| `docs/superpowers/specs/` | Product and firmware design specs |
| `docs/superpowers/plans/` | Executable implementation plans |

## Development

Run Python tests:

```bash
uv run pytest -q
```

Run firmware native tests:

```bash
cd firmware
pio test -e native
```

Build firmware without uploading:

```bash
cd firmware
pio run -e PaperS3
```

Do not report firmware upload, serial smoke, or visual correctness as verified unless the PlatformIO upload and physical device checks actually ran.

## Documentation

- Product/design source of truth: `docs/superpowers/specs/2026-04-29-codelet-design.md`
- Desktop Agent MVP plan: `docs/superpowers/plans/2026-04-29-codelet-agent-mvp-implementation.md`
- Firmware MVP plan: `docs/superpowers/plans/2026-04-29-codelet-firmware-mvp-implementation.md`
- Device Setup Mode design: `docs/superpowers/specs/2026-04-30-codelet-device-setup-design.md`
- Firmware-specific setup and smoke checks: `firmware/README.md`

## License

No license file is present in this repository.

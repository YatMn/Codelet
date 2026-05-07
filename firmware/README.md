# Codelet Firmware

M5PaperS3 firmware for Codelet Phase 2.

## Configure

Runtime setup is the normal configuration path.

On first boot, or when the device has no saved runtime config, M5PaperS3 enters Setup Mode:

```text
Wi-Fi: Codelet-Setup
Password: codelet-setup
Open: http://192.168.4.1
```

Connect a phone or computer to `Codelet-Setup`, open `http://192.168.4.1`, then enter:

- Wi-Fi SSID
- Wi-Fi password
- Agent IP, for example `192.168.1.23`
- Agent port, default `8765`
- API token, optional

The device saves the config to ESP32 NVS, keeps Setup Mode open while testing the connection, and exits Setup only after it can reach the Desktop Agent from the configured Wi-Fi.

To reconfigure an already configured device, reboot and hold the top-left screen area for 3 seconds during startup.

`include/codelet_secrets.h` is local-only and ignored by git. It is a development fallback, not the normal daily configuration path.

## Commands

```bash
pio test -e native
pio run -e PaperS3
pio run -e PaperS3 -t upload
pio device monitor -e PaperS3
```

## Manual Smoke

1. Start the Desktop Agent:

```bash
uv run uvicorn codelet_agent.main:app --host 0.0.0.0 --port 8765
```

2. Verify from another terminal:

```bash
curl -s http://127.0.0.1:8765/api/v1/snapshot | python -m json.tool | head -80
```

3. Upload firmware and open monitor:

```bash
cd firmware
pio run -e PaperS3 -t upload
pio device monitor -e PaperS3
```

4. First-run setup smoke for a fresh or no-NVS device:

- Device shows Setup Mode instead of Home.
- Connect a phone or computer to `Codelet-Setup`.
- Open `http://192.168.4.1`.
- Submit Wi-Fi SSID/password, Desktop Agent IP, Agent port `8765`, and optional API token.
- Device remains in Setup Mode while testing Wi-Fi and Agent reachability.
- After the test succeeds, device exits Setup Mode and starts normal snapshot polling.

Only report this setup smoke as verified if the upload and physical device check actually ran.

5. Normal display smoke after Setup Mode exits, or on a reboot with saved NVS config:

- Home shows up to 6 project cards.
- Top-right quota module is visible.
- Footer tap cycles Home and Threads.
- Project card tap opens Project Detail.
- `approval_required`, `awaiting_user`, and `failed` use hatched backgrounds.
- `failed` uses stronger border.
- `stale` uses dashed border and no hatched background.
- `server.sound_enabled=false` suppresses buzzer while visual attention remains.
- Turning off the Agent shows offline/data stale state on the device within one stale window.

Only report normal display smoke as verified if the device reached the normal UI and the listed checks actually ran.

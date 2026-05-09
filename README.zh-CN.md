# Codelet

[English](README.md) | 简体中文

Codelet 是一个本机 Codex 注意力面板：Python/FastAPI Desktop Agent 读取本机 Codex 状态，M5PaperS3 电子墨水屏显示哪些项目需要处理。

## 当前状态

Codelet 目前包含两个可用部分：

- **Desktop Agent**：FastAPI 服务，读取本机 Codex session 数据，标准化 thread 状态，做隐私过滤，聚合项目，建模 quota bucket，并提供面向设备的 snapshot API。
- **PaperS3 Firmware**：面向 M5PaperS3 的 PlatformIO/Arduino 固件，可在运行时配置 Wi-Fi 和 Agent endpoint，轮询 snapshot API，渲染电子墨水屏 UI，处理触摸导航，并应用全局蜂鸣设置。

当前 MVP 明确保持 local-first。它不包含 OTA、Web Dashboard、移动端通知、多设备同步、任务取消、Focus Mode、read/unread、away/presence detection。

## 工作方式

```text
Codex local data
  -> Desktop Agent adapters
  -> State reducer + privacy filter + project aggregation
  -> FastAPI snapshot API
  -> M5PaperS3 HTTP polling
  -> e-ink-safe rendering + touch navigation + buzzer
```

核心边界：

- Desktop Agent 负责读取并标准化本机 Codex 数据。
- Desktop Agent 负责状态推断、quota 建模、项目聚合和隐私过滤。
- M5PaperS3 只轮询 `/api/v1/snapshot`，展示已过滤的数据，并处理触摸、导航和蜂鸣。
- `awaiting_user` 是一等阻塞状态，不能当作 `completed`。

## 快速启动

前置条件：

- Python 3.12+
- `uv`

安装依赖：

```bash
uv sync --dev
```

为 M5PaperS3 真机启动 Desktop Agent：

```bash
uv run uvicorn codelet_agent.main:app --host 0.0.0.0 --port 8765
```

查看电脑的 LAN IP，并在设备设置里填写这个 IP 和端口 `8765`：

```bash
ipconfig getifaddr en0
```

从电脑上检查 API：

```bash
curl -s http://127.0.0.1:8765/api/v1/health
curl -s http://127.0.0.1:8765/api/v1/snapshot | python -m json.tool | head -80
```

真机 smoke 前也要检查 LAN 地址：

```bash
curl -s http://<desktop-lan-ip>:8765/api/v1/health
```

如果需要服务在当前 shell 退出后继续运行，可以用 user-level `launchctl`：

```bash
launchctl remove codelet-agent-8765 >/dev/null 2>&1 || true
launchctl submit -l codelet-agent-8765 -- /bin/zsh -lc 'cd /Users/yatmn/Projects/Codelet && /opt/homebrew/bin/uv run uvicorn codelet_agent.main:app --host 0.0.0.0 --port 8765 >/tmp/codelet-agent-8765.log 2>&1'
lsof -nP -iTCP:8765 -sTCP:LISTEN
```

只做本机桌面调试时，`127.0.0.1` 就够了：

```bash
uv run uvicorn codelet_agent.main:app --host 127.0.0.1 --port 8765
```

不要把真机的 Agent IP 配成 `127.0.0.1`；在设备上它代表设备自己，不是运行 Agent 的电脑。如果设备显示 `offline`，先确认 Agent 监听在 `*:8765` 或 `0.0.0.0:8765`，再从同一网络验证 `http://<desktop-lan-ip>:8765/api/v1/health`。

## 接口

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| `GET` | `/api/v1/health` | 健康检查 |
| `GET` | `/api/v1/snapshot` | 面向设备的完整状态快照 |
| `POST` | `/api/v1/settings/sound` | 开关全局蜂鸣 |
| `POST` | `/api/v1/mute` | 设置或清除 `mute_until` |

Snapshot 包含：

- `server`：健康状态、版本、隐私模式、蜂鸣设置、静音状态和诊断用 `codex_home`
- `quota`：可用时包含 5 小时和周额度 bucket
- `summary`：全局紧凑计数
- `projects`：按 attention priority 排序的项目卡片
- `threads`：按 attention priority 排序的可见 thread 列表
- `alerts`：面向设备的提醒记录

`server.codex_home` 是明确允许暴露的诊断例外；除此之外，面向 M5PaperS3 的 project/thread 展示字段都应保持短小并经过过滤。

## 状态优先级

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

电子墨水屏使用形状、纹理和线条处理，而不是依赖颜色。阻塞或异常状态使用更强的灰度纹理和边框；普通运行和完成状态保持低干扰。

## PaperS3 固件

前置条件：

- PlatformIO
- 用于上传和手动 smoke check 的 M5PaperS3 设备

固件命令：

```bash
cd firmware
pio test -e native
pio run -e PaperS3
pio run -e PaperS3 -t upload
pio device monitor -e PaperS3
```

运行时设置是默认设备配置方式：

1. 首次启动时连接 Wi-Fi AP `Codelet-Setup`，密码为 `codelet-setup`。
2. 打开 `http://192.168.4.1`。
3. 填写 Wi-Fi、Desktop Agent IP、端口 `8765` 和可选 API token。
4. 设备只有在能连通 Desktop Agent 后才退出 Setup Mode。

如需重新配置已经配置过的设备，重启后在启动期间按住屏幕左上角 3 秒。

`firmware/include/codelet_secrets.h` 是本机私有文件且被 git 忽略。它是开发 fallback，不是日常配置方式；可共享占位配置写在 `firmware/include/codelet_secrets.example.h`。

## 隐私边界

Codelet 是经过过滤的状态面板，不是完整 Codex transcript viewer。设备侧字段必须避免暴露：

- 完整本地路径
- 完整 prompt 或回复
- diff
- secret
- API key
- 私密凭据

面向设备的字段只应保留短标题、项目别名、状态、时间、简短事件和简短错误摘要。

## 项目结构

| 路径 | 用途 |
| --- | --- |
| `src/codelet_agent/` | Desktop Agent package、API、settings、adapter、reducer、隐私过滤和 snapshot builder |
| `tests/` | Desktop Agent、fixture adapter、真实本机 Codex adapter、API、privacy、reducer 和 model 的 pytest 覆盖 |
| `firmware/include/` | 固件 header、runtime config、layout 常量、setup flow、client、renderer 和本机 secrets example |
| `firmware/src/` | M5PaperS3 固件实现 |
| `firmware/test/` | PlatformIO native tests |
| `docs/superpowers/specs/` | 产品和固件设计 spec |
| `docs/superpowers/plans/` | 可执行 implementation plan |

## 开发

运行 Python 测试：

```bash
uv run pytest -q
```

运行固件 native tests：

```bash
cd firmware
pio test -e native
```

只构建固件，不上传：

```bash
cd firmware
pio run -e PaperS3
```

不要把 firmware upload、serial smoke 或视觉正确性报告为已验证，除非实际运行了 PlatformIO upload 和物理设备检查。

## 文档

- 产品/设计 source of truth：`docs/superpowers/specs/2026-04-29-codelet-design.md`
- Desktop Agent MVP plan：`docs/superpowers/plans/2026-04-29-codelet-agent-mvp-implementation.md`
- Firmware MVP plan：`docs/superpowers/plans/2026-04-29-codelet-firmware-mvp-implementation.md`
- Device Setup Mode design：`docs/superpowers/specs/2026-04-30-codelet-device-setup-design.md`
- 固件专用 setup 和 smoke check：`firmware/README.md`

## License

仓库中没有 license 文件。

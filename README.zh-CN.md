# Codelet

[English](README.md) | 简体中文

> 一个面向本机 Codex 多项目开发的桌面注意力面板：由 Python/FastAPI Desktop Agent 聚合本机状态，由 M5PaperS3 电子墨水屏显示需要处理的项目、线程和 quota 风险。

## 项目定位

Codelet 只回答一个实际问题：

```text
现在有哪些 Codex 项目需要我处理？我的 quota 还剩多少？
```

Codelet 面向单人同时运行多个 Codex thread/session 的场景。它不会把完整 prompt、回复、diff、secret 或本地路径搬到小屏上，而是把阻塞、失败、等待用户回复、长时间运行和 quota 风险压缩成一个可扫视的桌面面板。

## 架构

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

## 组成部分

| 组件 | 作用 |
| --- | --- |
| Desktop Agent | 读取本机 Codex 状态，做隐私过滤，聚合项目和 thread，并提供 snapshot API。 |
| PaperS3 Firmware | 轮询 Desktop Agent，渲染电子墨水屏界面，处理触摸导航和蜂鸣行为。 |

## 快速启动

安装依赖：

```bash
uv sync --dev
```

本机启动 Desktop Agent：

```bash
uv run uvicorn codelet_agent.main:app --host 127.0.0.1 --port 8765
```

查看 snapshot：

```bash
curl -s http://127.0.0.1:8765/api/v1/snapshot | python -m json.tool | head -80
```

真机局域网访问时，Desktop Agent 需要监听所有网卡：

```bash
uv run uvicorn codelet_agent.main:app --host 0.0.0.0 --port 8765
```

本机开发时使用 `127.0.0.1` 即可；真机需要从局域网访问 Desktop Agent，因此要使用 `0.0.0.0` 启动，并在设备设置里填写电脑的 LAN IP。

## 接口

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| `GET` | `/api/v1/health` | 健康检查 |
| `GET` | `/api/v1/snapshot` | 面向设备的完整状态快照 |
| `POST` | `/api/v1/settings/sound` | 开关全局蜂鸣 |
| `POST` | `/api/v1/mute` | 设置或清除 `mute_until` |

Snapshot 包含：

- `server`：健康状态、版本、隐私模式、蜂鸣设置、静音状态、诊断用 `codex_home`
- `quota`：5 小时和周额度 bucket model
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

电子墨水屏不依赖颜色或动画。阻塞/异常状态使用更强的灰度纹理和边框；普通运行和完成状态保持低干扰。

## PaperS3 固件

固件位于 `firmware/`，使用 PlatformIO：

```bash
cd firmware
pio run -e PaperS3
pio run -e PaperS3 -t upload
pio device monitor -e PaperS3
```

运行时设置是默认设备配置方式：

1. 首次启动时连接 Wi-Fi AP `Codelet-Setup`。
2. 打开 `http://192.168.4.1`。
3. 填写 Wi-Fi、Desktop Agent IP、端口 `8765` 和可选 API token。
4. 设备只有在能连通 Desktop Agent 后才退出 Setup Mode。

`firmware/include/codelet_secrets.h` 是本机私有文件且被 git 忽略；可共享占位配置写在 `firmware/include/codelet_secrets.example.h`。

## 隐私边界

Codelet 是状态面板，不是完整会话查看器。设备侧字段必须避免暴露：

- 完整本地路径
- 完整 prompt 或回复
- diff
- secret
- API key
- 私密凭据

面向设备的字段只应保留短标题、项目别名、状态、时间、简短事件和简短错误摘要。

---

Codelet 的目标很克制：让 Desktop Agent 理解 Codex 状态，让 PaperS3 只显示真正需要注意的信号。

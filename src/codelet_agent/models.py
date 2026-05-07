from __future__ import annotations

from datetime import datetime
from enum import StrEnum
from typing import Any

from pydantic import BaseModel

JsonDict = dict[str, Any]


class Status(StrEnum):
    APPROVAL_REQUIRED = "approval_required"
    FAILED = "failed"
    AWAITING_USER = "awaiting_user"
    STALE = "stale"
    LONG_RUNNING = "long_running"
    TOOL_RUNNING = "tool_running"
    RUNNING = "running"
    THINKING = "thinking"
    COMPLETED = "completed"
    IDLE = "idle"
    CANCELLED = "cancelled"
    UNKNOWN = "unknown"


_STATUS_PRIORITY: dict[Status, int] = {
    Status.APPROVAL_REQUIRED: 110,
    Status.FAILED: 100,
    Status.AWAITING_USER: 90,
    Status.STALE: 80,
    Status.LONG_RUNNING: 70,
    Status.TOOL_RUNNING: 60,
    Status.RUNNING: 50,
    Status.THINKING: 40,
    Status.COMPLETED: 30,
    Status.IDLE: 20,
    Status.CANCELLED: 10,
    Status.UNKNOWN: 0,
}


def priority_for_status(status: Status) -> int:
    return _STATUS_PRIORITY[status]


class ServerSnapshot(BaseModel):
    status: str
    now: datetime
    snapshot_version: int
    privacy_mode: bool
    sound_enabled: bool
    mute_until: datetime | None
    codex_home: str


class QuotaBucket(BaseModel):
    kind: str
    label: str
    used_percent: int
    remaining_percent: int
    resets_at: datetime | None
    reset_in_sec: int | None
    status: str


class QuotaSnapshot(BaseModel):
    primary_constraint: str | None
    buckets: list[QuotaBucket]


class ThreadSnapshot(BaseModel):
    id: str
    project_id: str
    source: str
    title: str
    status: Status
    attention_priority: int
    started_at: datetime | None
    updated_at: datetime
    completed_at: datetime | None
    duration_sec: int | None
    last_event: str | None
    approval_required: bool
    error_summary: str | None
    display_version: int


class ProjectSnapshot(BaseModel):
    id: str
    alias: str
    path_hint: str
    state: Status
    attention_priority: int
    thread_count: int
    counts: dict[str, int]
    recent_threads: list[ThreadSnapshot]
    display_version: int


class AlertSnapshot(BaseModel):
    id: str
    level: str
    message: str
    created_at: datetime
    acknowledged: bool = False


class Snapshot(BaseModel):
    server: ServerSnapshot
    quota: QuotaSnapshot
    summary: JsonDict
    projects: list[ProjectSnapshot]
    threads: list[ThreadSnapshot]
    alerts: list[AlertSnapshot]

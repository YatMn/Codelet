from __future__ import annotations

from datetime import datetime, timezone
from typing import Protocol

from codelet_agent.models import (
    QuotaBucket,
    QuotaSnapshot,
    ServerSnapshot,
    Snapshot,
    ThreadSnapshot,
)
from codelet_agent.reducer import aggregate_projects, build_summary, sort_threads

SNAPSHOT_VERSION = 1


class ThreadAdapter(Protocol):
    def load_threads(self) -> list[ThreadSnapshot]: ...


def build_snapshot(
    adapter: ThreadAdapter,
    sound_enabled: bool,
    privacy_mode: bool,
    mute_until: datetime | None = None,
) -> Snapshot:
    now = datetime.now(timezone.utc)
    threads = sort_threads(adapter.load_threads())
    projects = aggregate_projects(threads)

    load_quota = getattr(adapter, "load_quota", None)
    quota = load_quota(now) if callable(load_quota) else _build_quota(now)

    return Snapshot(
        server=ServerSnapshot(
            status="ok",
            now=now,
            snapshot_version=SNAPSHOT_VERSION,
            privacy_mode=privacy_mode,
            sound_enabled=sound_enabled,
            mute_until=mute_until,
            codex_home=str(getattr(adapter, "codex_home", "")),
        ),
        quota=quota,
        summary=build_summary(projects, threads),
        projects=projects,
        threads=threads,
        alerts=[],
    )


def _build_quota(now: datetime) -> QuotaSnapshot:
    return QuotaSnapshot(
        primary_constraint="five_hour",
        buckets=[
            QuotaBucket(
                kind="five_hour",
                label="5h",
                used_percent=0,
                remaining_percent=0,
                resets_at=now,
                reset_in_sec=None,
                status="unknown",
            ),
            QuotaBucket(
                kind="weekly",
                label="Week",
                used_percent=0,
                remaining_percent=0,
                resets_at=now,
                reset_in_sec=None,
                status="unknown",
            ),
        ],
    )

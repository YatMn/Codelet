from __future__ import annotations

from datetime import datetime, timezone

from codelet_agent.models import Status, ThreadSnapshot, priority_for_status

FIXTURE_SOURCE = "fixture"
DISPLAY_VERSION = 1


class FixtureAdapter:
    def load_threads(self) -> list[ThreadSnapshot]:
        now = datetime(2026, 4, 29, 12, 0, tzinfo=timezone.utc)
        return [
            _thread(
                thread_id="fixture:approval",
                project_id="project:Codelet:fixture",
                title="Review snapshot API",
                status=Status.APPROVAL_REQUIRED,
                updated_at=now,
                last_event="approval required",
            ),
            _thread(
                thread_id="fixture:running",
                project_id="project:Codelet:fixture",
                title="Build reducer tests",
                status=Status.RUNNING,
                updated_at=now.replace(hour=11),
                last_event="tests running",
            ),
            _thread(
                thread_id="fixture:idle",
                project_id="project:Arcly:fixture",
                title="No active work",
                status=Status.IDLE,
                updated_at=now.replace(hour=10),
                last_event=None,
            ),
        ]


def _thread(
    thread_id: str,
    project_id: str,
    title: str,
    status: Status,
    updated_at: datetime,
    last_event: str | None,
) -> ThreadSnapshot:
    return ThreadSnapshot(
        id=thread_id,
        project_id=project_id,
        source=FIXTURE_SOURCE,
        title=title,
        status=status,
        attention_priority=priority_for_status(status),
        started_at=None,
        updated_at=updated_at,
        completed_at=None,
        duration_sec=None,
        last_event=last_event,
        approval_required=status == Status.APPROVAL_REQUIRED,
        error_summary=last_event if status == Status.FAILED else None,
        display_version=DISPLAY_VERSION,
    )

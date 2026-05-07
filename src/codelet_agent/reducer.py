from __future__ import annotations

from datetime import datetime, timezone

from codelet_agent.models import ProjectSnapshot, Status, ThreadSnapshot, priority_for_status

DISPLAY_VERSION = 1
RECENT_THREAD_LIMIT = 2
NEED_ATTENTION_STATUSES = {
    Status.APPROVAL_REQUIRED,
    Status.FAILED,
    Status.AWAITING_USER,
    Status.STALE,
}


def with_attention_priority(
    thread: ThreadSnapshot, latest_non_failed_by_project: dict[str, float] | None = None
) -> ThreadSnapshot:
    priority = _effective_attention_priority(thread, latest_non_failed_by_project or {})
    return thread.model_copy(update={"attention_priority": priority})


def sort_threads(threads: list[ThreadSnapshot]) -> list[ThreadSnapshot]:
    latest_non_failed_by_project = _latest_non_failed_by_project(threads)
    prioritized = [
        with_attention_priority(thread, latest_non_failed_by_project)
        for thread in threads
    ]
    return sorted(
        prioritized,
        key=lambda thread: (
            -thread.attention_priority,
            -_datetime_sort_value(thread.updated_at),
            thread.id,
        ),
    )


def aggregate_projects(threads: list[ThreadSnapshot]) -> list[ProjectSnapshot]:
    grouped: dict[str, list[ThreadSnapshot]] = {}
    for thread in sort_threads(threads):
        grouped.setdefault(thread.project_id, []).append(thread)

    projects = [
        _build_project(project_id=project_id, threads=project_threads)
        for project_id, project_threads in grouped.items()
    ]
    return sorted(
        projects,
        key=lambda project: (
            -project.attention_priority,
            -_project_latest_updated_at(project),
            project.id,
        ),
    )


def build_summary(
    projects: list[ProjectSnapshot], threads: list[ThreadSnapshot]
) -> dict[str, int]:
    sorted_threads = sort_threads(threads)
    return {
        "project_count": len(projects),
        "thread_count": len(threads),
        "need_attention_count": sum(
            1 for thread in sorted_threads if _needs_attention(thread)
        ),
    }


def _build_project(project_id: str, threads: list[ThreadSnapshot]) -> ProjectSnapshot:
    if not threads:
        raise ValueError("project aggregation requires at least one thread")

    counts: dict[str, int] = {}
    for thread in threads:
        counts[thread.status.value] = counts.get(thread.status.value, 0) + 1

    attention_threads = _project_attention_threads(threads)
    top_thread = attention_threads[0]
    alias = _safe_project_alias(project_id)
    return ProjectSnapshot(
        id=project_id,
        alias=alias,
        path_hint=alias,
        state=top_thread.status,
        attention_priority=top_thread.attention_priority,
        thread_count=len(threads),
        counts=counts,
        recent_threads=attention_threads[:RECENT_THREAD_LIMIT],
        display_version=DISPLAY_VERSION,
    )


def _project_attention_threads(threads: list[ThreadSnapshot]) -> list[ThreadSnapshot]:
    latest_non_failed = max(
        (
            _datetime_sort_value(thread.updated_at)
            for thread in threads
            if thread.status != Status.FAILED
        ),
        default=None,
    )
    if latest_non_failed is None:
        return threads

    current_threads = [
        thread
        for thread in threads
        if thread.status != Status.FAILED
        or _datetime_sort_value(thread.updated_at) >= latest_non_failed
    ]
    return current_threads or threads


def _latest_non_failed_by_project(threads: list[ThreadSnapshot]) -> dict[str, float]:
    latest: dict[str, float] = {}
    for thread in threads:
        if thread.status == Status.FAILED:
            continue
        timestamp = _datetime_sort_value(thread.updated_at)
        latest[thread.project_id] = max(latest.get(thread.project_id, timestamp), timestamp)
    return latest


def _effective_attention_priority(
    thread: ThreadSnapshot, latest_non_failed_by_project: dict[str, float]
) -> int:
    if thread.status != Status.FAILED:
        return priority_for_status(thread.status)

    latest_non_failed = latest_non_failed_by_project.get(thread.project_id)
    if latest_non_failed is None:
        return priority_for_status(Status.FAILED)
    if _datetime_sort_value(thread.updated_at) >= latest_non_failed:
        return priority_for_status(Status.FAILED)
    return priority_for_status(Status.COMPLETED)


def _needs_attention(thread: ThreadSnapshot) -> bool:
    if thread.status == Status.FAILED:
        return thread.attention_priority == priority_for_status(Status.FAILED)
    return thread.status in NEED_ATTENTION_STATUSES


def _safe_project_alias(project_id: str) -> str:
    parts = project_id.split(":")
    if len(parts) >= 3 and parts[0] == "project" and parts[1]:
        candidate = parts[1]
    else:
        candidate = ""

    if candidate.lower() == "unknown":
        return "Unknown"
    if not candidate or "/" in candidate or "\\" in candidate:
        return "Unknown"
    return candidate


def _project_latest_updated_at(project: ProjectSnapshot) -> float:
    if not project.recent_threads:
        return 0
    return max(_datetime_sort_value(thread.updated_at) for thread in project.recent_threads)


def _datetime_sort_value(value: datetime) -> float:
    if value.tzinfo is None:
        value = value.replace(tzinfo=timezone.utc)
    return value.astimezone(timezone.utc).timestamp()

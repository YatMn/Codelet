from __future__ import annotations

import hashlib
import json
import re
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

from codelet_agent.models import JsonDict, QuotaBucket, QuotaSnapshot, Status, ThreadSnapshot, priority_for_status
from codelet_agent.privacy import sanitize_text

CODEX_SOURCE = "codex_desktop"
DISPLAY_VERSION = 1
TEXT_PREVIEW_CHARS = 240
TOOL_OUTPUT_PREVIEW_CHARS = 500
DEFAULT_QUOTA_CACHE_TTL_SEC = 60
STALE_AFTER_SEC = 30 * 60
LONG_RUNNING_AFTER_SEC = 60 * 60
APPROVAL_REQUIRED_EVENT_TYPES = {"approval_required", "approval_request"}
APPROVAL_REQUIRED_MARKERS = (
    "approval required",
    "requires approval",
    "need your approval",
    "needs approval",
    "需要批准",
    "需要你批准",
)
AWAITING_USER_MARKERS = ("确认", "reply", "need your reply", "needs reply", "等待", "继续吗")
FAILED_MARKERS = ("failed", "error", "traceback")
EXIT_CODE_RE = re.compile(r"exit(?:ed)?(?:_code| with code)?[:= ]+(-?\d+)", re.IGNORECASE)
PROJECT_NAME_RE = re.compile(r"[^A-Za-z0-9._-]+")


@dataclass(frozen=True)
class _IndexEntry:
    session_id: str
    title: str
    updated_at: datetime | None
    order: int


@dataclass(frozen=True)
class _SessionFacts:
    cwd: str | None
    started_at: datetime | None
    updated_at: datetime | None
    last_event: str | None
    status: Status


class CodexLocalAdapter:
    def __init__(
        self,
        codex_home: Path | None = None,
        *,
        quota_cache_ttl_sec: int = DEFAULT_QUOTA_CACHE_TTL_SEC,
        monotonic: Callable[[], float] = time.monotonic,
    ) -> None:
        self.codex_home = codex_home or Path.home() / ".codex"
        self.quota_cache_ttl_sec = max(0, quota_cache_ttl_sec)
        self._monotonic = monotonic
        self._quota_rate_limit_cache: tuple[float, JsonDict | None] | None = None

    def load_threads(self, *, limit: int = 20, now: datetime | None = None) -> list[ThreadSnapshot]:
        if limit <= 0:
            return []

        current_time = _normalize_datetime(now) or datetime.now(timezone.utc)
        entries = _read_index(self.codex_home / "session_index.jsonl", limit=None)
        candidates = _session_candidates(self.codex_home)
        threads: list[ThreadSnapshot] = []

        for entry in entries:
            session_path = _find_session_file(entry.session_id, candidates)
            if session_path and _is_subagent_session(session_path):
                continue
            facts = _read_session_facts(session_path) if session_path else None
            threads.append(_build_thread(entry, facts, current_time))
            if len(threads) >= limit:
                break

        return threads

    def load_quota(self, now: datetime) -> QuotaSnapshot:
        latest_rate_limit = self._cached_latest_rate_limit()
        if latest_rate_limit is None:
            return _unknown_quota(now)
        return _quota_from_rate_limit(latest_rate_limit, now)

    def _cached_latest_rate_limit(self) -> JsonDict | None:
        cache_now = self._monotonic()
        if self._quota_rate_limit_cache is not None:
            cached_at, cached_rate_limit = self._quota_rate_limit_cache
            if cache_now - cached_at < self.quota_cache_ttl_sec:
                return cached_rate_limit

        latest: tuple[datetime, JsonDict] | None = None

        for path, modified_ts in _session_candidates_by_mtime(self.codex_home):
            if latest is not None and modified_ts <= latest[0].timestamp():
                break
            for record in _iter_jsonl(path):
                payload = record.get("payload")
                if not isinstance(payload, dict):
                    continue
                if payload.get("type") != "token_count":
                    continue

                rate_limit = _select_codex_rate_limit(payload.get("rate_limits"))
                if rate_limit is None:
                    continue

                event_time = _parse_datetime(record.get("timestamp")) or datetime.min.replace(tzinfo=timezone.utc)
                if latest is None or event_time > latest[0]:
                    latest = (event_time, rate_limit)

        latest_rate_limit = latest[1] if latest is not None else None
        self._quota_rate_limit_cache = (cache_now, latest_rate_limit)
        return latest_rate_limit


def _read_index(path: Path, *, limit: int | None) -> list[_IndexEntry]:
    entries: list[_IndexEntry] = []
    if not path.exists():
        return entries

    for order, record in enumerate(_iter_jsonl(path)):
        session_id = record.get("id")
        if not isinstance(session_id, str) or not session_id:
            continue

        title = record.get("thread_name")
        updated_at = _parse_datetime(record.get("updated_at"))
        entries.append(
            _IndexEntry(
                session_id=session_id,
                title=title if isinstance(title, str) and title else session_id,
                updated_at=updated_at,
                order=order,
            )
        )

    entries.sort(
        key=lambda entry: (
            entry.updated_at or datetime.min.replace(tzinfo=timezone.utc),
            entry.order,
        ),
        reverse=True,
    )
    if limit is None:
        return entries
    return entries[: max(limit, 0)]


def _session_candidates(codex_home: Path) -> list[Path]:
    candidates: list[Path] = []
    for root_name in ("sessions", "archived_sessions"):
        root = codex_home / root_name
        if root.exists():
            candidates.extend(path for path in root.rglob("*.jsonl") if path.is_file())
    return candidates


def _session_candidates_by_mtime(codex_home: Path) -> list[tuple[Path, float]]:
    candidates = []
    for path in _session_candidates(codex_home):
        try:
            modified_ts = path.stat().st_mtime
        except OSError:
            modified_ts = 0.0
        candidates.append((path, modified_ts))
    return sorted(candidates, key=lambda item: item[1], reverse=True)


def _find_session_file(session_id: str, candidates: list[Path]) -> Path | None:
    for path in candidates:
        if session_id in path.name:
            return path

    for path in candidates:
        first = _read_first_json(path)
        if not first:
            continue
        if first.get("type") != "session_meta":
            continue
        payload = first.get("payload")
        if isinstance(payload, dict) and payload.get("id") == session_id:
            return path

    return None


def _select_codex_rate_limit(value: Any) -> JsonDict | None:
    if isinstance(value, dict):
        return value
    if not isinstance(value, list):
        return None

    candidates = [item for item in value if isinstance(item, dict)]
    if not candidates:
        return None

    for candidate in candidates:
        if candidate.get("limit_id") == "codex":
            return candidate
    return candidates[0]


def _quota_from_rate_limit(rate_limit: JsonDict, now: datetime) -> QuotaSnapshot:
    return QuotaSnapshot(
        primary_constraint="five_hour",
        buckets=[
            _quota_bucket("five_hour", "5h", rate_limit.get("primary"), now),
            _quota_bucket("weekly", "Week", rate_limit.get("secondary"), now),
        ],
    )


def _unknown_quota(now: datetime) -> QuotaSnapshot:
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


def _quota_bucket(kind: str, label: str, value: Any, now: datetime) -> QuotaBucket:
    details = value if isinstance(value, dict) else {}
    used_percent = _clamp_percent(details.get("used_percent"))
    resets_at = _parse_epoch_datetime(details.get("resets_at"))
    reset_in_sec = None
    if resets_at is not None:
        reset_in_sec = max(0, int((resets_at - now).total_seconds()))

    return QuotaBucket(
        kind=kind,
        label=label,
        used_percent=used_percent,
        remaining_percent=max(0, 100 - used_percent),
        resets_at=resets_at,
        reset_in_sec=reset_in_sec,
        status=_quota_status(used_percent),
    )


def _clamp_percent(value: Any) -> int:
    if isinstance(value, bool):
        return 0
    if isinstance(value, int | float):
        return max(0, min(100, int(round(value))))
    return 0


def _parse_epoch_datetime(value: Any) -> datetime | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int | float):
        return datetime.fromtimestamp(value, timezone.utc)
    if isinstance(value, str):
        try:
            return datetime.fromtimestamp(float(value), timezone.utc)
        except ValueError:
            return None
    return None


def _quota_status(used_percent: int) -> str:
    if used_percent >= 95:
        return "critical"
    if used_percent >= 80:
        return "warning"
    return "normal"


def _is_subagent_session(path: Path) -> bool:
    first_record = _read_first_json(path)
    if not first_record or first_record.get("type") != "session_meta":
        return False

    payload = first_record.get("payload")
    if not isinstance(payload, dict):
        return False

    source = payload.get("source")
    return isinstance(source, dict) and isinstance(source.get("subagent"), dict)


def _read_session_facts(path: Path) -> _SessionFacts:
    cwd: str | None = None
    started_at: datetime | None = None
    updated_at: datetime | None = None
    last_event: str | None = None
    status_signal: Status | None = None
    open_call_ids: set[str] = set()
    open_unnamed_calls = 0

    for record in _iter_jsonl(path):
        event_time = _parse_datetime(record.get("timestamp"))
        if event_time:
            updated_at = event_time if updated_at is None else max(updated_at, event_time)
            started_at = event_time if started_at is None else min(started_at, event_time)

        record_type = record.get("type")
        payload = record.get("payload")
        payload = payload if isinstance(payload, dict) else {}

        if record_type == "session_meta":
            cwd = _string_or_none(payload.get("cwd")) or cwd
            meta_started = _parse_datetime(payload.get("timestamp"))
            if meta_started:
                started_at = meta_started if started_at is None else min(started_at, meta_started)
            continue

        payload_type = payload.get("type")
        if payload_type == "function_call":
            call_id = _string_or_none(payload.get("call_id"))
            if call_id:
                open_call_ids.add(call_id)
            else:
                open_unnamed_calls += 1
            last_event = _tool_summary(payload)
            status_signal = Status.TOOL_RUNNING
            continue

        if payload_type == "function_call_output":
            call_id = _string_or_none(payload.get("call_id"))
            if call_id:
                open_call_ids.discard(call_id)
            elif open_unnamed_calls:
                open_unnamed_calls -= 1
            last_event = "tool finished"
            if _has_open_calls(open_call_ids, open_unnamed_calls):
                status_signal = Status.TOOL_RUNNING
            else:
                status_signal = Status.FAILED if _tool_output_failed(payload) else Status.RUNNING
            continue

        if payload_type == "custom_tool_call":
            call_id = _string_or_none(payload.get("call_id"))
            if call_id:
                open_call_ids.add(call_id)
            else:
                open_unnamed_calls += 1
            last_event = "tool running"
            status_signal = Status.TOOL_RUNNING
            continue

        if payload_type == "custom_tool_call_output":
            call_id = _string_or_none(payload.get("call_id"))
            if call_id:
                open_call_ids.discard(call_id)
            elif open_unnamed_calls:
                open_unnamed_calls -= 1
            last_event = "tool finished"
            if _has_open_calls(open_call_ids, open_unnamed_calls):
                status_signal = Status.TOOL_RUNNING
            else:
                status_signal = Status.FAILED if _tool_output_failed(payload) else Status.RUNNING
            continue

        if payload_type == "message" and payload.get("role") == "assistant":
            is_final_answer = payload.get("final_answer") is True or payload.get("phase") == "final_answer"
            text = _assistant_text(payload)
            if text:
                last_event = text
            if text and _looks_approval_required(text):
                status_signal = Status.APPROVAL_REQUIRED
            elif text and _looks_failed(text):
                status_signal = Status.FAILED
            elif text and _looks_awaiting_user(text):
                status_signal = Status.AWAITING_USER
            elif is_final_answer:
                status_signal = Status.COMPLETED
            elif text:
                status_signal = Status.RUNNING
            continue

        if payload_type == "final_answer":
            text = _assistant_text(payload)
            if text:
                last_event = text
            if text and _looks_approval_required(text):
                status_signal = Status.APPROVAL_REQUIRED
            elif text and _looks_failed(text):
                status_signal = Status.FAILED
            elif text and _looks_awaiting_user(text):
                status_signal = Status.AWAITING_USER
            else:
                status_signal = Status.COMPLETED
            continue

        if record_type == "event_msg":
            event_type = _string_or_none(payload.get("type"))
            event_failed = _event_exit_code_failed(payload)
            event_status: Status | None = None
            if event_type in APPROVAL_REQUIRED_EVENT_TYPES:
                last_event = _short_event_message(payload) or "approval required"
                event_status = Status.APPROVAL_REQUIRED
            elif event_failed:
                last_event = _event_exit_code_summary(payload)
                event_status = Status.FAILED
            if event_status is None and not event_failed and event_type == "task_complete":
                last_event = "task complete"
                event_status = Status.COMPLETED
            if event_status is None and not event_failed and payload.get("phase") == "final_answer":
                event_status = Status.COMPLETED
            message = _short_event_message(payload)
            if message:
                if _looks_approval_required(message):
                    last_event = message
                    event_status = Status.APPROVAL_REQUIRED
                elif _looks_failed(message):
                    last_event = message
                    event_status = Status.FAILED
                elif event_status == Status.FAILED:
                    pass
                elif _looks_awaiting_user(message):
                    last_event = message
                    event_status = Status.AWAITING_USER
                elif payload.get("phase") == "final_answer":
                    last_event = message
                    event_status = Status.COMPLETED
                else:
                    last_event = message
                    event_status = Status.RUNNING
            if event_status is not None:
                status_signal = event_status

    status = status_signal or Status.UNKNOWN
    return _SessionFacts(
        cwd=cwd,
        started_at=started_at,
        updated_at=updated_at,
        last_event=sanitize_text(last_event) if last_event else None,
        status=status,
    )


def _build_thread(entry: _IndexEntry, facts: _SessionFacts | None, now: datetime) -> ThreadSnapshot:
    status = _status_at_time(facts, now) if facts else Status.UNKNOWN
    started_at = facts.started_at if facts else None
    updated_at = (facts.updated_at if facts else None) or entry.updated_at or datetime.now(timezone.utc)
    duration_sec = _duration_sec(started_at, updated_at)
    last_event = facts.last_event if facts else None

    return ThreadSnapshot(
        id=f"codex:{entry.session_id}",
        project_id=_project_id_for_cwd(facts.cwd if facts else None),
        source=CODEX_SOURCE,
        title=sanitize_text(entry.title),
        status=status,
        attention_priority=priority_for_status(status),
        started_at=started_at,
        updated_at=updated_at,
        completed_at=updated_at if status == Status.COMPLETED else None,
        duration_sec=duration_sec,
        last_event=last_event,
        approval_required=status == Status.APPROVAL_REQUIRED,
        error_summary=last_event if status == Status.FAILED else None,
        display_version=DISPLAY_VERSION,
    )


def _duration_sec(started_at: datetime | None, updated_at: datetime) -> int:
    if started_at is None:
        return 0
    return max(0, int((updated_at - started_at).total_seconds()))


def _status_at_time(facts: _SessionFacts | None, now: datetime) -> Status:
    if facts is None:
        return Status.UNKNOWN

    status = facts.status
    if status not in {Status.RUNNING, Status.TOOL_RUNNING, Status.THINKING}:
        return status

    updated_at = _normalize_datetime(facts.updated_at)
    started_at = _normalize_datetime(facts.started_at)
    if updated_at is not None and (now - updated_at).total_seconds() >= STALE_AFTER_SEC:
        return Status.STALE
    if started_at is not None and (now - started_at).total_seconds() >= LONG_RUNNING_AFTER_SEC:
        return Status.LONG_RUNNING
    return status


def _project_id_for_cwd(cwd: str | None) -> str:
    if not cwd:
        return "project:unknown"

    project_path = _canonical_project_path(Path(cwd))
    basename = project_path.name or "unknown"
    safe_basename = PROJECT_NAME_RE.sub("_", basename).strip("._-") or "unknown"
    digest = hashlib.sha256(str(project_path).encode("utf-8")).hexdigest()[:8]
    return f"project:{safe_basename}:{digest}"


def _canonical_project_path(path: Path) -> Path:
    codex_worktree_project = _canonical_codex_worktree_path(path)
    if codex_worktree_project is not None:
        return codex_worktree_project

    git_file = path / ".git"
    if not git_file.is_file():
        return path

    try:
        first_line = git_file.read_text(encoding="utf-8", errors="ignore").splitlines()[0]
    except (OSError, IndexError):
        return path

    prefix = "gitdir:"
    if not first_line.startswith(prefix):
        return path

    git_dir = Path(first_line[len(prefix) :].strip())
    if not git_dir.is_absolute():
        git_dir = (path / git_dir).resolve()

    parts = git_dir.parts
    for index, part in enumerate(parts):
        if part == ".git" and index + 2 < len(parts) and parts[index + 1] == "worktrees":
            return Path(*parts[:index])

    return path


def _canonical_codex_worktree_path(path: Path) -> Path | None:
    parts = path.parts
    for index, part in enumerate(parts):
        if part != ".codex":
            continue
        if index + 3 >= len(parts) or parts[index + 1] != "worktrees":
            continue
        home_root = Path(*parts[:index])
        repo_name = parts[index + 3]
        candidate = home_root / "Projects" / repo_name
        if candidate.exists():
            return candidate
    return None


def _iter_jsonl(path: Path):
    try:
        with path.open(encoding="utf-8") as handle:
            for line in handle:
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(record, dict):
                    yield record
    except OSError:
        return


def _read_first_json(path: Path) -> JsonDict | None:
    try:
        with path.open(encoding="utf-8") as handle:
            line = handle.readline()
    except OSError:
        return None

    try:
        record = json.loads(line)
    except json.JSONDecodeError:
        return None
    return record if isinstance(record, dict) else None


def _parse_datetime(value: Any) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    if parsed.tzinfo is None:
        return parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)


def _normalize_datetime(value: datetime | None) -> datetime | None:
    if value is None:
        return None
    if value.tzinfo is None:
        return value.replace(tzinfo=timezone.utc)
    return value.astimezone(timezone.utc)


def _assistant_text(payload: JsonDict) -> str | None:
    content = payload.get("content")
    if not isinstance(content, list):
        return None

    parts: list[str] = []
    for item in content:
        if not isinstance(item, dict):
            continue
        text = item.get("text")
        if isinstance(text, str) and text:
            parts.append(_preview(text, TEXT_PREVIEW_CHARS))
        if sum(len(part) for part in parts) >= TEXT_PREVIEW_CHARS:
            break

    if not parts:
        return None
    return " ".join(parts)


def _tool_summary(payload: JsonDict) -> str:
    name = _string_or_none(payload.get("name"))
    return f"tool running: {name}" if name else "tool running"


def _short_event_message(payload: JsonDict) -> str | None:
    for key in ("message", "msg", "summary"):
        value = _string_or_none(payload.get(key))
        if value:
            return _preview(value, TEXT_PREVIEW_CHARS)
    return None


def _looks_awaiting_user(value: str) -> bool:
    folded = value.lower()
    return any(marker in folded for marker in AWAITING_USER_MARKERS)


def _looks_approval_required(value: str) -> bool:
    folded = value.lower()
    return any(marker in folded for marker in APPROVAL_REQUIRED_MARKERS)


def _looks_failed(value: str) -> bool:
    folded = value.lower()
    if any(marker in folded for marker in FAILED_MARKERS):
        return True
    match = EXIT_CODE_RE.search(folded)
    return bool(match and match.group(1) != "0")


def _tool_output_failed(payload: JsonDict) -> bool:
    exit_code = payload.get("exit_code")
    if _exit_code_failed(exit_code):
        return True
    if exit_code is not None:
        return False

    output = payload.get("output")
    if isinstance(output, dict):
        output_exit_code = output.get("exit_code")
        if _exit_code_failed(output_exit_code):
            return True
        if output_exit_code is not None:
            return False

    output_text = _string_or_none(output)
    if not output_text:
        return False
    return _looks_failed(_preview(output_text, TOOL_OUTPUT_PREVIEW_CHARS))


def _has_open_calls(open_call_ids: set[str], open_unnamed_calls: int) -> bool:
    return bool(open_call_ids or open_unnamed_calls)


def _event_exit_code_failed(payload: JsonDict) -> bool:
    return _exit_code_failed(payload.get("exit_code"))


def _event_exit_code_summary(payload: JsonDict) -> str:
    return f"command exited with code {payload.get('exit_code')}"


def _exit_code_failed(value: Any) -> bool:
    if isinstance(value, bool) or value is None:
        return False
    if isinstance(value, int):
        return value != 0
    if isinstance(value, str):
        stripped = value.strip()
        return stripped.lstrip("-").isdigit() and int(stripped) != 0
    return False


def _preview(value: str, max_len: int) -> str:
    return value[:max_len]


def _string_or_none(value: Any) -> str | None:
    return value if isinstance(value, str) else None

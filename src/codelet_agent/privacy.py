from __future__ import annotations

import re


HOME_PATH_RE = re.compile(r"/Users/[A-Za-z0-9._-]+/[^\s]+")
SECRET_RE = re.compile(r"\b(sk-[A-Za-z0-9_-]{8,}|api[_-]?key[=:][A-Za-z0-9_-]+)\b", re.IGNORECASE)


def sanitize_text(value: str, *, max_len: int = 120) -> str:
    value = HOME_PATH_RE.sub("[path]", value)
    value = SECRET_RE.sub("[secret]", value)
    if len(value) <= max_len:
        return value
    return value[: max_len - 3].rstrip() + "..."

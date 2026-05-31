from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any

DEFAULT_SETTINGS_PATH = Path.home() / ".codelet-agent" / "settings.json"


@dataclass
class AgentSettings:
    sound_enabled: bool = True
    mute_until: datetime | None = None
    codex_home: Path = field(default_factory=lambda: Path.home() / ".codex")
    api_token: str = field(default_factory=lambda: os.environ.get("CODELET_API_TOKEN", ""))
    settings_path: Path | None = None

    def __post_init__(self) -> None:
        self.codex_home = Path(self.codex_home)
        self.settings_path = Path(self.settings_path) if self.settings_path else None

    @classmethod
    def load(
        cls,
        *,
        settings_path: Path | None = DEFAULT_SETTINGS_PATH,
        codex_home: Path | None = None,
    ) -> "AgentSettings":
        settings = cls(codex_home=codex_home or Path.home() / ".codex", settings_path=settings_path)
        if settings.settings_path is None or not settings.settings_path.exists():
            return settings

        try:
            payload = json.loads(settings.settings_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return settings
        if not isinstance(payload, dict):
            return settings

        env_api_token = _env_api_token()
        persisted_api_token = _str_or(payload.get("api_token"), "")

        return cls(
            sound_enabled=_bool_or(payload.get("sound_enabled"), settings.sound_enabled),
            mute_until=_datetime_or_none(payload.get("mute_until")),
            codex_home=settings.codex_home,
            api_token=env_api_token or persisted_api_token,
            settings_path=settings.settings_path,
        )

    def save(self) -> None:
        if self.settings_path is None:
            return

        self.settings_path.parent.mkdir(parents=True, exist_ok=True)
        payload: dict[str, Any] = {
            "sound_enabled": self.sound_enabled,
            "mute_until": self.mute_until.isoformat() if self.mute_until else None,
        }
        env_api_token = _env_api_token()
        if self.api_token and self.api_token != env_api_token:
            payload["api_token"] = self.api_token
        elif env_api_token and self.settings_path is not None:
            persisted_api_token = _read_persisted_api_token(self.settings_path)
            if persisted_api_token and persisted_api_token != env_api_token:
                payload["api_token"] = persisted_api_token

        encoded = json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
        handle_fd = os.open(
            self.settings_path,
            os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
            0o600,
        )
        with os.fdopen(handle_fd, "w", encoding="utf-8") as handle:
            handle.write(encoded)
        os.chmod(self.settings_path, 0o600)


def _bool_or(value: object, fallback: bool) -> bool:
    return value if isinstance(value, bool) else fallback


def _str_or(value: object, fallback: str) -> str:
    return value if isinstance(value, str) else fallback


def _env_api_token() -> str:
    return os.environ.get("CODELET_API_TOKEN", "")


def _read_persisted_api_token(settings_path: Path) -> str:
    try:
        payload = json.loads(settings_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return ""
    if not isinstance(payload, dict):
        return ""
    return _str_or(payload.get("api_token"), "")


def _datetime_or_none(value: object) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None

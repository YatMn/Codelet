from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path


@dataclass
class AgentSettings:
    sound_enabled: bool = True
    mute_until: datetime | None = None
    codex_home: Path = field(default_factory=lambda: Path.home() / ".codex")

    def __post_init__(self) -> None:
        self.codex_home = Path(self.codex_home)

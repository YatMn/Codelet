from __future__ import annotations

from datetime import datetime

from fastapi import Depends, FastAPI, Header, HTTPException
from pydantic import BaseModel

from codelet_agent.adapters.codex_local import CodexLocalAdapter
from codelet_agent.settings import AgentSettings
from codelet_agent.snapshot import build_snapshot


class SoundSettingsRequest(BaseModel):
    sound_enabled: bool


class MuteRequest(BaseModel):
    mute_until: datetime | None = None


def create_app(settings: AgentSettings | None = None) -> FastAPI:
    app = FastAPI(title="Codelet Agent")
    state = settings or AgentSettings.load()
    app.state.settings = state
    app.state.adapter = CodexLocalAdapter(codex_home=state.codex_home)

    def require_api_token(x_codelet_token: str | None = Header(default=None)) -> None:
        if not state.api_token:
            return
        if x_codelet_token != state.api_token:
            raise HTTPException(status_code=401, detail="invalid Codelet API token")

    @app.get("/api/v1/health")
    def health() -> dict[str, str]:
        return {"status": "ok"}

    @app.get("/api/v1/snapshot", dependencies=[Depends(require_api_token)])
    def snapshot():
        return build_snapshot(
            adapter=app.state.adapter,
            sound_enabled=state.sound_enabled,
            privacy_mode=False,
            mute_until=state.mute_until,
        )

    @app.post("/api/v1/settings/sound", dependencies=[Depends(require_api_token)])
    def update_sound_settings(request: SoundSettingsRequest) -> dict[str, bool]:
        state.sound_enabled = request.sound_enabled
        state.save()
        return {"sound_enabled": state.sound_enabled}

    @app.post("/api/v1/mute", dependencies=[Depends(require_api_token)])
    def update_mute(request: MuteRequest) -> dict[str, str | None]:
        state.mute_until = request.mute_until
        state.save()
        return {"mute_until": _datetime_or_none(state.mute_until)}

    return app


def _datetime_or_none(value: datetime | None) -> str | None:
    return value.isoformat() if value else None

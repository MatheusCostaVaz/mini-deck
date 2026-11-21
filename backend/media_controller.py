"""Controlador nativo de mídia do Windows 11 (SMTC + Media Keys fallback).

Utiliza a API WinRT `GlobalSystemMediaTransportControlsSessionManager` para:
- Ler Título, Artista/App e Estado (Playing / Paused) da mídia ativa
  (Spotify, YouTube/Edge/Chrome, Apple Music, VLC, etc.).
- Disparar comandos de controle multimídia (`play_pause`, `next`, `prev`),
  com fallback automático para teclas virtuais de mídia do Windows (`VK_MEDIA_*`).
"""

from __future__ import annotations

import ctypes
from dataclasses import dataclass
import re
from typing import Optional

try:
    from winrt.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as SessionManager,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus,
    )

    HAS_WINRT_MEDIA = True
except ImportError:
    SessionManager = None  # type: ignore[assignment]
    PlaybackStatus = None  # type: ignore[assignment]
    HAS_WINRT_MEDIA = False

# Códigos virtuais das teclas multimídia do Windows (user32.keybd_event)
VK_MEDIA_NEXT_TRACK = 0xB0
VK_MEDIA_PREV_TRACK = 0xB1
VK_MEDIA_PLAY_PAUSE = 0xB3
KEYEVENTF_KEYUP = 0x0002

APP_FRIENDLY_NAMES = {
    "msedge": "Microsoft Edge",
    "chrome": "Google Chrome",
    "firefox": "Firefox",
    "brave": "Brave",
    "spotify": "Spotify",
    "music.ui": "Media Player",
    "vlc": "VLC",
}


@dataclass(eq=True)
class MediaSnapshot:
    title: str = "Nenhuma mídia"
    artist: str = "Aguardando reprodução"
    playing: bool = False


def _sanitize_latin1(text: Optional[str], max_len: int) -> str:
    """Normaliza pontuação especial e remove emojis fora do range Latin-1 da fonte Inter."""
    if not text:
        return ""

    replacements = {
        "\u2013": "-",
        "\u2014": "-",
        "\u2018": "'",
        "\u2019": "'",
        "\u201c": '"',
        "\u201d": '"',
        "\u2026": "...",
        "\u00a0": " ",
    }
    for src, dst in replacements.items():
        text = text.replace(src, dst)

    # Mantém apenas caracteres ASCII + Latin-1 (32-126 e 160-255, presentes na fonte Inter)
    filtered = "".join(
        ch for ch in text if (32 <= ord(ch) <= 126) or (160 <= ord(ch) <= 255)
    )
    collapsed = re.sub(r"\s+", " ", filtered).strip()
    if len(collapsed) > max_len:
        collapsed = collapsed[: max_len - 3].rstrip() + "..."
    return collapsed


def _friendly_app_name(aumid: str) -> str:
    if not aumid:
        return ""
    low = aumid.lower()
    for key, label in APP_FRIENDLY_NAMES.items():
        if key in low:
            return label
    base = aumid.split("!")[0].split(".")[-1]
    if base.lower().endswith("exe"):
        base = base[:-3]
    return _sanitize_latin1(base, 28)


def _send_media_vk(vk_code: int) -> None:
    """Dispara uma tecla multimídia global do Windows via user32.keybd_event."""
    user32 = ctypes.windll.user32
    user32.keybd_event(vk_code, 0, 0, 0)
    user32.keybd_event(vk_code, 0, KEYEVENTF_KEYUP, 0)


class MediaController:
    """Interface assíncrona com o Windows SMTC (GlobalSystemMediaTransportControls)."""

    def __init__(self) -> None:
        self._manager = None

    async def _ensure_manager(self):
        if not HAS_WINRT_MEDIA:
            return None
        if self._manager is None:
            try:
                self._manager = await SessionManager.request_async()
            except Exception:
                self._manager = None
        return self._manager

    async def _get_active_session(self):
        manager = await self._ensure_manager()
        if manager is None:
            return None

        try:
            current = manager.get_current_session()
            if current is not None:
                return current

            sessions = manager.get_sessions()
            if not sessions:
                return None

            # Prefere qualquer sessão que esteja reproduzindo
            for sess in sessions:
                pb = sess.get_playback_info()
                if pb is not None and pb.playback_status == PlaybackStatus.PLAYING:
                    return sess
            return sessions[0]
        except Exception:
            self._manager = None
            return None

    async def get_current_media(self) -> MediaSnapshot:
        """Retorna título, artista e status (playing) da mídia atual do Windows."""
        session = await self._get_active_session()
        if session is None:
            return MediaSnapshot()

        try:
            props = await session.try_get_media_properties_async()
            pb = session.get_playback_info()
            playing = bool(pb is not None and pb.playback_status == PlaybackStatus.PLAYING)

            raw_title = props.title if props is not None else ""
            raw_artist = props.artist if props is not None else ""
            app_name = _friendly_app_name(getattr(session, "source_app_user_model_id", "") or "")

            title = _sanitize_latin1(raw_title, 96) or (app_name or "Mídia ativa")
            artist = _sanitize_latin1(raw_artist, 64) or app_name or "Windows"

            return MediaSnapshot(title=title, artist=artist, playing=playing)
        except Exception:
            return MediaSnapshot()

    async def handle_action(self, action: str) -> bool:
        """Executa ação de mídia ('play_pause', 'next', 'prev') via SMTC ou Media Key."""
        action = action.strip().lower()
        vk_map = {
            "play_pause": VK_MEDIA_PLAY_PAUSE,
            "next": VK_MEDIA_NEXT_TRACK,
            "prev": VK_MEDIA_PREV_TRACK,
        }
        if action not in vk_map:
            return False

        session = await self._get_active_session()
        if session is not None:
            try:
                if action == "play_pause":
                    ok = await session.try_toggle_play_pause_async()
                elif action == "next":
                    ok = await session.try_skip_next_async()
                else:
                    ok = await session.try_skip_previous_async()
                if ok:
                    return True
            except Exception:
                pass

        # Fallback confiável via tecla multimídia global do Windows
        try:
            _send_media_vk(vk_map[action])
            return True
        except Exception:
            return False

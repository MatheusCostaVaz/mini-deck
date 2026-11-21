"""Módulo de controle de áudio do Windows 11 para o Mini Deck IoT.

Utiliza pycaw e comtypes (Windows Core Audio API / WASAPI) para controlar:
- Volume Master e Mute Master do dispositivo de saída padrão
- Sessões de áudio por aplicativo (listagem, volume individual e mute)
- Volume e Mute do microfone padrão (dispositivo de entrada)
"""

from __future__ import annotations

from _ctypes import COMError
from dataclasses import dataclass, field
from typing import Optional

import comtypes
from pycaw.constants import AudioSessionState
from pycaw.pycaw import AudioUtilities


def _clamp_volume(level: int | float) -> int:
    """Restringe um nível de volume ao intervalo inteiro [0, 100]."""
    return max(0, min(100, int(round(level))))


def _normalize_app_name(name: str) -> str:
    """Normaliza nome de processo removendo espaços extras e sufixo '.exe'."""
    normalized = name.strip().lower()
    if normalized.endswith(".exe"):
        normalized = normalized[:-4]
    return normalized


@dataclass
class AudioAppSession:
    """Representa um aplicativo com sessão de áudio registrada no Windows."""

    name: str
    process_name: str
    volume: int
    muted: bool
    is_active: bool
    pids: list[int] = field(default_factory=list)
    display_name: str = ""

    @property
    def state(self) -> str:
        return "ativo" if self.is_active else "inativo"


class AudioController:
    """Controlador de áudio do Windows usando pycaw e comtypes."""

    def __init__(self) -> None:
        self._com_initialized = False
        try:
            comtypes.CoInitialize()
            self._com_initialized = True
        except OSError:
            # COM já inicializado nesta thread com outro modo de apartamento
            self._com_initialized = False

        self._speakers = None
        self._microphone = None
        self.refresh_devices()

    def __enter__(self) -> "AudioController":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def close(self) -> None:
        """Libera referências COM e finaliza CoInitialize se iniciado por esta instância."""
        self._speakers = None
        self._microphone = None
        if self._com_initialized:
            comtypes.CoUninitialize()
            self._com_initialized = False

    def refresh_devices(self) -> None:
        """Recarrega os dispositivos padrão de saída (speakers) e entrada (microfone)."""
        try:
            self._speakers = AudioUtilities.GetSpeakers()
        except (COMError, OSError, Exception):
            self._speakers = None

        try:
            raw_mic = AudioUtilities.GetMicrophone()
            self._microphone = (
                AudioUtilities.CreateDevice(raw_mic) if raw_mic is not None else None
            )
        except (COMError, OSError, Exception):
            self._microphone = None

    def _with_device_retry(self, op):
        """Executa uma operação WASAPI com reconexão automática (hot-plug) em caso de falha."""
        try:
            return op()
        except (COMError, OSError, RuntimeError, Exception):
            self.refresh_devices()
            return op()

    # ------------------------------------------------------------------
    # Volume Master (Saída padrão)
    # ------------------------------------------------------------------

    def _require_speakers(self):
        if self._speakers is None:
            self.refresh_devices()
        if self._speakers is None:
            raise RuntimeError("Nenhum dispositivo de saída de áudio padrão encontrado.")
        return self._speakers

    def get_master_device_name(self) -> Optional[str]:
        """Retorna o nome amigável do dispositivo de saída padrão."""
        try:
            if self._speakers is None:
                self.refresh_devices()
            return self._speakers.FriendlyName if self._speakers is not None else None
        except (COMError, OSError, Exception):
            self.refresh_devices()
            return self._speakers.FriendlyName if self._speakers is not None else None

    def get_master_volume(self) -> int:
        """Retorna o Volume Master atual (0 a 100)."""
        def _op() -> int:
            endpoint = self._require_speakers().EndpointVolume
            return _clamp_volume(endpoint.GetMasterVolumeLevelScalar() * 100.0)

        return self._with_device_retry(_op)

    def set_master_volume(self, level: int | float) -> int:
        """Define o Volume Master (0 a 100) e retorna o valor aplicado."""
        clamped = _clamp_volume(level)

        def _op() -> int:
            endpoint = self._require_speakers().EndpointVolume
            endpoint.SetMasterVolumeLevelScalar(clamped / 100.0, None)
            return clamped

        return self._with_device_retry(_op)

    def get_master_mute(self) -> bool:
        """Retorna True se o áudio Master estiver mutado."""
        def _op() -> bool:
            endpoint = self._require_speakers().EndpointVolume
            return bool(endpoint.GetMute())

        return self._with_device_retry(_op)

    def set_master_mute(self, muted: bool) -> bool:
        """Define o estado de Mute Master e retorna o novo estado."""
        def _op() -> bool:
            endpoint = self._require_speakers().EndpointVolume
            endpoint.SetMute(1 if muted else 0, None)
            return bool(endpoint.GetMute())

        return self._with_device_retry(_op)

    def toggle_master_mute(self) -> bool:
        """Alterna o estado de Mute Master e retorna o novo estado."""
        def _op() -> bool:
            endpoint = self._require_speakers().EndpointVolume
            current = bool(endpoint.GetMute())
            endpoint.SetMute(0 if current else 1, None)
            return bool(endpoint.GetMute())

        return self._with_device_retry(_op)

    # ------------------------------------------------------------------
    # Microfone padrão (Entrada de áudio)
    # ------------------------------------------------------------------

    def has_microphone(self) -> bool:
        """Indica se há um microfone padrão disponível no sistema."""
        if self._microphone is None:
            self.refresh_devices()
        return self._microphone is not None

    def _require_microphone(self):
        if self._microphone is None:
            self.refresh_devices()
        if self._microphone is None:
            raise RuntimeError("Nenhum microfone padrão encontrado no sistema.")
        return self._microphone

    def get_mic_device_name(self) -> Optional[str]:
        """Retorna o nome amigável do microfone padrão."""
        try:
            if self._microphone is None:
                self.refresh_devices()
            return self._microphone.FriendlyName if self._microphone is not None else None
        except (COMError, OSError, Exception):
            self.refresh_devices()
            return self._microphone.FriendlyName if self._microphone is not None else None

    def get_mic_volume(self) -> int:
        """Retorna o nível de volume do microfone padrão (0 a 100)."""
        def _op() -> int:
            endpoint = self._require_microphone().EndpointVolume
            return _clamp_volume(endpoint.GetMasterVolumeLevelScalar() * 100.0)

        return self._with_device_retry(_op)

    def set_mic_volume(self, level: int | float) -> int:
        """Define o volume do microfone padrão (0 a 100) e retorna o valor aplicado."""
        clamped = _clamp_volume(level)

        def _op() -> int:
            endpoint = self._require_microphone().EndpointVolume
            endpoint.SetMasterVolumeLevelScalar(clamped / 100.0, None)
            return clamped

        return self._with_device_retry(_op)

    def get_mic_mute(self) -> bool:
        """Retorna True se o microfone padrão estiver mutado."""
        def _op() -> bool:
            endpoint = self._require_microphone().EndpointVolume
            return bool(endpoint.GetMute())

        return self._with_device_retry(_op)

    def set_mic_mute(self, muted: bool) -> bool:
        """Define o estado de Mute do microfone padrão e retorna o novo estado."""
        def _op() -> bool:
            endpoint = self._require_microphone().EndpointVolume
            endpoint.SetMute(1 if muted else 0, None)
            return bool(endpoint.GetMute())

        return self._with_device_retry(_op)

    def toggle_mic_mute(self) -> bool:
        """Alterna o estado de Mute do microfone padrão e retorna o novo estado."""
        def _op() -> bool:
            endpoint = self._require_microphone().EndpointVolume
            current = bool(endpoint.GetMute())
            endpoint.SetMute(0 if current else 1, None)
            return bool(endpoint.GetMute())

        return self._with_device_retry(_op)

    # ------------------------------------------------------------------
    # Sessões de áudio por aplicativo
    # ------------------------------------------------------------------

    def _get_raw_app_sessions(self) -> list:
        """Obtém sessões brutas do Windows com processo válido e não expiradas."""
        try:
            all_sessions = AudioUtilities.GetAllSessions()
        except (COMError, OSError, Exception):
            self.refresh_devices()
            try:
                all_sessions = AudioUtilities.GetAllSessions()
            except (COMError, OSError, Exception):
                return []

        valid_sessions = []
        for session in all_sessions:
            try:
                if session.State == AudioSessionState.Expired:
                    continue
                if session.Process is None:
                    continue
                valid_sessions.append(session)
            except (COMError, OSError, Exception):
                continue
        return valid_sessions

    def _find_matching_sessions(self, app_name: str) -> list:
        """Busca sessões de áudio correspondentes ao nome do processo informado.

        Primeiro tenta correspondência exata (com ou sem '.exe', case-insensitive).
        Se não encontrar, realiza busca parcial por substring no nome do processo.
        """
        target = _normalize_app_name(app_name)
        if not target:
            return []

        raw_sessions = self._get_raw_app_sessions()
        exact_matches = []
        partial_matches = []

        for session in raw_sessions:
            try:
                proc_name = session.Process.name()
            except Exception:
                continue

            normalized_proc = _normalize_app_name(proc_name)
            if normalized_proc == target:
                exact_matches.append(session)
            elif target in normalized_proc:
                partial_matches.append(session)

        return exact_matches if exact_matches else partial_matches

    def list_audio_sessions(
        self,
        active_only: bool = False,
        group_by_app: bool = True,
    ) -> list[AudioAppSession]:
        """Lista sessões de aplicativos de áudio detectadas no Windows.

        Args:
            active_only: Se True, retorna apenas aplicativos reproduzindo áudio
                no momento (AudioSessionState.Active).
            group_by_app: Se True, agrupa múltiplas sessões do mesmo executável
                (ex.: várias abas do Chrome ou subprocessos do Discord).
        """
        raw_sessions = self._get_raw_app_sessions()

        if not group_by_app:
            results: list[AudioAppSession] = []
            for session in raw_sessions:
                try:
                    proc_name = session.Process.name()
                    is_active = session.State == AudioSessionState.Active
                    if active_only and not is_active:
                        continue
                    vol_iface = session.SimpleAudioVolume
                    results.append(
                        AudioAppSession(
                            name=proc_name[:-4] if proc_name.lower().endswith(".exe") else proc_name,
                            process_name=proc_name,
                            volume=_clamp_volume(vol_iface.GetMasterVolume() * 100.0),
                            muted=bool(vol_iface.GetMute()),
                            is_active=is_active,
                            pids=[session.ProcessId],
                            display_name=session.DisplayName or "",
                        )
                    )
                except (COMError, OSError):
                    continue
            results.sort(key=lambda item: (not item.is_active, item.process_name.lower()))
            return results

        grouped: dict[str, AudioAppSession] = {}
        for session in raw_sessions:
            try:
                proc_name = session.Process.name()
                key = proc_name.lower()
                is_active = session.State == AudioSessionState.Active
                vol_iface = session.SimpleAudioVolume
                volume = _clamp_volume(vol_iface.GetMasterVolume() * 100.0)
                muted = bool(vol_iface.GetMute())
                pid = session.ProcessId
                display_name = session.DisplayName or ""
            except (COMError, OSError):
                continue

            if key not in grouped:
                friendly = proc_name[:-4] if proc_name.lower().endswith(".exe") else proc_name
                grouped[key] = AudioAppSession(
                    name=friendly,
                    process_name=proc_name,
                    volume=volume,
                    muted=muted,
                    is_active=is_active,
                    pids=[pid],
                    display_name=display_name,
                )
            else:
                entry = grouped[key]
                if pid not in entry.pids:
                    entry.pids.append(pid)
                if is_active and not entry.is_active:
                    entry.is_active = True
                    entry.volume = volume
                    entry.muted = muted
                elif is_active == entry.is_active:
                    entry.muted = entry.muted and muted
                if not entry.display_name and display_name:
                    entry.display_name = display_name

        apps = list(grouped.values())
        if active_only:
            apps = [app for app in apps if app.is_active]

        apps.sort(key=lambda item: (not item.is_active, item.process_name.lower()))
        return apps

    def get_app_volume(self, app_name: str) -> Optional[int]:
        """Retorna o volume (0 a 100) do aplicativo informado, ou None se não encontrado."""
        matches = self._find_matching_sessions(app_name)
        if not matches:
            return None

        # Prioriza sessão ativa, se existir
        target_session = next(
            (s for s in matches if s.State == AudioSessionState.Active),
            matches[0],
        )
        return _clamp_volume(target_session.SimpleAudioVolume.GetMasterVolume() * 100.0)

    def set_app_volume(self, app_name: str, level: int | float) -> bool:
        """Define o volume (0 a 100) de todas as sessões de um aplicativo.

        Retorna True se ao menos uma sessão correspondente foi atualizada.
        """
        scalar = _clamp_volume(level) / 100.0

        def _apply() -> bool:
            matches = self._find_matching_sessions(app_name)
            if not matches:
                return False
            updated = False
            for session in matches:
                try:
                    session.SimpleAudioVolume.SetMasterVolume(scalar, None)
                    updated = True
                except (COMError, OSError, Exception):
                    continue
            return updated

        try:
            if _apply():
                return True
        except (COMError, OSError, Exception):
            pass

        self.refresh_devices()
        try:
            return _apply()
        except (COMError, OSError, Exception):
            return False

    def get_app_mute(self, app_name: str) -> Optional[bool]:
        """Retorna o estado de mute do aplicativo informado, ou None se não encontrado."""
        def _read() -> Optional[bool]:
            matches = self._find_matching_sessions(app_name)
            if not matches:
                return None
            target_session = next(
                (s for s in matches if s.State == AudioSessionState.Active),
                matches[0],
            )
            return bool(target_session.SimpleAudioVolume.GetMute())

        try:
            return _read()
        except (COMError, OSError, Exception):
            self.refresh_devices()
            try:
                return _read()
            except (COMError, OSError, Exception):
                return None

    def set_app_mute(self, app_name: str, muted: bool) -> bool:
        """Define o estado de mute de todas as sessões de um aplicativo.

        Retorna True se ao menos uma sessão correspondente foi atualizada.
        """
        mute_flag = 1 if muted else 0

        def _apply() -> bool:
            matches = self._find_matching_sessions(app_name)
            if not matches:
                return False
            updated = False
            for session in matches:
                try:
                    session.SimpleAudioVolume.SetMute(mute_flag, None)
                    updated = True
                except (COMError, OSError, Exception):
                    continue
            return updated

        try:
            if _apply():
                return True
        except (COMError, OSError, Exception):
            pass

        self.refresh_devices()
        try:
            return _apply()
        except (COMError, OSError, Exception):
            return False

    def toggle_app_mute(self, app_name: str) -> Optional[bool]:
        """Alterna o mute de um aplicativo e retorna o novo estado (ou None se não encontrado)."""
        try:
            current = self.get_app_mute(app_name)
            if current is None:
                return None
            new_state = not current
            self.set_app_mute(app_name, new_state)
            return new_state
        except (COMError, OSError, Exception):
            self.refresh_devices()
            try:
                current = self.get_app_mute(app_name)
                if current is None:
                    return None
                new_state = not current
                self.set_app_mute(app_name, new_state)
                return new_state
            except (COMError, OSError, Exception):
                return None

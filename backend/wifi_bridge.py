"""Bridge Wi-Fi (WebSocket + Auto-Discovery UDP) do Mini Deck IoT para Windows 11.

Fluxo 100% sem fio:
1. Escuta broadcasts UDP na porta 4210 aguardando {"device":"minideck","port":81}
   para descobrir automaticamente o IP do ESP32-S3 na rede local.
2. Conecta via WebSocket em ws://<IP_DO_ESP32>:81.
3. Envia sincronização inicial completa (Master, Edge/Browser, Discord, Jogo/Steam, Microfone).
4. Escuta eventos de toque da tela em tempo real e aplica instantaneamente via AudioController.
5. Monitora alterações feitas no próprio Windows e sincroniza de volta para a tela.
6. Reconecta automaticamente em caso de reinicialização do ESP32 ou oscilação de Wi-Fi.
"""

from __future__ import annotations

import argparse
import asyncio
import ctypes
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import socket
import sys
import time
from dataclasses import dataclass
from typing import Optional

from _ctypes import COMError
import websockets
from websockets.exceptions import WebSocketException

from audio_controller import AudioAppSession, AudioController

BROWSER_CANDIDATES = (
    "msedge.exe",
    "chrome.exe",
    "firefox.exe",
    "brave.exe",
    "opera.exe",
    "vivaldi.exe",
)

CHAT_CANDIDATES = (
    "discord.exe",
    "ms-teams.exe",
    "teams.exe",
    "zoom.exe",
    "slack.exe",
    "telegram.exe",
)

IGNORED_UTILITY_PROCESSES = {
    "nvidia rtx voice.exe",
    "nvcontainer.exe",
    "audiodg.exe",
    "shellexperiencehost.exe",
    "searchhost.exe",
    "textinputhost.exe",
    "applicationframehost.exe",
}

FRIENDLY_LABELS = {
    "msedge.exe": "EDGE",
    "chrome.exe": "CHROME",
    "firefox.exe": "FIREFOX",
    "brave.exe": "BRAVE",
    "opera.exe": "OPERA",
    "vivaldi.exe": "VIVALDI",
    "discord.exe": "DISCORD",
    "ms-teams.exe": "TEAMS",
    "teams.exe": "TEAMS",
    "zoom.exe": "ZOOM",
    "slack.exe": "SLACK",
    "telegram.exe": "TELEGRAM",
    "steam.exe": "STEAM",
    "spotify.exe": "SPOTIFY",
}

DEFAULT_UDP_PORT = 4210
DEFAULT_WS_PORT = 81
POLL_INTERVAL_S = 0.25
APP_PING_INTERVAL_S = 2.0
RECONNECT_DELAY_S = 2.0
RECENT_ESP_WRITE_GRACE_S = 0.35

if getattr(sys, "frozen", False):
    BACKEND_DIR = Path(sys.executable).resolve().parent
else:
    BACKEND_DIR = Path(__file__).resolve().parent
LOG_PATH = BACKEND_DIR / "wifi_bridge.log"
MUTEX_NAME = "Local\\MiniDeckIoT_WiFiBridge_Mutex"


@dataclass
class ChannelSnapshot:
    volume: int
    muted: bool
    label: str
    process_name: Optional[str] = None


def _short_label(process_name: str, fallback: str) -> str:
    low = process_name.lower()
    if low in FRIENDLY_LABELS:
        return FRIENDLY_LABELS[low]
    base = process_name[:-4] if low.endswith(".exe") else process_name
    cleaned = base.strip().upper()
    return cleaned[:9] if cleaned else fallback


def _setup_logger(verbose: bool = False) -> logging.Logger:
    logger = logging.getLogger("mini_deck_wifi_bridge")
    logger.setLevel(logging.DEBUG if verbose else logging.INFO)
    logger.handlers.clear()

    fmt = logging.Formatter("%(asctime)s [%(levelname)s] %(message)s", datefmt="%H:%M:%S")

    try:
        file_handler = RotatingFileHandler(
            LOG_PATH, maxBytes=512 * 1024, backupCount=1, encoding="utf-8"
        )
        file_handler.setFormatter(fmt)
        logger.addHandler(file_handler)
    except OSError:
        pass

    if sys.stdout is not None:
        stream_handler = logging.StreamHandler(sys.stdout)
        stream_handler.setFormatter(fmt)
        logger.addHandler(stream_handler)

    return logger


def _acquire_single_instance_mutex():
    if os.name != "nt":
        return None
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.CreateMutexW(None, False, MUTEX_NAME)
    if not handle:
        return None
    ERROR_ALREADY_EXISTS = 183
    if kernel32.GetLastError() == ERROR_ALREADY_EXISTS:
        kernel32.CloseHandle(handle)
        return False
    return handle


def _release_mutex(handle) -> None:
    if os.name == "nt" and handle:
        ctypes.windll.kernel32.CloseHandle(handle)


class _UdpDiscoveryProtocol(asyncio.DatagramProtocol):
    def __init__(self, future: asyncio.Future, logger: logging.Logger) -> None:
        self._future = future
        self._log = logger

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if self._future.done():
            return
        try:
            text = data.decode("utf-8", errors="replace").strip()
            payload = json.loads(text)
        except (UnicodeDecodeError, json.JSONDecodeError):
            return

        if isinstance(payload, dict) and payload.get("device") == "minideck":
            ip = addr[0]
            try:
                port = int(payload.get("port", DEFAULT_WS_PORT))
            except (TypeError, ValueError):
                port = DEFAULT_WS_PORT
            self._log.info("Auto-Discovery UDP: Mini Deck encontrado em %s:%d", ip, port)
            self._future.set_result((ip, port))


class MiniDeckWiFiBridge:
    """Cliente WebSocket resiliente com Auto-Discovery UDP para o Mini Deck IoT."""

    def __init__(
        self,
        udp_port: int = DEFAULT_UDP_PORT,
        fixed_host: Optional[str] = None,
        fixed_port: int = DEFAULT_WS_PORT,
        verbose: bool = False,
        logger: Optional[logging.Logger] = None,
    ) -> None:
        self.udp_port = udp_port
        self.fixed_host = fixed_host
        self.fixed_port = fixed_port
        self.verbose = verbose
        self.log = logger or _setup_logger(verbose)

        self._last_channels: dict[str, ChannelSnapshot] = {}
        self._last_mic_mute: Optional[bool] = None
        self._last_esp_write_ts: dict[str, float] = {}
        self._resolved_processes: dict[str, Optional[str]] = {
            "browser": None,
            "discord": None,
            "game": None,
        }

    # ------------------------------------------------------------------
    # Auto-Discovery UDP (porta 4210)
    # ------------------------------------------------------------------

    async def discover_device(self, timeout: Optional[float] = None) -> Optional[tuple[str, int]]:
        if self.fixed_host:
            return (self.fixed_host, self.fixed_port)

        loop = asyncio.get_running_loop()
        found_future: asyncio.Future[tuple[str, int]] = loop.create_future()

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.bind(("0.0.0.0", self.udp_port))
        sock.setblocking(False)

        transport, _ = await loop.create_datagram_endpoint(
            lambda: _UdpDiscoveryProtocol(found_future, self.log),
            sock=sock,
        )

        self.log.info("Aguardando broadcast UDP na porta %d (Auto-Discovery)...", self.udp_port)
        try:
            if timeout is not None:
                return await asyncio.wait_for(found_future, timeout=timeout)
            return await found_future
        except asyncio.TimeoutError:
            return None
        finally:
            transport.close()

    # ------------------------------------------------------------------
    # Resolução de Sessões de Áudio do Windows
    # ------------------------------------------------------------------

    def _pick_session(
        self,
        sessions: list[AudioAppSession],
        candidates: tuple[str, ...],
    ) -> Optional[AudioAppSession]:
        by_name = {s.process_name.lower(): s for s in sessions}
        for cand in candidates:
            sess = by_name.get(cand)
            if sess is not None and sess.is_active:
                return sess
        for cand in candidates:
            sess = by_name.get(cand)
            if sess is not None:
                return sess
        return None

    def _pick_game_session(
        self,
        sessions: list[AudioAppSession],
        used_processes: set[str],
    ) -> Optional[AudioAppSession]:
        excluded = (
            used_processes
            | set(BROWSER_CANDIDATES)
            | set(CHAT_CANDIDATES)
            | IGNORED_UTILITY_PROCESSES
        )
        candidates = [s for s in sessions if s.process_name.lower() not in excluded]
        return candidates[0] if candidates else None

    def _read_windows_state(
        self, audio: AudioController
    ) -> tuple[dict[str, ChannelSnapshot], bool]:
        channels: dict[str, ChannelSnapshot] = {}

        try:
            master_vol = audio.get_master_volume()
            master_mute = audio.get_master_mute()
        except (RuntimeError, COMError, OSError):
            audio.refresh_devices()
            master_vol, master_mute = 100, False

        channels["master"] = ChannelSnapshot(
            volume=master_vol,
            muted=master_mute,
            label="MASTER",
            process_name="master",
        )

        try:
            sessions = audio.list_audio_sessions(active_only=False, group_by_app=True)
        except (COMError, OSError):
            audio.refresh_devices()
            sessions = []

        used: set[str] = set()

        browser_sess = self._pick_session(sessions, BROWSER_CANDIDATES)
        if browser_sess is not None:
            used.add(browser_sess.process_name.lower())
            self._resolved_processes["browser"] = browser_sess.process_name
            channels["browser"] = ChannelSnapshot(
                volume=browser_sess.volume,
                muted=browser_sess.muted,
                label=_short_label(browser_sess.process_name, "EDGE"),
                process_name=browser_sess.process_name,
            )
        else:
            self._resolved_processes["browser"] = None
            prev = self._last_channels.get("browser")
            channels["browser"] = ChannelSnapshot(
                volume=prev.volume if prev else 100,
                muted=prev.muted if prev else False,
                label="EDGE",
                process_name=None,
            )

        chat_sess = self._pick_session(sessions, CHAT_CANDIDATES)
        if chat_sess is not None:
            used.add(chat_sess.process_name.lower())
            self._resolved_processes["discord"] = chat_sess.process_name
            channels["discord"] = ChannelSnapshot(
                volume=chat_sess.volume,
                muted=chat_sess.muted,
                label=_short_label(chat_sess.process_name, "DISCORD"),
                process_name=chat_sess.process_name,
            )
        else:
            self._resolved_processes["discord"] = None
            prev = self._last_channels.get("discord")
            channels["discord"] = ChannelSnapshot(
                volume=prev.volume if prev else 100,
                muted=prev.muted if prev else False,
                label="DISCORD",
                process_name=None,
            )

        game_sess = self._pick_game_session(sessions, used)
        if game_sess is not None:
            self._resolved_processes["game"] = game_sess.process_name
            channels["game"] = ChannelSnapshot(
                volume=game_sess.volume,
                muted=game_sess.muted,
                label=_short_label(game_sess.process_name, "JOGO"),
                process_name=game_sess.process_name,
            )
        else:
            self._resolved_processes["game"] = None
            prev = self._last_channels.get("game")
            channels["game"] = ChannelSnapshot(
                volume=prev.volume if prev else 100,
                muted=prev.muted if prev else False,
                label="JOGO",
                process_name=None,
            )

        mic_mute = False
        if audio.has_microphone():
            try:
                mic_mute = audio.get_mic_mute()
            except (RuntimeError, COMError, OSError):
                audio.refresh_devices()
                mic_mute = False

        return channels, mic_mute

    # ------------------------------------------------------------------
    # Envio e Sincronização PC -> ESP32 (WebSocket)
    # ------------------------------------------------------------------

    async def _send_json(self, ws, payload: dict) -> None:
        text = json.dumps(payload, separators=(",", ":"))
        await ws.send(text)
        if self.verbose:
            self.log.debug("[PC -> ESP32] %s", text)

    async def _sync_to_esp32(self, ws, audio: AudioController, force_all: bool = False) -> None:
        channels, mic_mute = self._read_windows_state(audio)
        now = time.monotonic()

        for target, snap in channels.items():
            prev = self._last_channels.get(target)
            recently_written = (
                now - self._last_esp_write_ts.get(target, 0.0)
            ) < RECENT_ESP_WRITE_GRACE_S

            if force_all or prev is None or prev.label != snap.label:
                await self._send_json(ws, {"sync": "label", "target": target, "val": snap.label})

            if force_all or (not recently_written and (prev is None or prev.volume != snap.volume)):
                await self._send_json(ws, {"sync": "vol", "target": target, "val": snap.volume})
                if not force_all and prev is not None:
                    self.log.info(
                        "[Windows -> WiFi] %s (%s) volume = %d%%",
                        target,
                        snap.label,
                        snap.volume,
                    )

            if force_all or (not recently_written and (prev is None or prev.muted != snap.muted)):
                await self._send_json(ws, {"sync": "mute", "target": target, "val": snap.muted})
                if not force_all and prev is not None:
                    self.log.info(
                        "[Windows -> WiFi] %s (%s) mute = %s",
                        target,
                        snap.label,
                        "MUTADO" if snap.muted else "ATIVO",
                    )

        mic_recently_written = (
            now - self._last_esp_write_ts.get("mic", 0.0)
        ) < RECENT_ESP_WRITE_GRACE_S
        if force_all or (not mic_recently_written and self._last_mic_mute != mic_mute):
            await self._send_json(ws, {"sync": "mute", "target": "mic", "val": mic_mute})
            if not force_all and self._last_mic_mute is not None:
                self.log.info(
                    "[Windows -> WiFi] mic = %s",
                    "MUTADO" if mic_mute else "ATIVO",
                )

        self._last_channels = channels
        self._last_mic_mute = mic_mute

    # ------------------------------------------------------------------
    # Aplicação de Eventos ESP32 -> Windows
    # ------------------------------------------------------------------

    async def _handle_esp_event(self, ws, audio: AudioController, msg: dict) -> None:
        event = str(msg.get("event", "")).lower()
        target = str(msg.get("target", "")).lower()

        if event == "pong":
            return

        if event == "ready":
            self.log.info("[ESP32 WiFi] Placa pronta -> enviando sincronização completa...")
            await self._sync_to_esp32(ws, audio, force_all=True)
            return

        if event == "set_vol" and target:
            try:
                val = max(0, min(100, int(msg.get("val", 0))))
            except (TypeError, ValueError):
                return

            self._last_esp_write_ts[target] = time.monotonic()

            if target == "master":
                applied = audio.set_master_volume(val)
                if "master" in self._last_channels:
                    self._last_channels["master"].volume = applied
                self.log.info("[ESP32 WiFi -> Windows] Master volume = %d%%", applied)
                return

            proc_name = self._resolved_processes.get(target)
            if proc_name:
                ok = audio.set_app_volume(proc_name, val)
                if ok and target in self._last_channels:
                    self._last_channels[target].volume = val
                    self.log.info(
                        "[ESP32 WiFi -> Windows] %s (%s) volume = %d%%",
                        target,
                        proc_name,
                        val,
                    )
                else:
                    self.log.info(
                        "[ESP32 WiFi -> Windows] Sessão '%s' indisponível ao ajustar volume.",
                        proc_name,
                    )
            else:
                if target in self._last_channels:
                    self._last_channels[target].volume = val
                self.log.info(
                    "[ESP32 WiFi -> Windows] %s: nenhum app aberto (valor %d%%).",
                    target,
                    val,
                )
            return

        if event == "toggle_mute" and target:
            self._last_esp_write_ts[target] = time.monotonic()

            if target == "master":
                new_state = audio.toggle_master_mute()
                if "master" in self._last_channels:
                    self._last_channels["master"].muted = new_state
                await self._send_json(ws, {"sync": "mute", "target": "master", "val": new_state})
                self.log.info(
                    "[ESP32 WiFi -> Windows] Master mute = %s",
                    "MUTADO" if new_state else "ATIVO",
                )
                return

            if target == "mic":
                if not audio.has_microphone():
                    self.log.warning("[ESP32 WiFi -> Windows] Nenhum microfone padrão detectado.")
                    return
                new_state = audio.toggle_mic_mute()
                self._last_mic_mute = new_state
                await self._send_json(ws, {"sync": "mute", "target": "mic", "val": new_state})
                self.log.info(
                    "[ESP32 WiFi -> Windows] Microfone = %s",
                    "MUTADO" if new_state else "ATIVO",
                )
                return

            proc_name = self._resolved_processes.get(target)
            if proc_name:
                new_state = audio.toggle_app_mute(proc_name)
                if new_state is not None:
                    if target in self._last_channels:
                        self._last_channels[target].muted = new_state
                    await self._send_json(ws, {"sync": "mute", "target": target, "val": new_state})
                    self.log.info(
                        "[ESP32 WiFi -> Windows] %s (%s) mute = %s",
                        target,
                        proc_name,
                        "MUTADO" if new_state else "ATIVO",
                    )
            else:
                self.log.info(
                    "[ESP32 WiFi -> Windows] %s: nenhum app mapeado para mute.", target
                )

    # ------------------------------------------------------------------
    # Sessão WebSocket e Loop de Reconexão Contínua
    # ------------------------------------------------------------------

    async def _run_ws_session(
        self,
        ws,
        audio: AudioController,
        deadline: Optional[float],
    ) -> None:
        await self._sync_to_esp32(ws, audio, force_all=True)

        for target, snap in self._last_channels.items():
            proc_info = snap.process_name or "aguardando app"
            mute_txt = "MUTADO" if snap.muted else "OK"
            self.log.info(
                "  Canal %-8s [%-8s] -> %-22s | Vol: %3d%% | Mute: %s",
                target,
                snap.label,
                proc_info,
                snap.volume,
                mute_txt,
            )
        self.log.info(
            "  Microfone      -> %s",
            "MUTADO" if self._last_mic_mute else "ATIVO",
        )
        self.log.info("Bridge Wi-Fi ativa! Controle 100% sem fio pronto.")

        async def rx_task() -> None:
            async for raw in ws:
                if isinstance(raw, bytes):
                    raw = raw.decode("utf-8", errors="replace")
                text = raw.strip()
                if not text:
                    continue
                if self.verbose:
                    self.log.debug("[ESP32 -> PC] %s", text)
                if not text.startswith("{"):
                    continue
                try:
                    msg = json.loads(text)
                except json.JSONDecodeError:
                    continue
                if isinstance(msg, dict):
                    await self._handle_esp_event(ws, audio, msg)

        async def poll_task() -> None:
            last_ping = time.monotonic()
            while True:
                if deadline is not None and time.monotonic() >= deadline:
                    return
                await asyncio.sleep(POLL_INTERVAL_S)
                await self._sync_to_esp32(ws, audio, force_all=False)
                now = time.monotonic()
                if (now - last_ping) >= APP_PING_INTERVAL_S:
                    last_ping = now
                    await self._send_json(ws, {"cmd": "ping"})

        t_rx = asyncio.create_task(rx_task())
        t_poll = asyncio.create_task(poll_task())
        try:
            done, pending = await asyncio.wait(
                {t_rx, t_poll},
                return_when=asyncio.FIRST_COMPLETED,
            )
            for task in pending:
                task.cancel()
            await asyncio.gather(*pending, return_exceptions=True)
            for task in done:
                exc = task.exception()
                if exc is not None:
                    raise exc
        finally:
            if not t_rx.done():
                t_rx.cancel()
            if not t_poll.done():
                t_poll.cancel()

    async def run_async(self, duration_seconds: Optional[float] = None) -> None:
        self.log.info("============================================================")
        self.log.info("  Mini Deck IoT - Wi-Fi WebSocket Bridge (100% Sem Fio)")
        self.log.info("============================================================")

        deadline = (time.monotonic() + duration_seconds) if duration_seconds is not None else None
        last_endpoint: Optional[tuple[str, int]] = None

        with AudioController() as audio:
            self.log.info("Saída padrão : %s", audio.get_master_device_name() or "N/A")
            self.log.info("Microfone    : %s", audio.get_mic_device_name() or "N/A")

            while True:
                if deadline is not None and time.monotonic() >= deadline:
                    break

                # 1. Descobre o IP do ESP32 via UDP 4210 (ou usa último IP conhecido / fixo)
                if last_endpoint is None:
                    remaining = (deadline - time.monotonic()) if deadline is not None else None
                    if remaining is not None and remaining <= 0:
                        break
                    endpoint = await self.discover_device(timeout=remaining)
                    if endpoint is None:
                        break
                else:
                    endpoint = last_endpoint

                ip, port = endpoint
                uri = f"ws://{ip}:{port}"
                self.log.info("Conectando via WebSocket em %s ...", uri)

                try:
                    async with websockets.connect(
                        uri,
                        open_timeout=4.0,
                        ping_interval=3.0,
                        ping_timeout=3.0,
                        close_timeout=1.0,
                    ) as ws:
                        last_endpoint = endpoint
                        self.log.info("Conectado com sucesso em %s!", uri)
                        await self._run_ws_session(ws, audio, deadline)

                        if deadline is not None and time.monotonic() >= deadline:
                            break
                        self.log.warning("Conexão WebSocket encerrada pelo ESP32.")
                except (WebSocketException, OSError, asyncio.TimeoutError) as exc:
                    self.log.warning(
                        "Falha/queda na conexão com %s (%s). Redescobrindo em %.0fs...",
                        uri,
                        exc,
                        RECONNECT_DELAY_S,
                    )
                    # Limpa IP em cache se não for fixo, para acompanhar eventual mudança de DHCP
                    if not self.fixed_host:
                        last_endpoint = None

                if deadline is not None and time.monotonic() >= deadline:
                    break
                await asyncio.sleep(RECONNECT_DELAY_S)

        self.log.info("Wi-Fi Bridge finalizada com segurança.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Bridge Wi-Fi WebSocket + Auto-Discovery UDP do Mini Deck IoT."
    )
    parser.add_argument(
        "--udp-port",
        type=int,
        default=DEFAULT_UDP_PORT,
        help=f"Porta UDP de auto-descoberta (padrão: {DEFAULT_UDP_PORT})",
    )
    parser.add_argument(
        "--host",
        default=None,
        help="Opcional: IP fixo do ESP32 (pula Auto-Discovery UDP)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=DEFAULT_WS_PORT,
        help=f"Porta WebSocket do ESP32 quando --host for usado (padrão: {DEFAULT_WS_PORT})",
    )
    parser.add_argument("--verbose", action="store_true", help="Exibe todos os pacotes JSON TX/RX")
    parser.add_argument(
        "--duration",
        type=float,
        default=None,
        help="Opcional: executa por N segundos e encerra (testes automatizados)",
    )
    parser.add_argument(
        "--allow-multiple",
        action="store_true",
        help="Permite iniciar mesmo se outra instância já estiver em execução",
    )
    return parser.parse_args()


def main() -> None:
    if sys.stdout is not None and hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    args = parse_args()
    logger = _setup_logger(verbose=args.verbose)

    mutex_handle = None
    if not args.allow_multiple:
        mutex_handle = _acquire_single_instance_mutex()
        if mutex_handle is False:
            logger.info("Outra instância da Mini Deck Wi-Fi Bridge já está em execução. Saindo.")
            return

    bridge = MiniDeckWiFiBridge(
        udp_port=args.udp_port,
        fixed_host=args.host,
        fixed_port=args.port,
        verbose=args.verbose,
        logger=logger,
    )

    try:
        asyncio.run(bridge.run_async(duration_seconds=args.duration))
    except KeyboardInterrupt:
        logger.info("Encerrando Wi-Fi Bridge (Ctrl+C)...")
    finally:
        _release_mutex(mutex_handle)


if __name__ == "__main__":
    main()

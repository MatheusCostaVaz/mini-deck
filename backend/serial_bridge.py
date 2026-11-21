"""Bridge Serial resiliente entre o ESP32-S3 (Mini Deck IoT) e o Windows 11.

Recursos:
- Reconexão automática na porta serial (tenta reconectar a cada 2s em caso de queda/desconexão)
- Reenvio automático de todos os volumes, labels e status do microfone ao reconectar
- Thread de Heartbeat enviando {"cmd":"ping"} a cada 1 segundo
- Compatível com execução silenciosa via pythonw.exe (log em backend/bridge.log)
- Proteção contra instâncias duplicadas via Mutex do Windows

Protocolo JSON (uma mensagem por linha terminada em '\\n', 115200 baud):
  PC -> ESP32:
    {"cmd":"ping"}
    {"sync":"vol","target":"master","val":50}
    {"sync":"mute","target":"mic","val":true}
    {"sync":"label","target":"game","val":"STEAM"}

  ESP32 -> PC:
    {"event":"pong"}
    {"event":"set_vol","target":"master","val":80}
    {"event":"toggle_mute","target":"mic"}
    {"event":"ready",...}
"""

from __future__ import annotations

import argparse
import ctypes
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import queue
import sys
import threading
import time
from dataclasses import dataclass
from typing import Optional

from _ctypes import COMError
import serial

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

POLL_INTERVAL_S = 0.25
HEARTBEAT_INTERVAL_S = 1.0
RECONNECT_DELAY_S = 2.0
RECENT_ESP_WRITE_GRACE_S = 0.35

BACKEND_DIR = Path(__file__).resolve().parent
LOG_PATH = BACKEND_DIR / "bridge.log"
MUTEX_NAME = "Local\\MiniDeckIoT_SerialBridge_Mutex"


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
    logger = logging.getLogger("mini_deck_bridge")
    logger.setLevel(logging.DEBUG if verbose else logging.INFO)
    logger.handlers.clear()

    fmt = logging.Formatter("%(asctime)s [%(levelname)s] %(message)s", datefmt="%H:%M:%S")

    # Log em arquivo para diagnóstico quando rodando silenciosamente via pythonw.exe
    try:
        file_handler = RotatingFileHandler(
            LOG_PATH, maxBytes=512 * 1024, backupCount=1, encoding="utf-8"
        )
        file_handler.setFormatter(fmt)
        logger.addHandler(file_handler)
    except OSError:
        pass

    # Log no console apenas quando stdout estiver disponível (evita erro no pythonw.exe)
    if sys.stdout is not None:
        stream_handler = logging.StreamHandler(sys.stdout)
        stream_handler.setFormatter(fmt)
        logger.addHandler(stream_handler)

    return logger


def _acquire_single_instance_mutex():
    """Garante instância única no Windows para evitar disputa pela COM8."""
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


class MiniDeckBridge:
    """Gerencia reconexão serial automática, heartbeat e sincronização com AudioController."""

    def __init__(
        self,
        port: str = "COM8",
        baudrate: int = 115200,
        verbose: bool = False,
        logger: Optional[logging.Logger] = None,
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.verbose = verbose
        self.log = logger or _setup_logger(verbose)

        self._ser: Optional[serial.Serial] = None
        self._tx_lock = threading.Lock()
        self._rx_queue: queue.Queue[dict] = queue.Queue()
        self._shutdown_event = threading.Event()
        self._conn_lost_event = threading.Event()

        self._last_channels: dict[str, ChannelSnapshot] = {}
        self._last_mic_mute: Optional[bool] = None
        self._last_esp_write_ts: dict[str, float] = {}
        self._last_pong_ts: float = 0.0
        self._resolved_processes: dict[str, Optional[str]] = {
            "browser": None,
            "discord": None,
            "game": None,
        }

    # ------------------------------------------------------------------
    # Serial I/O e Threads de Sessão
    # ------------------------------------------------------------------

    def _open_serial(self) -> serial.Serial:
        ser = serial.Serial()
        ser.port = self.port
        ser.baudrate = self.baudrate
        ser.timeout = 0.15
        ser.write_timeout = 1.0
        # Evita resetar o ESP32-S3 (USB-Serial/JTAG) ao abrir a porta
        ser.dtr = False
        ser.rts = False
        ser.open()
        return ser

    def _send_json(self, payload: dict, log_tx: bool = True) -> bool:
        ser = self._ser
        if ser is None or not ser.is_open:
            self._conn_lost_event.set()
            return False

        line = json.dumps(payload, separators=(",", ":")) + "\n"
        try:
            with self._tx_lock:
                ser.write(line.encode("utf-8"))
                ser.flush()
        except (serial.SerialException, OSError) as exc:
            if not self._conn_lost_event.is_set():
                self.log.warning("Falha ao enviar para %s: %s", self.port, exc)
                self._conn_lost_event.set()
            return False

        if self.verbose and log_tx:
            self.log.debug("[PC -> ESP32] %s", line.strip())
        return True

    def _reader_loop(self) -> None:
        while not self._shutdown_event.is_set() and not self._conn_lost_event.is_set():
            ser = self._ser
            if ser is None or not ser.is_open:
                self._conn_lost_event.set()
                break

            try:
                raw = ser.readline()
            except (serial.SerialException, OSError) as exc:
                if not self._shutdown_event.is_set():
                    self.log.warning("Conexão perdida na leitura de %s: %s", self.port, exc)
                self._conn_lost_event.set()
                break

            if not raw:
                continue

            text = raw.decode("utf-8", errors="replace").strip()
            if not text:
                continue

            if not text.startswith("{"):
                if self.verbose:
                    self.log.debug("[ESP32 -> PC] %s", text)
                continue

            try:
                msg = json.loads(text)
            except json.JSONDecodeError:
                continue

            if isinstance(msg, dict):
                if msg.get("event") == "pong":
                    self._last_pong_ts = time.monotonic()
                    if self.verbose:
                        self.log.debug("[ESP32 -> PC] %s", text)
                    continue

                if self.verbose:
                    self.log.debug("[ESP32 -> PC] %s", text)
                self._rx_queue.put(msg)

    def _heartbeat_loop(self) -> None:
        """Envia {"cmd":"ping"} a cada 1 segundo para manter o status sincronizado no ESP32."""
        while not self._shutdown_event.is_set() and not self._conn_lost_event.is_set():
            if not self._send_json({"cmd": "ping"}, log_tx=self.verbose):
                break
            # Espera 1 segundo (acorda imediatamente se a conexão cair ou serviço encerrar)
            if self._conn_lost_event.wait(timeout=HEARTBEAT_INTERVAL_S):
                break
            if self._shutdown_event.is_set():
                break

    def _drain_rx_queue(self) -> None:
        while True:
            try:
                self._rx_queue.get_nowait()
            except queue.Empty:
                break

    # ------------------------------------------------------------------
    # Resolução de Sessões do Windows para as 4 Colunas
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

        # Coluna 2: Browser (Edge / Chrome / Firefox...)
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

        # Coluna 3: Discord / Chat
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

        # Coluna 4: Geral / Jogo
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
    # Sincronização PC -> ESP32
    # ------------------------------------------------------------------

    def _sync_to_esp32(self, audio: AudioController, force_all: bool = False) -> None:
        channels, mic_mute = self._read_windows_state(audio)
        now = time.monotonic()

        def send_sync(payload: dict) -> bool:
            ok = self._send_json(payload)
            if ok and force_all:
                # Pequeno intervalo no burst inicial para não estourar o buffer RX USB-CDC (256B)
                time.sleep(0.015)
            return ok

        for target, snap in channels.items():
            prev = self._last_channels.get(target)
            recently_written = (
                now - self._last_esp_write_ts.get(target, 0.0)
            ) < RECENT_ESP_WRITE_GRACE_S

            if force_all or prev is None or prev.label != snap.label:
                if not send_sync({"sync": "label", "target": target, "val": snap.label}):
                    return

            if force_all or (not recently_written and (prev is None or prev.volume != snap.volume)):
                if not send_sync({"sync": "vol", "target": target, "val": snap.volume}):
                    return
                if not force_all and prev is not None:
                    self.log.info(
                        "[Windows -> ESP32] %s (%s) volume = %d%%",
                        target,
                        snap.label,
                        snap.volume,
                    )

            if force_all or (not recently_written and (prev is None or prev.muted != snap.muted)):
                if not send_sync({"sync": "mute", "target": target, "val": snap.muted}):
                    return
                if not force_all and prev is not None:
                    self.log.info(
                        "[Windows -> ESP32] %s (%s) mute = %s",
                        target,
                        snap.label,
                        "MUTADO" if snap.muted else "ATIVO",
                    )

        mic_recently_written = (
            now - self._last_esp_write_ts.get("mic", 0.0)
        ) < RECENT_ESP_WRITE_GRACE_S
        if force_all or (not mic_recently_written and self._last_mic_mute != mic_mute):
            if not send_sync({"sync": "mute", "target": "mic", "val": mic_mute}):
                return
            if not force_all and self._last_mic_mute is not None:
                self.log.info(
                    "[Windows -> ESP32] mic = %s",
                    "MUTADO" if mic_mute else "ATIVO",
                )

        self._last_channels = channels
        self._last_mic_mute = mic_mute

    # ------------------------------------------------------------------
    # Aplicação de Eventos ESP32 -> Windows
    # ------------------------------------------------------------------

    def _handle_esp_event(self, audio: AudioController, msg: dict) -> None:
        event = str(msg.get("event", "")).lower()
        target = str(msg.get("target", "")).lower()

        if event == "ready":
            self.log.info("[ESP32] Placa pronta/reiniciada -> reenviando sincronização completa...")
            self._sync_to_esp32(audio, force_all=True)
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
                self.log.info("[ESP32 -> Windows] Master volume = %d%%", applied)
                return

            proc_name = self._resolved_processes.get(target)
            if proc_name:
                ok = audio.set_app_volume(proc_name, val)
                if ok and target in self._last_channels:
                    self._last_channels[target].volume = val
                    self.log.info("[ESP32 -> Windows] %s (%s) volume = %d%%", target, proc_name, val)
                else:
                    self.log.info(
                        "[ESP32 -> Windows] Sessão '%s' indisponível ao ajustar volume.", proc_name
                    )
            else:
                if target in self._last_channels:
                    self._last_channels[target].volume = val
                self.log.info(
                    "[ESP32 -> Windows] %s: nenhum app mapeado aberto (valor %d%%).", target, val
                )
            return

        if event == "toggle_mute" and target:
            self._last_esp_write_ts[target] = time.monotonic()

            if target == "master":
                new_state = audio.toggle_master_mute()
                if "master" in self._last_channels:
                    self._last_channels["master"].muted = new_state
                self._send_json({"sync": "mute", "target": "master", "val": new_state})
                self.log.info(
                    "[ESP32 -> Windows] Master mute = %s",
                    "MUTADO" if new_state else "ATIVO",
                )
                return

            if target == "mic":
                if not audio.has_microphone():
                    self.log.warning("[ESP32 -> Windows] Nenhum microfone padrão detectado.")
                    return
                new_state = audio.toggle_mic_mute()
                self._last_mic_mute = new_state
                self._send_json({"sync": "mute", "target": "mic", "val": new_state})
                self.log.info(
                    "[ESP32 -> Windows] Microfone = %s",
                    "MUTADO" if new_state else "ATIVO",
                )
                return

            proc_name = self._resolved_processes.get(target)
            if proc_name:
                new_state = audio.toggle_app_mute(proc_name)
                if new_state is not None:
                    if target in self._last_channels:
                        self._last_channels[target].muted = new_state
                    self._send_json({"sync": "mute", "target": target, "val": new_state})
                    self.log.info(
                        "[ESP32 -> Windows] %s (%s) mute = %s",
                        target,
                        proc_name,
                        "MUTADO" if new_state else "ATIVO",
                    )
            else:
                self.log.info("[ESP32 -> Windows] %s: nenhum app mapeado para mute.", target)

    # ------------------------------------------------------------------
    # Ciclo de Vida com Reconexão Automática
    # ------------------------------------------------------------------

    def _run_connected_session(
        self,
        audio: AudioController,
        start_ts: float,
        duration_seconds: Optional[float],
    ) -> None:
        self._conn_lost_event.clear()
        self._drain_rx_queue()
        self._last_pong_ts = time.monotonic()

        reader_thread = threading.Thread(
            target=self._reader_loop, name="serial-reader", daemon=True
        )
        heartbeat_thread = threading.Thread(
            target=self._heartbeat_loop, name="serial-heartbeat", daemon=True
        )
        reader_thread.start()
        heartbeat_thread.start()

        try:
            # Reenvio completo de todos os volumes, labels e microfone ao conectar/reconectar
            self._sync_to_esp32(audio, force_all=True)
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
            self.log.info("Bridge conectada em %s (heartbeat a cada %.0fs).", self.port, HEARTBEAT_INTERVAL_S)

            last_poll = time.monotonic()

            while not self._shutdown_event.is_set() and not self._conn_lost_event.is_set():
                if duration_seconds is not None and (time.monotonic() - start_ts) >= duration_seconds:
                    self._shutdown_event.set()
                    break

                # 1. Processa eventos recebidos do ESP32 imediatamente
                while True:
                    try:
                        msg = self._rx_queue.get_nowait()
                    except queue.Empty:
                        break
                    self._handle_esp_event(audio, msg)

                # 2. Monitora alterações feitas no Windows e sincroniza a tela
                now = time.monotonic()
                if (now - last_poll) >= POLL_INTERVAL_S:
                    last_poll = now
                    self._sync_to_esp32(audio, force_all=False)

                time.sleep(0.01)
        finally:
            self._conn_lost_event.set()
            reader_thread.join(timeout=1.5)
            heartbeat_thread.join(timeout=1.5)
            if self._ser is not None:
                try:
                    if self._ser.is_open:
                        self._ser.close()
                except OSError:
                    pass
                self._ser = None

    def run(self, duration_seconds: Optional[float] = None) -> None:
        self.log.info("============================================================")
        self.log.info("  Mini Deck IoT - Serial Bridge (%s @ %d baud)", self.port, self.baudrate)
        self.log.info("============================================================")

        self._shutdown_event.clear()
        start_ts = time.monotonic()
        waiting_logged = False

        try:
            with AudioController() as audio:
                self.log.info("Saída padrão : %s", audio.get_master_device_name() or "N/A")
                self.log.info("Microfone    : %s", audio.get_mic_device_name() or "N/A")

                while not self._shutdown_event.is_set():
                    if duration_seconds is not None and (time.monotonic() - start_ts) >= duration_seconds:
                        break

                    try:
                        self._ser = self._open_serial()
                        waiting_logged = False
                        self.log.info("Conectado com sucesso em %s!", self.port)
                    except (serial.SerialException, OSError) as exc:
                        if not waiting_logged:
                            self.log.warning(
                                "Porta %s indisponível (%s). Tentando reconectar a cada %.0fs...",
                                self.port,
                                exc,
                                RECONNECT_DELAY_S,
                            )
                            waiting_logged = True
                        if self._shutdown_event.wait(timeout=RECONNECT_DELAY_S):
                            break
                        continue

                    self._run_connected_session(audio, start_ts, duration_seconds)

                    if not self._shutdown_event.is_set():
                        self.log.warning(
                            "Desconectado de %s. Aguardando %.0fs para reconectar...",
                            self.port,
                            RECONNECT_DELAY_S,
                        )
                        if self._shutdown_event.wait(timeout=RECONNECT_DELAY_S):
                            break

        except KeyboardInterrupt:
            self.log.info("Encerrando Serial Bridge (Ctrl+C)...")
        finally:
            self._shutdown_event.set()
            self._conn_lost_event.set()
            if self._ser is not None:
                try:
                    if self._ser.is_open:
                        self._ser.close()
                except OSError:
                    pass
                self._ser = None
            self.log.info("Serial Bridge finalizada com segurança.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Bridge Serial resiliente do Mini Deck IoT (Windows 11 <-> ESP32-S3)."
    )
    parser.add_argument("--port", default="COM8", help="Porta serial do ESP32 (padrão: COM8)")
    parser.add_argument("--baud", type=int, default=115200, help="Baudrate serial (padrão: 115200)")
    parser.add_argument("--verbose", action="store_true", help="Exibe todos os pacotes JSON TX/RX")
    parser.add_argument(
        "--duration",
        type=float,
        default=None,
        help="Opcional: executa por N segundos e encerra (útil para testes automatizados)",
    )
    parser.add_argument(
        "--allow-multiple",
        action="store_true",
        help="Permite iniciar mesmo se outra instância da bridge já estiver ativa",
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
            logger.info("Outra instância da Mini Deck Serial Bridge já está em execução. Saindo.")
            return

    try:
        bridge = MiniDeckBridge(
            port=args.port,
            baudrate=args.baud,
            verbose=args.verbose,
            logger=logger,
        )
        bridge.run(duration_seconds=args.duration)
    finally:
        _release_mutex(mutex_handle)


if __name__ == "__main__":
    main()

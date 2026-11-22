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
import shutil
import socket
import subprocess
import sys
import time
import webbrowser
from dataclasses import dataclass
from typing import Optional

from _ctypes import COMError
import psutil
import websockets
from websockets.exceptions import WebSocketException

from audio_controller import AudioAppSession, AudioController
from media_controller import MediaController, MediaSnapshot

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
DEFAULT_CS2_GSI_PORT = 3000
CS2_C4_TOTAL_SECONDS = 40
POLL_INTERVAL_S = 0.25
STATS_POLL_TICKS = 6  # 6 * 0.25s = 1.5s
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


class _NvmlUtilization(ctypes.Structure):
    _fields_ = [("gpu", ctypes.c_uint), ("memory", ctypes.c_uint)]


class _PdhFmtCounterValue(ctypes.Structure):
    _fields_ = [
        ("CStatus", ctypes.c_uint32),
        ("_pad", ctypes.c_uint32),
        ("doubleValue", ctypes.c_double),
    ]


class _PdhFmtCounterValueItemW(ctypes.Structure):
    _fields_ = [
        ("szName", ctypes.c_wchar_p),
        ("FmtValue", _PdhFmtCounterValue),
    ]


class GpuMonitor:
    """Leitor leve e não bloqueante de utilização de GPU (%) no Windows.

    Ordem de prioridade:
      1. NVIDIA NVML (nvml.dll via ctypes) -> < 1 ms em memória, sem abrir subprocessos.
      2. nvidia-smi assíncrono (CREATE_NO_WINDOW) -> fallback para GPUs NVIDIA.
      3. Windows PDH (\\GPU Engine(*)\\Utilization Percentage) -> Intel/AMD integrada ou dedicada.
      4. Fallback seguro -> 0%.
    """

    def __init__(self, logger: logging.Logger) -> None:
        self._log = logger
        self.backend = "none"
        self.gpu_name = "N/A"

        # Estado NVML
        self._nvml = None
        self._nvml_dev = None

        # Estado nvidia-smi
        self._nvidia_smi_path: Optional[str] = None

        # Estado Windows PDH
        self._pdh = None
        self._pdh_query = None
        self._pdh_counter = None

        self._init_backend()

    def _init_backend(self) -> None:
        if self._try_init_nvml():
            return
        smi = shutil.which("nvidia-smi")
        if smi:
            self._nvidia_smi_path = smi
            self.backend = "nvidia-smi"
            self.gpu_name = "NVIDIA GPU"
            return
        if self._try_init_pdh():
            return
        self.backend = "none"

    def _try_init_nvml(self) -> bool:
        if os.name != "nt":
            return False
        try:
            nvml = ctypes.WinDLL("nvml.dll")
            nvml.nvmlInit_v2.restype = ctypes.c_int
            nvml.nvmlShutdown.restype = ctypes.c_int
            nvml.nvmlDeviceGetHandleByIndex_v2.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
            nvml.nvmlDeviceGetHandleByIndex_v2.restype = ctypes.c_int
            nvml.nvmlDeviceGetUtilizationRates.argtypes = [ctypes.c_void_p, ctypes.POINTER(_NvmlUtilization)]
            nvml.nvmlDeviceGetUtilizationRates.restype = ctypes.c_int
            nvml.nvmlDeviceGetTemperature.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint)]
            nvml.nvmlDeviceGetTemperature.restype = ctypes.c_int
            nvml.nvmlDeviceGetName.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint]
            nvml.nvmlDeviceGetName.restype = ctypes.c_int

            if nvml.nvmlInit_v2() != 0:
                return False
            dev = ctypes.c_void_p()
            if nvml.nvmlDeviceGetHandleByIndex_v2(0, ctypes.byref(dev)) != 0:
                nvml.nvmlShutdown()
                return False

            name_buf = ctypes.create_string_buffer(96)
            if nvml.nvmlDeviceGetName(dev, name_buf, len(name_buf)) == 0:
                self.gpu_name = name_buf.value.decode("utf-8", errors="replace").strip() or "NVIDIA GPU"
            else:
                self.gpu_name = "NVIDIA GPU"

            self._nvml = nvml
            self._nvml_dev = dev
            self.backend = "nvml"
            return True
        except (OSError, AttributeError):
            return False

    def _try_init_pdh(self) -> bool:
        if os.name != "nt":
            return False
        try:
            pdh = ctypes.WinDLL("pdh.dll")
            pdh.PdhOpenQueryW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
            pdh.PdhOpenQueryW.restype = ctypes.c_uint32
            pdh.PdhAddEnglishCounterW.argtypes = [
                ctypes.c_void_p,
                ctypes.c_wchar_p,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_void_p),
            ]
            pdh.PdhAddEnglishCounterW.restype = ctypes.c_uint32
            pdh.PdhCollectQueryData.argtypes = [ctypes.c_void_p]
            pdh.PdhCollectQueryData.restype = ctypes.c_uint32
            pdh.PdhGetFormattedCounterArrayW.argtypes = [
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.POINTER(ctypes.c_uint32),
                ctypes.POINTER(ctypes.c_uint32),
                ctypes.c_void_p,
            ]
            pdh.PdhGetFormattedCounterArrayW.restype = ctypes.c_uint32
            pdh.PdhCloseQuery.argtypes = [ctypes.c_void_p]
            pdh.PdhCloseQuery.restype = ctypes.c_uint32

            query = ctypes.c_void_p()
            if pdh.PdhOpenQueryW(None, None, ctypes.byref(query)) != 0:
                return False

            counter = ctypes.c_void_p()
            status = pdh.PdhAddEnglishCounterW(
                query,
                r"\GPU Engine(*)\Utilization Percentage",
                None,
                ctypes.byref(counter),
            )
            if status != 0:
                pdh.PdhCloseQuery(query)
                return False

            # Primeira coleta inicializa a janela de amostragem do contador
            pdh.PdhCollectQueryData(query)
            self._pdh = pdh
            self._pdh_query = query
            self._pdh_counter = counter
            self.backend = "pdh"
            self.gpu_name = "Windows GPU (PDH)"
            return True
        except (OSError, AttributeError):
            return False

    def _read_pdh_sync(self) -> int:
        if not self._pdh or not self._pdh_query or not self._pdh_counter:
            return 0
        try:
            if self._pdh.PdhCollectQueryData(self._pdh_query) != 0:
                return 0
            PDH_FMT_DOUBLE = 0x00000200
            PDH_MORE_DATA = 0x800007D2
            buf_size = ctypes.c_uint32(0)
            item_count = ctypes.c_uint32(0)
            status = self._pdh.PdhGetFormattedCounterArrayW(
                self._pdh_counter,
                PDH_FMT_DOUBLE,
                ctypes.byref(buf_size),
                ctypes.byref(item_count),
                None,
            )
            if status != PDH_MORE_DATA or buf_size.value == 0:
                return 0

            raw_buf = (ctypes.c_byte * buf_size.value)()
            status = self._pdh.PdhGetFormattedCounterArrayW(
                self._pdh_counter,
                PDH_FMT_DOUBLE,
                ctypes.byref(buf_size),
                ctypes.byref(item_count),
                ctypes.byref(raw_buf),
            )
            if status != 0 or item_count.value == 0:
                return 0

            items = ctypes.cast(raw_buf, ctypes.POINTER(_PdhFmtCounterValueItemW))
            # Soma por tipo de engine (ex.: engtype_3D, engtype_VideoDecode) e pega o maior
            by_engine: dict[str, float] = {}
            for i in range(item_count.value):
                item = items[i]
                if item.FmtValue.CStatus != 0:
                    continue
                val = item.FmtValue.doubleValue
                if val <= 0.0:
                    continue
                name = item.szName or ""
                key = name.split("engtype_")[-1] if "engtype_" in name else "total"
                by_engine[key] = by_engine.get(key, 0.0) + val

            peak = max(by_engine.values(), default=0.0)
            return int(round(max(0.0, min(100.0, peak))))
        except (OSError, ValueError):
            return 0

    async def read_metrics(self) -> tuple[int, Optional[int]]:
        """Retorna (gpu_percent, gpu_temp_celsius_ou_None)."""
        if self.backend == "nvml" and self._nvml and self._nvml_dev:
            pct = 0
            temp_c: Optional[int] = None
            try:
                util = _NvmlUtilization()
                if self._nvml.nvmlDeviceGetUtilizationRates(self._nvml_dev, ctypes.byref(util)) == 0:
                    pct = int(max(0, min(100, util.gpu)))
            except OSError:
                pass
            try:
                raw_temp = ctypes.c_uint(0)
                # NVML_TEMPERATURE_GPU = 0
                if self._nvml.nvmlDeviceGetTemperature(self._nvml_dev, 0, ctypes.byref(raw_temp)) == 0:
                    if 0 < raw_temp.value < 150:
                        temp_c = int(raw_temp.value)
            except OSError:
                pass
            return pct, temp_c

        if self.backend == "nvidia-smi" and self._nvidia_smi_path:
            try:
                creationflags = 0x08000000 if os.name == "nt" else 0
                proc = await asyncio.create_subprocess_exec(
                    self._nvidia_smi_path,
                    "--query-gpu=utilization.gpu,temperature.gpu",
                    "--format=csv,noheader,nounits",
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.DEVNULL,
                    creationflags=creationflags,
                )
                stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=1.0)
                if proc.returncode == 0 and stdout:
                    first_line = stdout.decode("utf-8", errors="replace").splitlines()[0].strip()
                    parts = [p.strip() for p in first_line.split(",")]
                    pct = int(round(max(0.0, min(100.0, float(parts[0]))))) if parts else 0
                    temp_c = None
                    if len(parts) >= 2:
                        try:
                            val = int(round(float(parts[1])))
                            if 0 < val < 150:
                                temp_c = val
                        except ValueError:
                            temp_c = None
                    return pct, temp_c
            except (OSError, asyncio.TimeoutError, ValueError, IndexError):
                return 0, None
            return 0, None

        if self.backend == "pdh":
            return self._read_pdh_sync(), None

        return 0, None

    async def read_percent(self) -> int:
        pct, _ = await self.read_metrics()
        return pct

    def close(self) -> None:
        if self._nvml is not None:
            try:
                self._nvml.nvmlShutdown()
            except OSError:
                pass
            self._nvml = None
            self._nvml_dev = None
        if self._pdh is not None and self._pdh_query is not None:
            try:
                self._pdh.PdhCloseQuery(self._pdh_query)
            except OSError:
                pass
            self._pdh = None
            self._pdh_query = None
            self._pdh_counter = None


class CpuTempMonitor:
    """Leitor leve de temperatura de CPU (°C) no Windows sem exigir privilégios de Admin.

    Ordem de prioridade:
      1. Windows PDH (\\Thermal Zone Information(*)\\Temperature) -> ~2 ms, sem elevação.
      2. WMI (root\\LibreHardwareMonitor / root\\OpenHardwareMonitor / root\\wmi MSAcpi_ThermalZoneTemperature).
      3. Fallback seguro -> None.
    """

    def __init__(self, logger: logging.Logger) -> None:
        self._log = logger
        self.backend = "none"
        self._pdh = None
        self._pdh_query = None
        self._pdh_counter = None
        self._wmi_service = None
        self._wmi_mode = ""
        self._init_backend()

    def _init_backend(self) -> None:
        if self._try_init_pdh():
            return
        if self._try_init_wmi():
            return
        self.backend = "none"

    def _try_init_pdh(self) -> bool:
        if os.name != "nt":
            return False
        try:
            pdh = ctypes.WinDLL("pdh.dll")
            pdh.PdhOpenQueryW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
            pdh.PdhOpenQueryW.restype = ctypes.c_uint32
            pdh.PdhAddEnglishCounterW.argtypes = [
                ctypes.c_void_p,
                ctypes.c_wchar_p,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_void_p),
            ]
            pdh.PdhAddEnglishCounterW.restype = ctypes.c_uint32
            pdh.PdhCollectQueryData.argtypes = [ctypes.c_void_p]
            pdh.PdhCollectQueryData.restype = ctypes.c_uint32
            pdh.PdhGetFormattedCounterArrayW.argtypes = [
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.POINTER(ctypes.c_uint32),
                ctypes.POINTER(ctypes.c_uint32),
                ctypes.c_void_p,
            ]
            pdh.PdhGetFormattedCounterArrayW.restype = ctypes.c_uint32
            pdh.PdhCloseQuery.argtypes = [ctypes.c_void_p]
            pdh.PdhCloseQuery.restype = ctypes.c_uint32

            query = ctypes.c_void_p()
            if pdh.PdhOpenQueryW(None, None, ctypes.byref(query)) != 0:
                return False

            counter = ctypes.c_void_p()
            status = pdh.PdhAddEnglishCounterW(
                query,
                r"\Thermal Zone Information(*)\Temperature",
                None,
                ctypes.byref(counter),
            )
            if status != 0:
                pdh.PdhCloseQuery(query)
                return False

            self._pdh = pdh
            self._pdh_query = query
            self._pdh_counter = counter
            # Valida se há ao menos uma zona térmica acessível
            if self._read_pdh_temp() is not None:
                self.backend = "pdh"
                return True

            pdh.PdhCloseQuery(query)
            self._pdh = None
            self._pdh_query = None
            self._pdh_counter = None
            return False
        except (OSError, AttributeError):
            return False

    def _try_init_wmi(self) -> bool:
        if os.name != "nt":
            return False
        try:
            import comtypes.client

            for ns, mode in (
                ("winmgmts:\\\\.\\root\\LibreHardwareMonitor", "ohm"),
                ("winmgmts:\\\\.\\root\\OpenHardwareMonitor", "ohm"),
                ("winmgmts:\\\\.\\root\\wmi", "acpi"),
            ):
                try:
                    svc = comtypes.client.GetObject(ns)
                    self._wmi_service = svc
                    self._wmi_mode = mode
                    if self._read_wmi_temp() is not None:
                        self.backend = f"wmi-{mode}"
                        return True
                except Exception:
                    continue
        except Exception:
            pass
        self._wmi_service = None
        self._wmi_mode = ""
        return False

    def _read_pdh_temp(self) -> Optional[int]:
        if not self._pdh or not self._pdh_query or not self._pdh_counter:
            return None
        try:
            if self._pdh.PdhCollectQueryData(self._pdh_query) != 0:
                return None
            PDH_FMT_DOUBLE = 0x00000200
            PDH_MORE_DATA = 0x800007D2
            buf_size = ctypes.c_uint32(0)
            item_count = ctypes.c_uint32(0)
            status = self._pdh.PdhGetFormattedCounterArrayW(
                self._pdh_counter,
                PDH_FMT_DOUBLE,
                ctypes.byref(buf_size),
                ctypes.byref(item_count),
                None,
            )
            if status != PDH_MORE_DATA or buf_size.value == 0:
                return None

            raw_buf = (ctypes.c_byte * buf_size.value)()
            status = self._pdh.PdhGetFormattedCounterArrayW(
                self._pdh_counter,
                PDH_FMT_DOUBLE,
                ctypes.byref(buf_size),
                ctypes.byref(item_count),
                ctypes.byref(raw_buf),
            )
            if status != 0 or item_count.value == 0:
                return None

            items = ctypes.cast(raw_buf, ctypes.POINTER(_PdhFmtCounterValueItemW))
            valid_c: list[float] = []
            for i in range(item_count.value):
                item = items[i]
                if item.FmtValue.CStatus != 0:
                    continue
                val = item.FmtValue.doubleValue
                # Thermal Zone Information retorna Kelvin (ex.: 318.0 -> 44.85 °C)
                celsius = val - 273.15 if val > 150.0 else val
                if 10.0 <= celsius <= 125.0:
                    valid_c.append(celsius)
            if not valid_c:
                return None
            return int(round(max(valid_c)))
        except (OSError, ValueError):
            return None

    def _read_wmi_temp(self) -> Optional[int]:
        if self._wmi_service is None:
            return None
        try:
            if self._wmi_mode == "ohm":
                sensors = self._wmi_service.ExecQuery(
                    "SELECT Name, Value FROM Sensor WHERE SensorType='Temperature'"
                )
                cpu_vals: list[float] = []
                for s in sensors:
                    name = str(getattr(s, "Name", "") or "").lower()
                    if "cpu" in name or "package" in name or "core" in name:
                        val = float(getattr(s, "Value", 0.0) or 0.0)
                        if 10.0 <= val <= 125.0:
                            cpu_vals.append(val)
                return int(round(max(cpu_vals))) if cpu_vals else None

            if self._wmi_mode == "acpi":
                zones = self._wmi_service.ExecQuery(
                    "SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"
                )
                vals: list[float] = []
                for z in zones:
                    raw = float(getattr(z, "CurrentTemperature", 0.0) or 0.0)
                    celsius = (raw / 10.0) - 273.15
                    if 10.0 <= celsius <= 125.0:
                        vals.append(celsius)
                return int(round(max(vals))) if vals else None
        except Exception:
            return None
        return None

    def read_temperature(self) -> Optional[int]:
        if self.backend == "pdh":
            return self._read_pdh_temp()
        if self.backend.startswith("wmi-"):
            return self._read_wmi_temp()
        return None

    def close(self) -> None:
        if self._pdh is not None and self._pdh_query is not None:
            try:
                self._pdh.PdhCloseQuery(self._pdh_query)
            except OSError:
                pass
            self._pdh = None
            self._pdh_query = None
            self._pdh_counter = None
        self._wmi_service = None


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
        gsi_port: int = DEFAULT_CS2_GSI_PORT,
        verbose: bool = False,
        logger: Optional[logging.Logger] = None,
    ) -> None:
        self.udp_port = udp_port
        self.fixed_host = fixed_host
        self.fixed_port = fixed_port
        self.gsi_port = gsi_port
        self.verbose = verbose
        self.log = logger or _setup_logger(verbose)

        self._last_channels: dict[str, ChannelSnapshot] = {}
        self._last_mic_mute: Optional[bool] = None
        self._last_media: Optional[MediaSnapshot] = None
        self._media = MediaController()
        self._last_esp_write_ts: dict[str, float] = {}
        self._resolved_processes: dict[str, Optional[str]] = {
            "browser": None,
            "discord": None,
            "game": None,
        }
        # Estado do servidor local CS2 GSI (C4 Timer)
        self._active_ws = None
        self._gsi_server: Optional[asyncio.AbstractServer] = None
        self._c4_state: str = "idle"
        self._c4_time_left: int = 0
        self._c4_deadline: Optional[float] = None
        self._c4_task: Optional[asyncio.Task] = None

        # Inicializa o contador não-bloqueante de CPU do psutil e leitores térmicos/GPU
        psutil.cpu_percent(interval=None)
        self._gpu = GpuMonitor(self.log)
        self._cpu_temp = CpuTempMonitor(self.log)

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
        except (COMError, Exception):
            audio.refresh_devices()
            prev_master = self._last_channels.get("master")
            master_vol = prev_master.volume if prev_master else 100
            master_mute = prev_master.muted if prev_master else False

        channels["master"] = ChannelSnapshot(
            volume=master_vol,
            muted=master_mute,
            label="MASTER",
            process_name="master",
        )

        try:
            sessions = audio.list_audio_sessions(active_only=False, group_by_app=True)
        except (COMError, Exception):
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

        mic_mute = self._last_mic_mute if self._last_mic_mute is not None else False
        try:
            if audio.has_microphone():
                mic_mute = audio.get_mic_mute()
        except (COMError, Exception):
            audio.refresh_devices()

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

    async def _sync_browser_channel(self, ws, audio: AudioController) -> None:
        channels, _ = self._read_windows_state(audio)
        snap = channels.get("browser")
        if snap is None:
            return
        await self._send_json(ws, {"sync": "label", "target": "browser", "val": snap.label})
        await self._send_json(ws, {"sync": "vol", "target": "browser", "val": snap.volume})
        await self._send_json(ws, {"sync": "mute", "target": "browser", "val": snap.muted})
        self._last_channels["browser"] = snap

    async def _sync_media_to_esp32(
        self,
        ws,
        audio: Optional[AudioController] = None,
        force: bool = False,
    ) -> None:
        snap = await self._media.get_current_media()
        if force or self._last_media != snap:
            await self._send_json(
                ws,
                {
                    "sync": "media",
                    "title": snap.title,
                    "artist": snap.artist,
                    "playing": snap.playing,
                },
            )
            if audio is not None:
                await self._sync_browser_channel(ws, audio)
            if not force or self._last_media is None:
                estado = "TOCANDO" if snap.playing else "PAUSADO"
                self.log.info(
                    "[Windows -> WiFi] Mídia (%s): %s - %s",
                    estado,
                    snap.title,
                    snap.artist,
                )
            self._last_media = snap

    async def _read_system_stats(self) -> dict:
        cpu_pct = int(round(max(0.0, min(100.0, psutil.cpu_percent(interval=None)))))
        cpu_temp = self._cpu_temp.read_temperature()
        gpu_pct, gpu_temp = await self._gpu.read_metrics()
        vm = psutil.virtual_memory()
        ram_pct = int(round(max(0.0, min(100.0, vm.percent))))
        gb = 1024.0 ** 3
        ram_used = f"{vm.used / gb:.1f}"
        ram_total = f"{vm.total / gb:.1f}"

        disk_pct = -1
        try:
            disk_pct = int(round(max(0.0, min(100.0, psutil.disk_usage("C:\\").percent))))
        except OSError:
            disk_pct = -1

        payload: dict = {
            "sync": "stats",
            "cpu": cpu_pct,
            "cpu_temp": cpu_temp,
            "gpu": gpu_pct,
            "gpu_temp": gpu_temp,
            "ram_pct": ram_pct,
            "ram_used": ram_used,
            "ram_total": ram_total,
        }
        if disk_pct >= 0:
            payload["disk_pct"] = disk_pct
        return payload

    async def _sync_stats_to_esp32(self, ws, log_once: bool = False) -> None:
        stats = await self._read_system_stats()
        await self._send_json(ws, stats)
        if log_once:
            cpu_t = f" | {stats['cpu_temp']}°C" if stats.get("cpu_temp") else ""
            gpu_t = f" | {stats['gpu_temp']}°C" if stats.get("gpu_temp") else ""
            disk_str = f" | Disco C: {stats['disk_pct']}%" if "disk_pct" in stats else ""
            self.log.info(
                "  Hardware       -> CPU: %d%%%s | GPU: %d%%%s (%s) | RAM: %d%% (%s / %s GB)%s",
                stats["cpu"],
                cpu_t,
                stats["gpu"],
                gpu_t,
                self._gpu.gpu_name,
                stats["ram_pct"],
                stats["ram_used"],
                stats["ram_total"],
                disk_str,
            )

    # ------------------------------------------------------------------
    # Macros / Atalhos de Aplicativos e Servidor CS2 GSI (C4 Timer)
    # ------------------------------------------------------------------

    def _launch_windows_target(self, target: str) -> bool:
        try:
            if hasattr(os, "startfile"):
                os.startfile(target)
                return True
        except OSError:
            pass
        try:
            creationflags = 0x08000000 if os.name == "nt" else 0
            subprocess.Popen(
                ["cmd", "/c", "start", "", target],
                creationflags=creationflags,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            return True
        except OSError:
            return False

    def _execute_macro(self, action: str) -> bool:
        action = action.strip().lower()
        if action == "edge":
            return self._launch_windows_target("msedge")
        if action == "steam":
            return self._launch_windows_target("steam://open/main")
        if action == "discord":
            if self._launch_windows_target("discord://"):
                return True
            local_app = os.environ.get("LOCALAPPDATA", "")
            updater = Path(local_app) / "Discord" / "Update.exe"
            if updater.exists():
                try:
                    creationflags = 0x08000000 if os.name == "nt" else 0
                    subprocess.Popen(
                        [str(updater), "--processStart", "Discord.exe"],
                        creationflags=creationflags,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                    return True
                except OSError:
                    return False
            return False
        if action == "disney":
            return bool(webbrowser.open("https://www.disneyplus.com"))
        if action == "hbo":
            return bool(webbrowser.open("https://www.max.com"))
        if action == "cs2_c4":
            return True
        return False

    async def _sync_cs2_c4_to_esp32(self, ws=None) -> None:
        target_ws = ws if ws is not None else self._active_ws
        if target_ws is None:
            return
        payload = {
            "sync": "cs2_c4",
            "state": self._c4_state,
            "time_left": self._c4_time_left if self._c4_state == "planted" else 0,
        }
        try:
            await self._send_json(target_ws, payload)
        except (WebSocketException, OSError):
            pass

    @staticmethod
    def _enable_tcp_nodelay(transport) -> bool:
        if transport is None:
            return False
        try:
            sock = transport.get_extra_info("socket")
            if sock is not None:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                return True
        except OSError:
            pass
        return False

    async def _cs2_c4_countdown_loop(self, skip_initial_send: bool = False) -> None:
        try:
            first_tick = True
            while self._c4_state == "planted" and self._c4_deadline is not None:
                remaining = int(round(max(0.0, self._c4_deadline - time.monotonic())))
                self._c4_time_left = remaining
                if not (first_tick and skip_initial_send):
                    await self._sync_cs2_c4_to_esp32()
                first_tick = False
                if remaining <= 0:
                    break
                # Sincroniza o próximo tick exatamente no próximo segundo inteiro
                next_target = self._c4_deadline - (remaining - 1)
                sleep_s = max(0.05, min(1.0, next_target - time.monotonic()))
                await asyncio.sleep(sleep_s)
        except asyncio.CancelledError:
            return

        if self._c4_state == "planted":
            self._c4_state = "idle"
            self._c4_time_left = 0
            self._c4_deadline = None
            self.log.info("[CS2 GSI] Contagem da C4 finalizada -> idle.")
            await self._sync_cs2_c4_to_esp32()

    async def _start_cs2_c4_timer(self, initial_seconds: float = CS2_C4_TOTAL_SECONDS) -> None:
        clamped = max(1.0, min(60.0, float(initial_seconds)))
        if self._c4_state == "planted" and self._c4_task and not self._c4_task.done():
            return
        self._c4_state = "planted"
        self._c4_deadline = time.monotonic() + clamped
        self._c4_time_left = int(round(clamped))
        self.log.info("[CS2 GSI] BOMBA PLANTADA! Iniciando contador regressivo (%ds).", self._c4_time_left)
        # Dispara imediatamente o primeiro frame via WebSocket na mesma corrotina (zero atraso)
        await self._sync_cs2_c4_to_esp32()
        if self._c4_task and not self._c4_task.done():
            self._c4_task.cancel()
        self._c4_task = asyncio.create_task(self._cs2_c4_countdown_loop(skip_initial_send=True))

    async def _stop_cs2_c4_timer(self, reason: str) -> None:
        was_active = self._c4_state != "idle" or (self._c4_task is not None and not self._c4_task.done())
        if self._c4_task and not self._c4_task.done():
            self._c4_task.cancel()
        self._c4_task = None
        self._c4_state = "idle"
        self._c4_time_left = 0
        self._c4_deadline = None
        if was_active:
            self.log.info("[CS2 GSI] C4 encerrada (%s) -> idle.", reason)
            await self._sync_cs2_c4_to_esp32()

    async def _handle_cs2_gsi_payload(self, payload: dict) -> None:
        round_info = payload.get("round")
        bomb_info = payload.get("bomb")
        round_bomb = ""
        round_phase = ""
        bomb_state = ""

        if isinstance(round_info, dict):
            round_bomb = str(round_info.get("bomb", "")).strip().lower()
            round_phase = str(round_info.get("phase", "")).strip().lower()

        if isinstance(bomb_info, dict):
            bomb_state = str(bomb_info.get("state", "")).strip().lower()

        self.log.info(
            "[CS2 GSI] Payload recebido do CS2: bomb_state=%s | round_bomb=%s | round_phase=%s",
            bomb_state or "-",
            round_bomb or "-",
            round_phase or "-",
        )

        if bomb_state in ("defused", "exploded") or round_bomb in ("defused", "exploded"):
            reason = bomb_state if bomb_state in ("defused", "exploded") else round_bomb
            await self._stop_cs2_c4_timer(reason)
            return

        if round_phase in ("over", "freezetime"):
            await self._stop_cs2_c4_timer(round_phase)
            return

        if bomb_state in ("planted", "planting") or round_bomb == "planted":
            countdown = float(CS2_C4_TOTAL_SECONDS)
            if isinstance(bomb_info, dict) and bomb_info.get("countdown") is not None:
                try:
                    countdown = float(bomb_info["countdown"])
                except (TypeError, ValueError):
                    countdown = float(CS2_C4_TOTAL_SECONDS)
            await self._start_cs2_c4_timer(countdown)

    async def _handle_gsi_http_client(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        self._enable_tcp_nodelay(writer.transport)
        try:
            raw_headers = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=3.0)
            header_text = raw_headers.decode("iso-8859-1", errors="replace")
            content_length = 0
            expect_continue = False
            is_chunked = False

            for line in header_text.split("\r\n")[1:]:
                if ":" in line:
                    k, v = line.split(":", 1)
                    key = k.strip().lower()
                    val = v.strip()
                    if key == "content-length":
                        try:
                            content_length = max(0, min(512 * 1024, int(val)))
                        except ValueError:
                            content_length = 0
                    elif key == "expect" and "100-continue" in val.lower():
                        expect_continue = True
                    elif key == "transfer-encoding" and "chunked" in val.lower():
                        is_chunked = True

            # CS2 (libcurl) pode enviar Expect: 100-continue em payloads > 1 KB
            if expect_continue:
                writer.write(b"HTTP/1.1 100 Continue\r\n\r\n")
                await writer.drain()

            body = b""
            if is_chunked:
                chunks: list[bytes] = []
                total_len = 0
                while total_len < 512 * 1024:
                    size_line = await asyncio.wait_for(reader.readline(), timeout=3.0)
                    if not size_line:
                        break
                    chunk_size = int(size_line.split(b";", 1)[0].strip(), 16)
                    if chunk_size <= 0:
                        await asyncio.wait_for(reader.readexactly(2), timeout=2.0)
                        break
                    chunk_data = await asyncio.wait_for(reader.readexactly(chunk_size), timeout=3.0)
                    await asyncio.wait_for(reader.readexactly(2), timeout=2.0)
                    chunks.append(chunk_data)
                    total_len += chunk_size
                body = b"".join(chunks)
            elif content_length > 0:
                body = await asyncio.wait_for(reader.readexactly(content_length), timeout=3.0)

            # Dispara o processamento/WebSocket imediatamente antes de encerrar a conexão HTTP
            if body:
                try:
                    payload = json.loads(body.decode("utf-8", errors="replace"))
                    if isinstance(payload, dict):
                        await self._handle_cs2_gsi_payload(payload)
                except json.JSONDecodeError:
                    self.log.warning("[CS2 GSI] Payload JSON inválido recebido na porta %d.", self.gsi_port)

            writer.write(
                b"HTTP/1.1 200 OK\r\n"
                b"Content-Type: text/plain\r\n"
                b"Content-Length: 2\r\n"
                b"Connection: close\r\n\r\nOK"
            )
            await writer.drain()
        except (asyncio.TimeoutError, asyncio.IncompleteReadError, OSError, ValueError):
            pass
        finally:
            try:
                writer.close()
                await writer.wait_closed()
            except OSError:
                pass

    async def _start_cs2_gsi_server(self) -> None:
        if self._gsi_server is not None or self.gsi_port <= 0:
            return
        try:
            self._gsi_server = await asyncio.start_server(
                self._handle_gsi_http_client,
                host="127.0.0.1",
                port=self.gsi_port,
            )
            self.log.info("CS2 GSI      : http://127.0.0.1:%d (escutando C4)", self.gsi_port)
        except OSError as exc:
            self.log.warning(
                "CS2 GSI      : porta %d indisponível (%s) — timer C4 desativado nesta instância.",
                self.gsi_port,
                exc,
            )
            self._gsi_server = None

    async def _stop_cs2_gsi_server(self) -> None:
        if self._c4_task and not self._c4_task.done():
            self._c4_task.cancel()
            try:
                await self._c4_task
            except asyncio.CancelledError:
                pass
        self._c4_task = None
        if self._gsi_server is not None:
            self._gsi_server.close()
            try:
                await self._gsi_server.wait_closed()
            except OSError:
                pass
            self._gsi_server = None

    # ------------------------------------------------------------------
    # Aplicação de Eventos ESP32 -> Windows
    # ------------------------------------------------------------------

    async def _handle_esp_event(self, ws, audio: AudioController, msg: dict) -> None:
        cmd = str(msg.get("cmd", "")).lower()
        event = str(msg.get("event", "")).lower()
        target = str(msg.get("target", "")).lower()

        if cmd == "media":
            action = str(msg.get("action", "")).lower()
            if action in ("play_pause", "next", "prev"):
                ok = await self._media.handle_action(action)
                self.log.info(
                    "[ESP32 WiFi -> Windows] Comando de mídia '%s' -> %s",
                    action,
                    "OK" if ok else "FALHA",
                )
                await asyncio.sleep(0.15)
                await self._sync_media_to_esp32(ws, audio=audio, force=True)
            return

        if cmd == "macro":
            action = str(msg.get("action", "")).lower()
            ok = self._execute_macro(action)
            self.log.info(
                "[ESP32 WiFi -> Windows] Atalho '%s' -> %s",
                action,
                "OK" if ok else "FALHA",
            )
            if action == "cs2_c4":
                await self._sync_cs2_c4_to_esp32(ws)
            return

        if event == "screen":
            screen = str(msg.get("screen", "")).lower()
            if screen == "media":
                self.log.info("[ESP32 WiFi] Tela Mídia aberta -> sincronizando Edge e Now Playing...")
                await self._sync_browser_channel(ws, audio)
                await self._sync_media_to_esp32(ws, audio=None, force=True)
            elif screen == "hardware":
                self.log.info("[ESP32 WiFi] Tela Hardware aberta -> enviando telemetria CPU/RAM/GPU...")
                await self._sync_stats_to_esp32(ws, log_once=False)
            elif screen == "macros":
                self.log.info("[ESP32 WiFi] Tela Atalhos aberta -> sincronizando estado CS2 C4...")
                await self._sync_cs2_c4_to_esp32(ws)
            return

        if event == "pong":
            return

        if event == "ready":
            self.log.info("[ESP32 WiFi] Placa pronta -> enviando sincronização completa...")
            await self._sync_to_esp32(ws, audio, force_all=True)
            await self._sync_media_to_esp32(ws, audio=None, force=True)
            await self._sync_stats_to_esp32(ws, log_once=False)
            await self._sync_cs2_c4_to_esp32(ws)
            return

        if event == "set_vol" and target:
            try:
                val = max(0, min(100, int(msg.get("val", 0))))
            except (TypeError, ValueError):
                return

            self._last_esp_write_ts[target] = time.monotonic()

            if target == "master":
                try:
                    applied = audio.set_master_volume(val)
                except (COMError, Exception) as exc:
                    self.log.warning(
                        "[Audio Hot-Plug] Falha ao ajustar Master (%s). Recarregando dispositivos...",
                        exc,
                    )
                    audio.refresh_devices()
                    try:
                        applied = audio.set_master_volume(val)
                        self.log.info(
                            "[Audio Hot-Plug] Reconectado ao dispositivo padrão: %s",
                            audio.get_master_device_name() or "N/A",
                        )
                    except (COMError, Exception) as retry_exc:
                        self.log.warning(
                            "[Audio Hot-Plug] Saída padrão temporariamente indisponível (%s).",
                            retry_exc,
                        )
                        return

                if "master" in self._last_channels:
                    self._last_channels["master"].volume = applied
                self.log.info("[ESP32 WiFi -> Windows] Master volume = %d%%", applied)
                return

            proc_name = self._resolved_processes.get(target)
            if proc_name:
                try:
                    ok = audio.set_app_volume(proc_name, val)
                except (COMError, Exception) as exc:
                    self.log.warning(
                        "[Audio Hot-Plug] Falha ao ajustar volume de '%s' (%s). Recarregando dispositivos...",
                        proc_name,
                        exc,
                    )
                    audio.refresh_devices()
                    try:
                        ok = audio.set_app_volume(proc_name, val)
                    except (COMError, Exception):
                        ok = False

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
                try:
                    new_state = audio.toggle_master_mute()
                except (COMError, Exception) as exc:
                    self.log.warning(
                        "[Audio Hot-Plug] Falha ao alternar mute Master (%s). Recarregando dispositivos...",
                        exc,
                    )
                    audio.refresh_devices()
                    try:
                        new_state = audio.toggle_master_mute()
                        self.log.info(
                            "[Audio Hot-Plug] Reconectado ao dispositivo padrão: %s",
                            audio.get_master_device_name() or "N/A",
                        )
                    except (COMError, Exception) as retry_exc:
                        self.log.warning(
                            "[Audio Hot-Plug] Saída padrão temporariamente indisponível (%s).",
                            retry_exc,
                        )
                        return

                if "master" in self._last_channels:
                    self._last_channels["master"].muted = new_state
                await self._send_json(ws, {"sync": "mute", "target": "master", "val": new_state})
                self.log.info(
                    "[ESP32 WiFi -> Windows] Master mute = %s",
                    "MUTADO" if new_state else "ATIVO",
                )
                return

            if target == "mic":
                try:
                    if not audio.has_microphone():
                        self.log.warning("[ESP32 WiFi -> Windows] Nenhum microfone padrão detectado.")
                        return
                    new_state = audio.toggle_mic_mute()
                except (COMError, Exception) as exc:
                    self.log.warning(
                        "[Audio Hot-Plug] Falha ao alternar Microfone (%s). Recarregando dispositivos...",
                        exc,
                    )
                    audio.refresh_devices()
                    try:
                        if not audio.has_microphone():
                            return
                        new_state = audio.toggle_mic_mute()
                        self.log.info(
                            "[Audio Hot-Plug] Microfone reconectado: %s",
                            audio.get_mic_device_name() or "N/A",
                        )
                    except (COMError, Exception) as retry_exc:
                        self.log.warning(
                            "[Audio Hot-Plug] Microfone temporariamente indisponível (%s).",
                            retry_exc,
                        )
                        return

                self._last_mic_mute = new_state
                await self._send_json(ws, {"sync": "mute", "target": "mic", "val": new_state})
                self.log.info(
                    "[ESP32 WiFi -> Windows] Microfone = %s",
                    "MUTADO" if new_state else "ATIVO",
                )
                return

            proc_name = self._resolved_processes.get(target)
            if proc_name:
                try:
                    new_state = audio.toggle_app_mute(proc_name)
                except (COMError, Exception) as exc:
                    self.log.warning(
                        "[Audio Hot-Plug] Falha ao alternar mute de '%s' (%s). Recarregando dispositivos...",
                        proc_name,
                        exc,
                    )
                    audio.refresh_devices()
                    try:
                        new_state = audio.toggle_app_mute(proc_name)
                    except (COMError, Exception):
                        new_state = None

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
        self._active_ws = ws
        if self._enable_tcp_nodelay(getattr(ws, "transport", None)):
            self.log.info("  WebSocket      -> TCP_NODELAY ativo (baixa latência)")
        await self._sync_to_esp32(ws, audio, force_all=True)
        await self._sync_media_to_esp32(ws, audio=audio, force=True)
        await self._sync_stats_to_esp32(ws, log_once=True)
        await self._sync_cs2_c4_to_esp32(ws)

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
            tick_count = 0
            while True:
                if deadline is not None and time.monotonic() >= deadline:
                    return
                await asyncio.sleep(POLL_INTERVAL_S)
                tick_count += 1
                await self._sync_to_esp32(ws, audio, force_all=False)
                if tick_count % 2 == 0:
                    await self._sync_media_to_esp32(ws, audio=audio, force=False)
                if tick_count % STATS_POLL_TICKS == 0:
                    await self._sync_stats_to_esp32(ws, log_once=False)
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
            self._active_ws = None
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

        try:
            await self._start_cs2_gsi_server()
            with AudioController() as audio:
                self.log.info("Saída padrão : %s", audio.get_master_device_name() or "N/A")
                self.log.info("Microfone    : %s", audio.get_mic_device_name() or "N/A")
                self.log.info("GPU Monitor  : %s [%s]", self._gpu.gpu_name, self._gpu.backend)
                self.log.info("CPU Temp     : backend [%s]", self._cpu_temp.backend)

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
        finally:
            await self._stop_cs2_gsi_server()
            self._gpu.close()
            self._cpu_temp.close()
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
    parser.add_argument(
        "--gsi-port",
        type=int,
        default=DEFAULT_CS2_GSI_PORT,
        help=f"Porta HTTP local para CS2 Game State Integration (padrão: {DEFAULT_CS2_GSI_PORT}, 0 desativa)",
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
        gsi_port=args.gsi_port,
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

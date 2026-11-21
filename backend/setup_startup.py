"""Configura a inicialização automática do Mini Deck IoT no Windows 11 (shell:startup).

Cria (ou atualiza) o atalho 'MiniDeckIoT.lnk' apontando para 'backend/run_hidden.vbs'
na pasta de Inicialização do usuário atual.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys

import comtypes
import comtypes.client

SHORTCUT_NAME = "MiniDeckIoT.lnk"


def get_startup_dir() -> Path:
    appdata = os.environ.get("APPDATA")
    if not appdata:
        raise RuntimeError("Variável de ambiente APPDATA não encontrada.")
    return Path(appdata) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Startup"


def install_startup_shortcut() -> Path:
    backend_dir = Path(__file__).resolve().parent
    vbs_path = backend_dir / "run_hidden.vbs"
    if not vbs_path.exists():
        raise FileNotFoundError(f"Arquivo não encontrado: {vbs_path}")

    startup_dir = get_startup_dir()
    startup_dir.mkdir(parents=True, exist_ok=True)
    shortcut_path = startup_dir / SHORTCUT_NAME

    comtypes.CoInitialize()
    try:
        shell = comtypes.client.CreateObject("WScript.Shell", dynamic=True)
        shortcut = shell.CreateShortcut(str(shortcut_path))
        shortcut.TargetPath = "wscript.exe"
        shortcut.Arguments = f'"{vbs_path}"'
        shortcut.WorkingDirectory = str(backend_dir)
        shortcut.Description = "Mini Deck IoT - Bridge Wi-Fi WebSocket de Controle de Áudio (ESP32-S3)"
        shortcut.WindowStyle = 7  # Minimizado/oculto
        shortcut.Save()
    finally:
        comtypes.CoUninitialize()

    return shortcut_path


def remove_startup_shortcut() -> bool:
    shortcut_path = get_startup_dir() / SHORTCUT_NAME
    if shortcut_path.exists():
        shortcut_path.unlink()
        return True
    return False


def main() -> None:
    if sys.stdout is not None and hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(
        description="Gerencia o atalho de autoinicialização do Mini Deck IoT no Windows 11."
    )
    parser.add_argument(
        "--remove",
        action="store_true",
        help="Remove o atalho da pasta de Inicialização do Windows",
    )
    args = parser.parse_args()

    if args.remove:
        removed = remove_startup_shortcut()
        if removed:
            print(f"Atalho removido com sucesso de: {get_startup_dir() / SHORTCUT_NAME}")
        else:
            print("Nenhum atalho do Mini Deck IoT encontrado na pasta de Inicialização.")
        return

    shortcut_path = install_startup_shortcut()
    backend_dir = Path(__file__).resolve().parent
    print("=" * 64)
    print("  Mini Deck IoT - Autoinicialização Configurada (Windows 11)")
    print("=" * 64)
    print(f"  Atalho criado : {shortcut_path}")
    print(f"  Alvo          : wscript.exe \"{backend_dir / 'run_hidden.vbs'}\"")
    print(f"  Pasta Startup : {get_startup_dir()}")
    print("=" * 64)


if __name__ == "__main__":
    main()

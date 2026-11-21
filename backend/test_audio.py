"""Script de teste para validação inicial do AudioController (Mini Deck IoT)."""

from __future__ import annotations

import sys

from audio_controller import AudioController


def format_mute(muted: bool) -> str:
    return "MUTADO" if muted else "ATIVO (sem mute)"


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    print("=" * 60)
    print("  Mini Deck IoT - Teste do Controlador de Áudio (Windows 11)")
    print("=" * 60)

    with AudioController() as audio:
        # 1. Volume Master e Mute Master
        print("\n[SAÍDA PADRÃO / MASTER]")
        try:
            master_name = audio.get_master_device_name() or "Desconhecido"
            master_vol = audio.get_master_volume()
            master_mute = audio.get_master_mute()
            print(f"  Dispositivo : {master_name}")
            print(f"  Volume      : {master_vol}%")
            print(f"  Status Mute : {format_mute(master_mute)}")
        except RuntimeError as exc:
            print(f"  Erro: {exc}")

        # 2. Microfone Padrão
        print("\n[MICROFONE PADRÃO / ENTRADA]")
        if audio.has_microphone():
            mic_name = audio.get_mic_device_name() or "Desconhecido"
            mic_vol = audio.get_mic_volume()
            mic_mute = audio.get_mic_mute()
            print(f"  Dispositivo : {mic_name}")
            print(f"  Volume      : {mic_vol}%")
            print(f"  Status Mute : {format_mute(mic_mute)}")
        else:
            print("  Nenhum microfone padrão detectado.")

        # 3. Sessões de Aplicativos de Áudio
        sessions = audio.list_audio_sessions(active_only=False, group_by_app=True)
        active_count = sum(1 for s in sessions if s.is_active)

        print(f"\n[APLICATIVOS DE ÁUDIO DETECTADOS: {len(sessions)} ({active_count} reproduzindo agora)]")
        if not sessions:
            print("  Nenhum aplicativo com sessão de áudio encontrado no momento.")
        else:
            for idx, app in enumerate(sessions, start=1):
                state_tag = "ATIVO  " if app.is_active else "INATIVO"
                mute_tag = "MUTADO" if app.muted else "OK"
                pids_str = ", ".join(str(pid) for pid in app.pids)
                print(
                    f"  {idx:02d}. [{state_tag}] {app.process_name:<25} "
                    f"| Vol: {app.volume:3d}% | Mute: {mute_tag:<6} | PIDs: {pids_str}"
                )

    print("\n" + "=" * 60)


if __name__ == "__main__":
    main()

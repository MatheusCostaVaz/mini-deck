#pragma once

#include <stdint.h>

enum UiConnState : uint8_t {
    UI_CONN_WAITING = 0,      // Aguardando conexão do PC (amarelo/cinza)
    UI_CONN_SYNCED = 1,       // Sincronizado via Serial USB (verde)
    UI_CONN_WIFI = 2,         // Conectado via WebSocket Wi-Fi (verde)
    UI_CONN_DISCONNECTED = 3, // Timeout / desconectado (vermelho)
};

using UiVolumeCallback = void (*)(const char *target, int value);
using UiMuteCallback = void (*)(const char *target);

// Inicializa toda a interface LVGL (480x320 Dark Theme)
void ui_init(UiVolumeCallback on_volume_change, UiMuteCallback on_mute_toggle);

// Sincronização vinda do PC (não dispara eventos de volta)
void ui_sync_volume(const char *target, int value);
void ui_sync_mute(const char *target, bool muted);
void ui_sync_label(const char *target, const char *label);
void ui_set_conn_state(UiConnState state);
void ui_set_pc_connected(bool connected);
void ui_set_status_text(const char *text);

// Indicadores de Wi-Fi e Relógio Digital (NTP) no cabeçalho
void ui_set_wifi_status(bool connected, const char *ip_str);
void ui_set_clock_text(const char *hhmm);

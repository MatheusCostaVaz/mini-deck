#pragma once

#include <lvgl.h>
#include <stdint.h>

// Fontes externas oficiais (assets Phosphor + Inter em firmware/src/assets/)
LV_FONT_DECLARE(phosphor_20);
LV_FONT_DECLARE(phosphor_28);
LV_FONT_DECLARE(phosphor_32);
LV_FONT_DECLARE(phosphor_42);
LV_FONT_DECLARE(inter_16);
LV_FONT_DECLARE(inter_medium_16);
LV_FONT_DECLARE(inter_bold_16);

// Ícones Phosphor (UTF-8 verificados em phosphor_20/28/32/42.c)
#define PH_ICN_MIXER     "\xEE\x88\xA8" // U+E228 Faders / Mixer
#define PH_ICN_CROSSHAIR "\xEE\x87\x8A" // U+E1CA Crosshair / CS2 C4
#define PH_ICN_FILM      "\xEE\x89\x80" // U+E240 FilmSlate / Disney+
#define PH_ICN_FIRE      "\xEE\x89\x82" // U+E242 Fire / C4 Plantada
#define PH_ICN_GAME      "\xEE\x89\xAE" // U+E26E Gamepad / Steam
#define PH_ICN_HOME      "\xEE\x8B\x82" // U+E2C2 Home
#define PH_ICN_SMART     "\xEE\x8B\x9C" // U+E2DC Lightbulb / Brilho
#define PH_ICN_MIC_ON    "\xEE\x8C\xA6" // U+E326 Microphone On
#define PH_ICN_MIC_OFF   "\xEE\x8C\xA8" // U+E328 Microphone Slash
#define PH_ICN_MPLAY     "\xEE\x8C\xB4" // U+E334 MonitorPlay / Max
#define PH_ICN_MUSIC     "\xEE\x8D\x80" // U+E340 Music Notes / Mídia
#define PH_ICN_PAUSE     "\xEE\x8E\xA0" // U+E3A0 Pause
#define PH_ICN_PLAY      "\xEE\x8F\x92" // U+E3D2 Play
#define PH_ICN_PREV      "\xEE\x90\xAE" // U+E42E SkipBack / Anterior
#define PH_ICN_NEXT      "\xEE\x90\xB0" // U+E430 SkipForward / Próximo
#define PH_ICN_WARNING   "\xEE\x93\xA0" // U+E4E0 Warning / C4 Crítico
#define PH_ICN_WIFI      "\xEE\x93\xAA" // U+E4EA Wi-Fi High / Rede
#define PH_ICN_APP       "\xEE\x97\x9A" // U+E5DA SquaresFour / Atalhos
#define PH_ICN_CPU       "\xEE\x98\x90" // U+E610 CPU / Hardware
#define PH_ICN_GPU       "\xEE\x8C\xB2" // U+E332 Monitor / GPU
#define PH_ICN_DISCORD   "\xEE\x98\x9A" // U+E61A Discord
#define PH_ICN_BROWSER   "\xEE\x98\xBA" // U+E63A Browser / Globe
#define PH_ICN_VOL       "\xEE\x99\xAE" // U+E66E Speaker / Volume

enum UiConnState : uint8_t {
    UI_CONN_WAITING = 0,      // Aguardando conexão do PC (âmbar)
    UI_CONN_SYNCED = 1,       // Sincronizado via Serial USB (verde)
    UI_CONN_WIFI = 2,         // Conectado via WebSocket Wi-Fi (verde)
    UI_CONN_DISCONNECTED = 3, // Timeout / desconectado (vermelho)
};

enum UiScreenId : uint8_t {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_AUDIO = 1,
    UI_SCREEN_MEDIA = 2,
    UI_SCREEN_HARDWARE = 3,
    UI_SCREEN_MACROS = 4,
};

using UiVolumeCallback = void (*)(const char *target, int value);
using UiMuteCallback   = void (*)(const char *target);
using UiMediaCallback  = void (*)(const char *action);
using UiScreenCallback = void (*)(const char *screen_name);
using UiMacroCallback  = void (*)(const char *action);

// Inicializa arquitetura multi-screen LVGL (Home, Mixer, Mídia, Hardware, Atalhos)
void ui_init(
    UiVolumeCallback on_volume_change,
    UiMuteCallback on_mute_toggle,
    UiMediaCallback on_media_action = nullptr,
    UiScreenCallback on_screen_change = nullptr,
    UiMacroCallback on_macro_action = nullptr);

// Navegação entre telas
void ui_show_screen(UiScreenId screen_id, bool animate = true);

// Sincronização vinda do PC (não dispara eventos de volta)
void ui_sync_volume(const char *target, int value);
void ui_sync_mute(const char *target, bool muted);
void ui_sync_label(const char *target, const char *label);
void ui_sync_media(const char *title, const char *artist, bool playing);
void ui_sync_stats(int cpu_pct, int gpu_pct, int ram_pct, const char *ram_used, const char *ram_total, int disk_pct = -1);
void ui_sync_cs2_c4(const char *state, int time_left);
void ui_set_conn_state(UiConnState state);
void ui_set_pc_connected(bool connected);
void ui_set_status_text(const char *text);

// Indicadores de Wi-Fi e Relógio Digital (NTP) sincronizados em todas as telas
void ui_set_wifi_status(bool connected, const char *ip_str);
void ui_set_clock_text(const char *hhmm);

#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

// ============================================================================
// Paleta Dark Theme V2 (Design System Ergonomico - Header 50px - 480x320)
// ============================================================================
constexpr uint32_t HEX_BG           = 0x090E1A;
constexpr uint32_t HEX_CARD         = 0x131C2E;
constexpr uint32_t HEX_CARD_PRESSED = 0x1E293B;
constexpr uint32_t HEX_CARD_BORDER  = 0x26354F;
constexpr uint32_t HEX_TRACK        = 0x1E293B;
constexpr uint32_t HEX_BTN_IDLE     = 0x1E293B;
constexpr uint32_t HEX_TEXT_MAIN    = 0xF8FAFC;
constexpr uint32_t HEX_TEXT_DIM     = 0x94A3B8;
constexpr uint32_t HEX_CYAN         = 0x22D3EE;
constexpr uint32_t HEX_ROSE         = 0xFB7185;
constexpr uint32_t HEX_MUTE_BG      = 0xDC2626;
constexpr uint32_t HEX_MUTE_BORDER  = 0xF87171;
constexpr uint32_t HEX_MIC_OK_BG    = 0x059669;
constexpr uint32_t HEX_MIC_OK_BDR   = 0x34D399;
constexpr uint32_t HEX_WARN         = 0xF59E0B;
constexpr uint32_t HEX_OK           = 0x10B981;

constexpr uint32_t SLIDER_THROTTLE_MS = 40;
constexpr uint32_t TOAST_DURATION_MS  = 1600;
constexpr int      NUM_SCREENS        = 12;

struct ChannelUi {
    const char *target;
    const char *default_name;
    uint32_t accent_hex;

    lv_obj_t *card = nullptr;
    lv_obj_t *name_label = nullptr;
    lv_obj_t *pct_label = nullptr;
    lv_obj_t *slider = nullptr;
    lv_obj_t *mute_btn = nullptr;
    lv_obj_t *mute_label = nullptr;

    int value = 100;
    bool muted = false;
    bool dragging = false;
    int last_sent_value = -1;
    uint32_t last_send_ms = 0;

    ChannelUi(const char *t, const char *n, uint32_t c)
        : target(t), default_name(n), accent_hex(c) {}
};

struct HubCardDef {
    const char *id;
    const char *title;
    const char *icon_utf8;
    uint32_t accent_hex;
    uint32_t press_bg_hex;
    int8_t target_screen; // -1 = em breve, ou UiScreenId

    HubCardDef(const char *i, const char *t, const char *ic, uint32_t a, uint32_t pbg, int8_t scr)
        : id(i), title(t), icon_utf8(ic), accent_hex(a), press_bg_hex(pbg), target_screen(scr) {}
};

struct HeaderMicWidgets {
    lv_obj_t *btn = nullptr;
    lv_obj_t *icon = nullptr;
    lv_obj_t *label = nullptr;
};

struct SidebarWidgets {
    lv_obj_t *icon = nullptr;
    lv_obj_t *slider = nullptr;
    lv_obj_t *pct = nullptr;
    bool dragging = false;
};

ChannelUi g_channels[4] = {
    ChannelUi("master",  "MASTER",  HEX_CYAN),
    ChannelUi("browser", "EDGE",    0x38BDF8),
    ChannelUi("discord", "DISCORD", 0xA78BFA),
    ChannelUi("game",    "JOGO",    0x34D399),
};

// 6 Cards Minimalistas da Home (apenas ícone grande Phosphor 42px centralizado)
const HubCardDef g_hub_cards[6] = {
    HubCardDef("media",    "Mídia",        PH_ICN_MUSIC, HEX_ROSE, 0x3B1024, UI_SCREEN_MEDIA),
    HubCardDef("mixer",    "Mixer",        PH_ICN_MIXER, HEX_CYAN, 0x083344, UI_SCREEN_AUDIO),
    HubCardDef("light",    "Brilho",       PH_ICN_SMART, 0xFBBF24, 0x3B2608, UI_SCREEN_BRIGHTNESS),
    HubCardDef("settings", "Configuração", PH_ICN_GEAR,  0x38BDF8, 0x0A2A43, UI_SCREEN_SETTINGS),
    HubCardDef("macros",   "Atalhos",      PH_ICN_APP,   0xA78BFA, 0x22184A, UI_SCREEN_MACROS),
    HubCardDef("hardware", "Hardware",     PH_ICN_CPU,   0x34D399, 0x064E3B, UI_SCREEN_HARDWARE),
};

// ============================================================================
// Telas Principais e Widgets Persistentes
// ============================================================================
lv_obj_t *g_screens[NUM_SCREENS]      = {};
lv_obj_t *g_header_clock[NUM_SCREENS] = {};
HeaderMicWidgets g_header_mic[NUM_SCREENS];

// Badge de conexão no centro do Header da Home
lv_obj_t *g_home_conn_badge = nullptr;
lv_obj_t *g_home_conn_label = nullptr;

// Cabeçalho dinâmico da tela screen_game_hud
lv_obj_t *g_hud_header_icon  = nullptr;
lv_obj_t *g_hud_header_title = nullptr;

// Sliders laterais esquerdos persistentes (Mídia = Browser, HUD = Game, demais = Master)
SidebarWidgets g_home_sidebar;
SidebarWidgets g_media_sidebar;
SidebarWidgets g_hw_sidebar;
SidebarWidgets g_macros_sidebar;
SidebarWidgets g_games_sidebar;
SidebarWidgets g_hud_sidebar;
SidebarWidgets g_settings_sidebar;
SidebarWidgets g_wifi_sidebar;
SidebarWidgets g_bright_sidebar;
SidebarWidgets g_sys_sidebar;

SidebarWidgets *const g_master_sidebars[] = {
    &g_home_sidebar,
    &g_hw_sidebar,
    &g_macros_sidebar,
    &g_games_sidebar,
    &g_settings_sidebar,
    &g_wifi_sidebar,
    &g_bright_sidebar,
    &g_sys_sidebar,
};

// Toast flutuante da Home
lv_obj_t *g_home_toast       = nullptr;
lv_obj_t *g_home_toast_label = nullptr;
lv_timer_t *g_toast_timer    = nullptr;

// Widgets do Player de Mídia (screen_media)
lv_obj_t *g_media_icon_box   = nullptr;
lv_obj_t *g_media_icon_lbl   = nullptr;
lv_obj_t *g_media_state_lbl  = nullptr;
lv_obj_t *g_media_title_lbl  = nullptr;
lv_obj_t *g_media_artist_lbl = nullptr;
lv_obj_t *g_media_play_btn   = nullptr;
lv_obj_t *g_media_play_icon  = nullptr;

// Widgets do Monitor de Hardware (screen_hardware)
lv_obj_t *g_hw_cpu_accent    = nullptr;
lv_obj_t *g_hw_cpu_pct_lbl   = nullptr;
lv_obj_t *g_hw_cpu_temp_lbl  = nullptr;
lv_obj_t *g_hw_cpu_bar       = nullptr;
lv_obj_t *g_hw_cpu_state_lbl = nullptr;
lv_obj_t *g_hw_disk_lbl      = nullptr;

lv_obj_t *g_hw_gpu_accent    = nullptr;
lv_obj_t *g_hw_gpu_pct_lbl   = nullptr;
lv_obj_t *g_hw_gpu_temp_lbl  = nullptr;
lv_obj_t *g_hw_gpu_bar       = nullptr;
lv_obj_t *g_hw_gpu_state_lbl = nullptr;

lv_obj_t *g_hw_ram_accent    = nullptr;
lv_obj_t *g_hw_ram_pct_lbl   = nullptr;
lv_obj_t *g_hw_ram_bar       = nullptr;
lv_obj_t *g_hw_ram_gb_lbl    = nullptr;

// Widgets do Painel Game HUD (screen_game_hud)
lv_obj_t *g_hud_cpu_accent   = nullptr;
lv_obj_t *g_hud_cpu_pct_lbl  = nullptr;
lv_obj_t *g_hud_cpu_temp_lbl = nullptr;
lv_obj_t *g_hud_cpu_bar      = nullptr;

lv_obj_t *g_hud_gpu_accent   = nullptr;
lv_obj_t *g_hud_gpu_pct_lbl  = nullptr;
lv_obj_t *g_hud_gpu_temp_lbl = nullptr;
lv_obj_t *g_hud_gpu_bar      = nullptr;

lv_obj_t *g_hud_ram_accent   = nullptr;
lv_obj_t *g_hud_ram_pct_lbl  = nullptr;
lv_obj_t *g_hud_ram_gb_lbl   = nullptr;
lv_obj_t *g_hud_ram_bar      = nullptr;

// Bloco Inferior do Game HUD: Modo CS2 (C4 Timer) vs Modo Genérico (Sessão/Disco/RAM)
lv_obj_t *g_hud_cs2_panel    = nullptr;
lv_obj_t *g_hud_c4_accent    = nullptr;
lv_obj_t *g_hud_c4_icon_box  = nullptr;
lv_obj_t *g_hud_c4_icon_lbl  = nullptr;
lv_obj_t *g_hud_c4_title_lbl = nullptr;
lv_obj_t *g_hud_c4_time_lbl  = nullptr;
lv_obj_t *g_hud_c4_sub_lbl   = nullptr;
lv_obj_t *g_hud_c4_bar       = nullptr;

lv_obj_t *g_hud_gen_panel    = nullptr;
lv_obj_t *g_hud_gen_accent   = nullptr;
lv_obj_t *g_hud_gen_icon_box = nullptr;
lv_obj_t *g_hud_gen_icon_lbl = nullptr;
lv_obj_t *g_hud_gen_title    = nullptr;
lv_obj_t *g_hud_gen_ram_lbl  = nullptr;
lv_obj_t *g_hud_gen_disk_lbl = nullptr;
lv_obj_t *g_hud_gen_disk_bar = nullptr;

// Widgets do Hub de Configurações (screen_settings)
lv_obj_t *g_set_wifi_sub        = nullptr;
lv_obj_t *g_set_bt_card         = nullptr;
lv_obj_t *g_set_bt_accent       = nullptr;
lv_obj_t *g_set_bt_icon         = nullptr;
lv_obj_t *g_set_bt_sub          = nullptr;
lv_obj_t *g_set_bt_pill         = nullptr;
lv_obj_t *g_set_bt_pill_lbl     = nullptr;
lv_obj_t *g_set_bright_sub      = nullptr;
lv_obj_t *g_set_sys_sub         = nullptr;

// Widgets da Tela de Gerenciamento Wi-Fi (screen_wifi_manager)
lv_obj_t *g_net_wifi_icon       = nullptr;
lv_obj_t *g_net_ssid_lbl        = nullptr;
lv_obj_t *g_net_rssi_lbl        = nullptr;
lv_obj_t *g_net_ip_lbl          = nullptr;

lv_obj_t *g_net_scan_btn        = nullptr;
lv_obj_t *g_net_scan_status_lbl = nullptr;
lv_obj_t *g_net_wifi_list       = nullptr;

// Widgets da Tela de Brilho do Display (screen_brightness)
lv_obj_t *g_bright_slider       = nullptr;
lv_obj_t *g_bright_pct_lbl      = nullptr;
lv_obj_t *g_bright_icon_box     = nullptr;

// Widgets da Tela de Sistema / Info (screen_system_info)
lv_obj_t *g_sys_wifi_lbl        = nullptr;
lv_obj_t *g_sys_mem_lbl         = nullptr;
lv_obj_t *g_sys_ws_lbl          = nullptr;
lv_obj_t *g_sys_standby_lbl     = nullptr;
lv_obj_t *g_sys_calib_lbl       = nullptr;
lv_obj_t *g_sys_restart_lbl     = nullptr;

// Widgets da Tela de Descanso Inteligente (screen_screensaver)
lv_obj_t *g_ss_clock_lbl        = nullptr;
lv_obj_t *g_ss_date_lbl         = nullptr;
lv_obj_t *g_ss_weather_lbl      = nullptr;

// Modal de Senha Wi-Fi + Teclado Virtual LVGL (overlay em screen_wifi_manager)
lv_obj_t *g_wifi_modal          = nullptr;
lv_obj_t *g_wifi_modal_title    = nullptr;
lv_obj_t *g_wifi_pwd_ta         = nullptr;
lv_obj_t *g_wifi_pwd_eye_lbl    = nullptr;
lv_obj_t *g_wifi_kb             = nullptr;

constexpr int MAX_WIFI_SCAN_RESULTS = 12;
UiWifiNetwork g_wifi_scan_cache[MAX_WIFI_SCAN_RESULTS];
int g_wifi_scan_count           = 0;
char g_selected_ssid[33]        = "";

// Estado Global Compartilhado
UiScreenId g_active_screen         = UI_SCREEN_HOME;
UiScreenId g_prev_screen_before_ss = UI_SCREEN_HOME;
UiConnState g_conn_state           = UI_CONN_WAITING;
bool g_mic_muted                   = false;
bool g_media_playing               = false;
bool g_pc_bt_enabled               = false;
bool g_wifi_connected              = false;
bool g_hud_cs2_mode                = true;
char g_wifi_ip[24]                 = "";
char g_ws_host_ip[24]              = "";
char g_wifi_ssid_str[36]           = "Desconectado";
int32_t g_wifi_rssi                = 0;
char g_last_clock[16]              = "--:--";
char g_last_date[32]               = "Aguardando data NTP...";
char g_weather_city[32]            = "São Luís";
int g_weather_temp                 = 0;
bool g_has_weather                 = false;
bool g_syncing_from_pc             = false;

UiVolumeCallback      g_vol_cb          = nullptr;
UiMuteCallback        g_mute_cb         = nullptr;
UiMediaCallback       g_media_cb        = nullptr;
UiScreenCallback      g_screen_cb       = nullptr;
UiMacroCallback       g_macro_cb        = nullptr;
UiLaunchGameCallback  g_launch_game_cb  = nullptr;
UiPcBluetoothCallback g_pc_bt_cb        = nullptr;
UiWifiScanCallback    g_wifi_scan_cb    = nullptr;
UiWifiConnectCallback g_wifi_connect_cb = nullptr;

// ============================================================================
// Helpers Gerais
// ============================================================================
int clamp_vol(int v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return v;
}

ChannelUi *find_channel(const char *target)
{
    if (!target) return nullptr;
    for (auto &ch : g_channels)
    {
        if (strcasecmp(ch.target, target) == 0)
        {
            return &ch;
        }
    }
    return nullptr;
}

void update_sidebar_visuals(SidebarWidgets &sb, const ChannelUi &ch, const char *active_icon)
{
    if (!sb.slider || !sb.pct || !sb.icon) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", ch.value);
    lv_label_set_text(sb.pct, buf);

    if (ch.muted)
    {
        lv_label_set_text(sb.icon, PH_ICN_MIC_OFF);
        lv_obj_set_style_text_color(sb.icon, lv_color_hex(HEX_MUTE_BORDER), 0);
        lv_obj_set_style_text_color(sb.pct, lv_color_hex(HEX_MUTE_BORDER), 0);
        lv_obj_set_style_bg_color(sb.slider, lv_color_hex(0x475569), LV_PART_INDICATOR);
        lv_obj_set_style_border_color(sb.slider, lv_color_hex(HEX_MUTE_BORDER), LV_PART_KNOB);
    }
    else
    {
        lv_label_set_text(sb.icon, active_icon);
        lv_obj_set_style_text_color(sb.icon, lv_color_hex(ch.accent_hex), 0);
        lv_obj_set_style_text_color(sb.pct, lv_color_hex(ch.accent_hex), 0);
        lv_obj_set_style_bg_color(sb.slider, lv_color_hex(ch.accent_hex), LV_PART_INDICATOR);
        lv_obj_set_style_border_color(sb.slider, lv_color_hex(ch.accent_hex), LV_PART_KNOB);
    }
}

void update_channel_visuals(ChannelUi &ch)
{
    if (ch.pct_label)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", ch.value);
        lv_label_set_text(ch.pct_label, buf);
    }

    if (ch.slider && ch.mute_btn && ch.mute_label && ch.pct_label)
    {
        if (ch.muted)
        {
            lv_obj_set_style_text_color(ch.pct_label, lv_color_hex(HEX_MUTE_BORDER), 0);
            lv_obj_set_style_bg_color(ch.slider, lv_color_hex(0x475569), LV_PART_INDICATOR);
            lv_obj_set_style_border_color(ch.slider, lv_color_hex(HEX_MUTE_BORDER), LV_PART_KNOB);

            lv_obj_set_style_bg_color(ch.mute_btn, lv_color_hex(HEX_MUTE_BG), 0);
            lv_obj_set_style_border_color(ch.mute_btn, lv_color_hex(HEX_MUTE_BORDER), 0);
            lv_label_set_text(ch.mute_label, "MUTADO");
            lv_obj_set_style_text_color(ch.mute_label, lv_color_hex(HEX_TEXT_MAIN), 0);
        }
        else
        {
            lv_obj_set_style_text_color(ch.pct_label, lv_color_hex(ch.accent_hex), 0);
            lv_obj_set_style_bg_color(ch.slider, lv_color_hex(ch.accent_hex), LV_PART_INDICATOR);
            lv_obj_set_style_border_color(ch.slider, lv_color_hex(ch.accent_hex), LV_PART_KNOB);

            lv_obj_set_style_bg_color(ch.mute_btn, lv_color_hex(HEX_BTN_IDLE), 0);
            lv_obj_set_style_border_color(ch.mute_btn, lv_color_hex(HEX_CARD_BORDER), 0);
            lv_label_set_text(ch.mute_label, "SOM");
            lv_obj_set_style_text_color(ch.mute_label, lv_color_hex(HEX_TEXT_DIM), 0);
        }
    }

    if (strcasecmp(ch.target, "master") == 0)
    {
        for (SidebarWidgets *msb : g_master_sidebars)
        {
            update_sidebar_visuals(*msb, ch, PH_ICN_VOL);
        }
    }
    else if (strcasecmp(ch.target, "browser") == 0)
    {
        update_sidebar_visuals(g_media_sidebar, ch, PH_ICN_BROWSER);
    }
    else if (strcasecmp(ch.target, "game") == 0)
    {
        update_sidebar_visuals(g_hud_sidebar, ch, PH_ICN_GAME);
    }
}

void update_mic_visuals()
{
    for (int i = 0; i < NUM_SCREENS; ++i)
    {
        HeaderMicWidgets &m = g_header_mic[i];
        if (!m.btn || !m.icon || !m.label) continue;

        if (g_mic_muted)
        {
            lv_obj_set_style_bg_color(m.btn, lv_color_hex(HEX_MUTE_BG), 0);
            lv_obj_set_style_border_color(m.btn, lv_color_hex(HEX_MUTE_BORDER), 0);
            lv_label_set_text(m.icon, PH_ICN_MIC_OFF);
            lv_obj_set_style_text_color(m.icon, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(m.label, "MUTE");
            lv_obj_set_style_text_color(m.label, lv_color_hex(0xFFFFFF), 0);
        }
        else
        {
            lv_obj_set_style_bg_color(m.btn, lv_color_hex(HEX_MIC_OK_BG), 0);
            lv_obj_set_style_border_color(m.btn, lv_color_hex(HEX_MIC_OK_BDR), 0);
            lv_label_set_text(m.icon, PH_ICN_MIC_ON);
            lv_obj_set_style_text_color(m.icon, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(m.label, "MIC");
            lv_obj_set_style_text_color(m.label, lv_color_hex(0xFFFFFF), 0);
        }
    }
}

void update_media_play_visuals()
{
    if (g_media_play_btn && g_media_play_icon)
    {
        if (g_media_playing)
        {
            lv_obj_set_style_bg_color(g_media_play_btn, lv_color_hex(0xE11D48), 0);
            lv_obj_set_style_border_color(g_media_play_btn, lv_color_hex(HEX_ROSE), 0);
            lv_label_set_text(g_media_play_icon, PH_ICN_PAUSE);
        }
        else
        {
            lv_obj_set_style_bg_color(g_media_play_btn, lv_color_hex(0x0891B2), 0);
            lv_obj_set_style_border_color(g_media_play_btn, lv_color_hex(HEX_CYAN), 0);
            lv_label_set_text(g_media_play_icon, PH_ICN_PLAY);
        }
    }

    if (g_media_state_lbl)
    {
        if (g_media_playing)
        {
            lv_label_set_text(g_media_state_lbl, "TOCANDO AGORA");
            lv_obj_set_style_text_color(g_media_state_lbl, lv_color_hex(HEX_ROSE), 0);
        }
        else
        {
            lv_label_set_text(g_media_state_lbl, "PAUSADO");
            lv_obj_set_style_text_color(g_media_state_lbl, lv_color_hex(HEX_TEXT_DIM), 0);
        }
    }

    if (g_media_icon_box && g_media_icon_lbl)
    {
        if (g_media_playing)
        {
            lv_obj_set_style_bg_color(g_media_icon_box, lv_color_hex(0x3B1024), 0);
            lv_obj_set_style_border_color(g_media_icon_box, lv_color_hex(HEX_ROSE), 0);
            lv_obj_set_style_text_color(g_media_icon_lbl, lv_color_hex(HEX_ROSE), 0);
        }
        else
        {
            lv_obj_set_style_bg_color(g_media_icon_box, lv_color_hex(0x1E293B), 0);
            lv_obj_set_style_border_color(g_media_icon_box, lv_color_hex(0x475569), 0);
            lv_obj_set_style_text_color(g_media_icon_lbl, lv_color_hex(HEX_TEXT_DIM), 0);
        }
    }
}

void emit_volume_if_needed(ChannelUi &ch, bool force)
{
    const uint32_t now = millis();
    if (ch.value == ch.last_sent_value) return;
    if (!force && (now - ch.last_send_ms) < SLIDER_THROTTLE_MS) return;

    ch.last_sent_value = ch.value;
    ch.last_send_ms = now;

    if (g_vol_cb)
    {
        g_vol_cb(ch.target, ch.value);
    }
}

void toast_hide_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_home_toast)
    {
        lv_obj_add_flag(g_home_toast, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_toast_timer)
    {
        lv_timer_pause(g_toast_timer);
    }
}

void show_home_toast(const char *msg)
{
    if (!g_home_toast || !g_home_toast_label || !msg) return;

    lv_label_set_text(g_home_toast_label, msg);
    lv_obj_clear_flag(g_home_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_home_toast);

    if (!g_toast_timer)
    {
        g_toast_timer = lv_timer_create(toast_hide_timer_cb, TOAST_DURATION_MS, nullptr);
    }
    else
    {
        lv_timer_set_period(g_toast_timer, TOAST_DURATION_MS);
        lv_timer_reset(g_toast_timer);
        lv_timer_resume(g_toast_timer);
    }
}

// ============================================================================
// Callbacks de Eventos
// ============================================================================
void sidebar_slider_cb(lv_event_t *e)
{
    auto *sb = static_cast<SidebarWidgets *>(lv_event_get_user_data(e));
    if (!sb) return;

    const bool is_browser = (sb == &g_media_sidebar);
    const bool is_game    = (sb == &g_hud_sidebar);
    ChannelUi &ch = is_browser ? g_channels[1] : (is_game ? g_channels[3] : g_channels[0]);
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED)
    {
        sb->dragging = true;
    }
    else if (code == LV_EVENT_VALUE_CHANGED)
    {
        if (g_syncing_from_pc) return;

        ch.value = clamp_vol(static_cast<int>(lv_slider_get_value(sb->slider)));
        g_syncing_from_pc = true;
        if (ch.slider)
        {
            lv_slider_set_value(ch.slider, ch.value, LV_ANIM_OFF);
        }
        if (!is_browser && !is_game)
        {
            for (SidebarWidgets *msb : g_master_sidebars)
            {
                if (sb != msb && msb->slider)
                {
                    lv_slider_set_value(msb->slider, ch.value, LV_ANIM_OFF);
                }
            }
        }
        g_syncing_from_pc = false;

        update_channel_visuals(ch);
        emit_volume_if_needed(ch, false);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        sb->dragging = false;
        if (!g_syncing_from_pc)
        {
            ch.value = clamp_vol(static_cast<int>(lv_slider_get_value(sb->slider)));
            emit_volume_if_needed(ch, true);
        }
    }
}

void sidebar_mute_tap_cb(lv_event_t *e)
{
    const int ch_idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (ch_idx < 0 || ch_idx > 3 || g_syncing_from_pc) return;

    ChannelUi &ch = g_channels[ch_idx];
    ch.muted = !ch.muted;
    update_channel_visuals(ch);

    if (g_mute_cb)
    {
        g_mute_cb(ch.target);
    }
}

void audio_slider_event_cb(lv_event_t *e)
{
    auto *ch = static_cast<ChannelUi *>(lv_event_get_user_data(e));
    if (!ch) return;

    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED)
    {
        ch->dragging = true;
    }
    else if (code == LV_EVENT_VALUE_CHANGED)
    {
        if (g_syncing_from_pc) return;

        ch->value = clamp_vol(static_cast<int>(lv_slider_get_value(ch->slider)));

        if (strcasecmp(ch->target, "master") == 0)
        {
            g_syncing_from_pc = true;
            for (SidebarWidgets *msb : g_master_sidebars)
            {
                if (msb->slider)
                {
                    lv_slider_set_value(msb->slider, ch->value, LV_ANIM_OFF);
                }
            }
            g_syncing_from_pc = false;
        }
        else if (strcasecmp(ch->target, "browser") == 0 && g_media_sidebar.slider)
        {
            g_syncing_from_pc = true;
            lv_slider_set_value(g_media_sidebar.slider, ch->value, LV_ANIM_OFF);
            g_syncing_from_pc = false;
        }
        else if (strcasecmp(ch->target, "game") == 0 && g_hud_sidebar.slider)
        {
            g_syncing_from_pc = true;
            lv_slider_set_value(g_hud_sidebar.slider, ch->value, LV_ANIM_OFF);
            g_syncing_from_pc = false;
        }

        update_channel_visuals(*ch);
        emit_volume_if_needed(*ch, false);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        ch->dragging = false;
        if (!g_syncing_from_pc)
        {
            ch->value = clamp_vol(static_cast<int>(lv_slider_get_value(ch->slider)));
            emit_volume_if_needed(*ch, true);
        }
    }
}

void channel_mute_event_cb(lv_event_t *e)
{
    auto *ch = static_cast<ChannelUi *>(lv_event_get_user_data(e));
    if (!ch || g_syncing_from_pc) return;

    ch->muted = !ch->muted;
    update_channel_visuals(*ch);

    if (g_mute_cb)
    {
        g_mute_cb(ch->target);
    }
}

void mic_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (g_syncing_from_pc) return;

    g_mic_muted = !g_mic_muted;
    update_mic_visuals();

    if (g_mute_cb)
    {
        g_mute_cb("mic");
    }
}

void home_btn_event_cb(lv_event_t *e)
{
    const auto screen_id = static_cast<UiScreenId>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));

    if (screen_id == UI_SCREEN_HOME)
    {
        if (g_screen_cb)
        {
            g_screen_cb("home");
        }
        return;
    }

    ui_show_screen(UI_SCREEN_HOME, true);
}

void hub_card_click_cb(lv_event_t *e)
{
    const auto *card = static_cast<const HubCardDef *>(lv_event_get_user_data(e));
    if (!card) return;

    if (card->target_screen >= 0)
    {
        ui_show_screen(static_cast<UiScreenId>(card->target_screen), true);
        return;
    }

    Serial.printf("{\"event\":\"ui_card\",\"card\":\"%s\"}\n", card->id);

    char msg[48];
    if (strcmp(card->id, "network") == 0 && g_wifi_connected && g_wifi_ip[0] != '\0')
    {
        snprintf(msg, sizeof(msg), "Wi-Fi: %s", g_wifi_ip);
    }
    else
    {
        snprintf(msg, sizeof(msg), "%s: Em breve", card->title);
    }
    show_home_toast(msg);
}

void media_action_btn_cb(lv_event_t *e)
{
    const char *action = static_cast<const char *>(lv_event_get_user_data(e));
    if (!action) return;

    if (strcmp(action, "play_pause") == 0)
    {
        g_media_playing = !g_media_playing;
        update_media_play_visuals();
    }

    if (g_media_cb)
    {
        g_media_cb(action);
    }
}

void macro_btn_cb(lv_event_t *e)
{
    const char *action = static_cast<const char *>(lv_event_get_user_data(e));
    if (!action) return;

    if (g_macro_cb)
    {
        g_macro_cb(action);
    }
}

struct GameCardDef {
    const char *id;
    const char *title;
    const char *hud_title;
    const char *subtitle;
    const char *icon_utf8;
    uint32_t accent_hex;
    uint32_t press_bg_hex;
    bool is_cs2;

    GameCardDef(
        const char *i,
        const char *t,
        const char *ht,
        const char *sub,
        const char *ic,
        uint32_t acc,
        uint32_t pbg,
        bool cs2)
        : id(i),
          title(t),
          hud_title(ht),
          subtitle(sub),
          icon_utf8(ic),
          accent_hex(acc),
          press_bg_hex(pbg),
          is_cs2(cs2) {}
};

const GameCardDef g_game_cards[4] = {
    GameCardDef("cs2",       "CS2",        "CS2",        "GSI C4 + HUD",  PH_ICN_CROSSHAIR, 0xF59E0B, 0x3B2308, true),
    GameCardDef("pubg",      "PUBG",       "PUBG",       "BATTLEGROUNDS", PH_ICN_GAME,      0x38BDF8, 0x0A2A43, false),
    GameCardDef("watchdogs", "WATCH DOGS", "WATCH DOGS", "GAME HUD",      PH_ICN_FIRE,      0xA855F7, 0x2E1065, false),
    GameCardDef("custom",    "OUTROS",     "GAME HUD",   "BIBLIOTECA",    PH_ICN_APP,       0x34D399, 0x064E3B, false),
};

void configure_game_hud(const GameCardDef &game)
{
    g_hud_cs2_mode = game.is_cs2;

    if (g_hud_header_icon)
    {
        lv_label_set_text(g_hud_header_icon, game.icon_utf8);
        lv_obj_set_style_text_color(g_hud_header_icon, lv_color_hex(game.accent_hex), 0);
    }
    if (g_hud_header_title)
    {
        lv_label_set_text(g_hud_header_title, game.hud_title);
    }

    if (g_hud_cs2_panel && g_hud_gen_panel)
    {
        if (game.is_cs2)
        {
            lv_obj_clear_flag(g_hud_cs2_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g_hud_gen_panel, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(g_hud_cs2_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_hud_gen_panel, LV_OBJ_FLAG_HIDDEN);

            if (g_hud_gen_accent)
            {
                lv_obj_set_style_bg_color(g_hud_gen_accent, lv_color_hex(game.accent_hex), 0);
            }
            if (g_hud_gen_icon_box)
            {
                lv_obj_set_style_bg_color(g_hud_gen_icon_box, lv_color_hex(game.press_bg_hex), 0);
                lv_obj_set_style_border_color(g_hud_gen_icon_box, lv_color_hex(game.accent_hex), 0);
            }
            if (g_hud_gen_icon_lbl)
            {
                lv_label_set_text(g_hud_gen_icon_lbl, game.icon_utf8);
                lv_obj_set_style_text_color(g_hud_gen_icon_lbl, lv_color_hex(game.accent_hex), 0);
            }
            if (g_hud_gen_title)
            {
                char sbuf[40];
                snprintf(sbuf, sizeof(sbuf), "SESSÃO: %s", game.hud_title);
                lv_label_set_text(g_hud_gen_title, sbuf);
            }
        }
    }
}

void open_games_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_show_screen(UI_SCREEN_GAMES, true);
}

void game_card_click_cb(lv_event_t *e)
{
    const auto *game = static_cast<const GameCardDef *>(lv_event_get_user_data(e));
    if (!game) return;

    configure_game_hud(*game);
    update_sidebar_visuals(g_hud_sidebar, g_channels[3], PH_ICN_GAME);
    ui_show_screen(UI_SCREEN_GAME_HUD, true);
    if (g_launch_game_cb)
    {
        g_launch_game_cb(game->id);
    }
}

void update_standby_visuals()
{
    if (!g_sys_standby_lbl) return;
    const uint8_t mins = get_standby_minutes();
    if (mins == 0)
    {
        lv_label_set_text(g_sys_standby_lbl, "Desativado");
        lv_obj_set_style_text_color(g_sys_standby_lbl, lv_color_hex(HEX_TEXT_DIM), 0);
    }
    else
    {
        char sbuf[20];
        snprintf(sbuf, sizeof(sbuf), "%u min", static_cast<unsigned>(mins));
        lv_label_set_text(g_sys_standby_lbl, sbuf);
        lv_obj_set_style_text_color(g_sys_standby_lbl, lv_color_hex(0x38BDF8), 0);
    }
}

void update_screensaver_weather_visuals()
{
    if (!g_ss_weather_lbl) return;
    char wbuf[56];
    if (g_has_weather)
    {
        snprintf(wbuf, sizeof(wbuf), "%s  •  %d°C", g_weather_city, g_weather_temp);
    }
    else
    {
        snprintf(wbuf, sizeof(wbuf), "%s  •  --°C", g_weather_city);
    }
    lv_label_set_text(g_ss_weather_lbl, wbuf);
}

void update_system_info_visuals()
{
    if (g_sys_wifi_lbl)
    {
        char wbuf[64];
        if (g_wifi_connected)
        {
            snprintf(
                wbuf,
                sizeof(wbuf),
                "%s  |  %s",
                g_wifi_ssid_str[0] ? g_wifi_ssid_str : "Wi-Fi",
                g_wifi_ip[0] ? g_wifi_ip : "--");
            lv_obj_set_style_text_color(g_sys_wifi_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
        }
        else
        {
            snprintf(wbuf, sizeof(wbuf), "Desconectado");
            lv_obj_set_style_text_color(g_sys_wifi_lbl, lv_color_hex(HEX_WARN), 0);
        }
        lv_label_set_text(g_sys_wifi_lbl, wbuf);
    }

    if (g_sys_ws_lbl)
    {
        if (g_conn_state == UI_CONN_WIFI)
        {
            char hbuf[64];
            if (g_ws_host_ip[0] != '\0')
            {
                snprintf(hbuf, sizeof(hbuf), "WS :81  •  PC %s", g_ws_host_ip);
            }
            else
            {
                snprintf(hbuf, sizeof(hbuf), "WS :81  •  PC Conectado");
            }
            lv_label_set_text(g_sys_ws_lbl, hbuf);
            lv_obj_set_style_text_color(g_sys_ws_lbl, lv_color_hex(0x34D399), 0);
        }
        else if (g_conn_state == UI_CONN_SYNCED)
        {
            lv_label_set_text(g_sys_ws_lbl, "USB Serial  •  PC Conectado");
            lv_obj_set_style_text_color(g_sys_ws_lbl, lv_color_hex(0x34D399), 0);
        }
        else
        {
            lv_label_set_text(g_sys_ws_lbl, "WS :81  •  Aguardando PC");
            lv_obj_set_style_text_color(g_sys_ws_lbl, lv_color_hex(HEX_WARN), 0);
        }
    }
}

void update_brightness_visuals(uint8_t pct, bool sync_slider)
{
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;

    char pbuf[16];
    snprintf(pbuf, sizeof(pbuf), "%u%%", static_cast<unsigned>(pct));

    if (g_bright_pct_lbl)
    {
        lv_label_set_text(g_bright_pct_lbl, pbuf);
    }
    if (sync_slider && g_bright_slider)
    {
        lv_slider_set_value(g_bright_slider, pct, LV_ANIM_OFF);
    }
    if (g_set_bright_sub)
    {
        char sbuf[28];
        snprintf(sbuf, sizeof(sbuf), "Brilho: %u%%", static_cast<unsigned>(pct));
        lv_label_set_text(g_set_bright_sub, sbuf);
    }
}

void update_pc_bt_visuals()
{
    if (!g_set_bt_card || !g_set_bt_icon || !g_set_bt_sub || !g_set_bt_pill || !g_set_bt_pill_lbl) return;

    if (g_pc_bt_enabled)
    {
        lv_obj_set_style_bg_color(g_set_bt_card, lv_color_hex(0x0B2545), 0);
        lv_obj_set_style_border_color(g_set_bt_card, lv_color_hex(0x38BDF8), 0);
        if (g_set_bt_accent)
        {
            lv_obj_set_style_bg_color(g_set_bt_accent, lv_color_hex(0x38BDF8), 0);
        }
        lv_label_set_text(g_set_bt_icon, PH_ICN_BT);
        lv_obj_set_style_text_color(g_set_bt_icon, lv_color_hex(0x38BDF8), 0);
        lv_label_set_text(g_set_bt_sub, "LIGADO (Toque p/ desligar)");
        lv_obj_set_style_text_color(g_set_bt_sub, lv_color_hex(0x7DD3FC), 0);
        lv_obj_set_style_bg_color(g_set_bt_pill, lv_color_hex(0x0284C7), 0);
        lv_label_set_text(g_set_bt_pill_lbl, "ON");
        lv_obj_set_style_text_color(g_set_bt_pill_lbl, lv_color_hex(0xFFFFFF), 0);
    }
    else
    {
        lv_obj_set_style_bg_color(g_set_bt_card, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_border_color(g_set_bt_card, lv_color_hex(HEX_CARD_BORDER), 0);
        if (g_set_bt_accent)
        {
            lv_obj_set_style_bg_color(g_set_bt_accent, lv_color_hex(0x475569), 0);
        }
        lv_label_set_text(g_set_bt_icon, PH_ICN_BT_OFF);
        lv_obj_set_style_text_color(g_set_bt_icon, lv_color_hex(HEX_TEXT_DIM), 0);
        lv_label_set_text(g_set_bt_sub, "DESLIGADO (Toque p/ ligar)");
        lv_obj_set_style_text_color(g_set_bt_sub, lv_color_hex(HEX_TEXT_DIM), 0);
        lv_obj_set_style_bg_color(g_set_bt_pill, lv_color_hex(HEX_TRACK), 0);
        lv_label_set_text(g_set_bt_pill_lbl, "OFF");
        lv_obj_set_style_text_color(g_set_bt_pill_lbl, lv_color_hex(HEX_TEXT_DIM), 0);
    }
}

void update_wifi_card_visuals()
{
    if (g_set_wifi_sub)
    {
        if (g_wifi_connected)
        {
            lv_label_set_text(g_set_wifi_sub, g_wifi_ssid_str[0] ? g_wifi_ssid_str : "Conectado");
            lv_obj_set_style_text_color(g_set_wifi_sub, lv_color_hex(0x34D399), 0);
        }
        else
        {
            lv_label_set_text(g_set_wifi_sub, "Desconectado");
            lv_obj_set_style_text_color(g_set_wifi_sub, lv_color_hex(HEX_WARN), 0);
        }
    }

    update_system_info_visuals();

    if (!g_net_wifi_icon || !g_net_ssid_lbl || !g_net_rssi_lbl || !g_net_ip_lbl) return;

    if (g_wifi_connected)
    {
        lv_label_set_text(g_net_wifi_icon, PH_ICN_WIFI);
        lv_obj_set_style_text_color(g_net_wifi_icon, lv_color_hex(0x34D399), 0);
        lv_label_set_text(g_net_ssid_lbl, g_wifi_ssid_str[0] ? g_wifi_ssid_str : "Wi-Fi Conectado");
        lv_obj_set_style_text_color(g_net_ssid_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);

        char rbuf[24];
        if (g_wifi_rssi < 0)
        {
            snprintf(rbuf, sizeof(rbuf), "%ld dBm", static_cast<long>(g_wifi_rssi));
            uint32_t rcol = 0x34D399;
            if (g_wifi_rssi <= -80) rcol = 0xEF4444;
            else if (g_wifi_rssi <= -70) rcol = HEX_WARN;
            lv_obj_set_style_text_color(g_net_rssi_lbl, lv_color_hex(rcol), 0);
        }
        else
        {
            snprintf(rbuf, sizeof(rbuf), "ONLINE");
            lv_obj_set_style_text_color(g_net_rssi_lbl, lv_color_hex(0x34D399), 0);
        }
        lv_label_set_text(g_net_rssi_lbl, rbuf);

        char ibuf[56];
        snprintf(ibuf, sizeof(ibuf), "IP: %s  |  WS :81", g_wifi_ip[0] ? g_wifi_ip : "--");
        lv_label_set_text(g_net_ip_lbl, ibuf);
    }
    else
    {
        lv_label_set_text(g_net_wifi_icon, PH_ICN_WIFI_OFF);
        lv_obj_set_style_text_color(g_net_wifi_icon, lv_color_hex(HEX_WARN), 0);
        lv_label_set_text(g_net_ssid_lbl, g_wifi_ssid_str[0] ? g_wifi_ssid_str : "Desconectado");
        lv_obj_set_style_text_color(g_net_ssid_lbl, lv_color_hex(HEX_WARN), 0);
        lv_label_set_text(g_net_rssi_lbl, "OFFLINE");
        lv_obj_set_style_text_color(g_net_rssi_lbl, lv_color_hex(0xF87171), 0);
        lv_label_set_text(g_net_ip_lbl, "IP: aguardando rede Wi-Fi...");
    }
}

void pc_bt_toggle_btn_cb(lv_event_t *e)
{
    (void)e;
    // Feedback visual otimista; backend sincroniza o estado real logo em seguida
    g_pc_bt_enabled = !g_pc_bt_enabled;
    update_pc_bt_visuals();
    if (g_pc_bt_cb)
    {
        g_pc_bt_cb();
    }
}

void wifi_scan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_net_scan_status_lbl)
    {
        lv_label_set_text(g_net_scan_status_lbl, "Escaneando...");
        lv_obj_set_style_text_color(g_net_scan_status_lbl, lv_color_hex(HEX_CYAN), 0);
    }
    if (g_wifi_scan_cb)
    {
        g_wifi_scan_cb();
    }
}

void close_wifi_modal()
{
    if (g_wifi_modal)
    {
        lv_obj_add_flag(g_wifi_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

void open_wifi_modal(const UiWifiNetwork &net)
{
    strncpy(g_selected_ssid, net.ssid, sizeof(g_selected_ssid) - 1);
    g_selected_ssid[sizeof(g_selected_ssid) - 1] = '\0';

    if (!net.secured)
    {
        // Rede aberta: conecta diretamente sem exigir senha
        if (g_net_scan_status_lbl)
        {
            char sbuf[52];
            snprintf(sbuf, sizeof(sbuf), "Conectando em %s...", g_selected_ssid);
            lv_label_set_text(g_net_scan_status_lbl, sbuf);
            lv_obj_set_style_text_color(g_net_scan_status_lbl, lv_color_hex(HEX_CYAN), 0);
        }
        if (g_wifi_connect_cb)
        {
            g_wifi_connect_cb(g_selected_ssid, "");
        }
        return;
    }

    if (g_wifi_modal_title)
    {
        char tbuf[56];
        snprintf(tbuf, sizeof(tbuf), "Conectar a: %s", g_selected_ssid);
        lv_label_set_text(g_wifi_modal_title, tbuf);
    }
    if (g_wifi_pwd_ta)
    {
        lv_textarea_set_text(g_wifi_pwd_ta, "");
        lv_textarea_set_password_mode(g_wifi_pwd_ta, true);
        lv_obj_add_state(g_wifi_pwd_ta, LV_STATE_FOCUSED);
    }
    if (g_wifi_pwd_eye_lbl)
    {
        lv_label_set_text(g_wifi_pwd_eye_lbl, LV_SYMBOL_EYE_OPEN);
    }
    if (g_wifi_kb)
    {
        lv_keyboard_set_mode(g_wifi_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(g_wifi_kb, g_wifi_pwd_ta);
    }
    if (g_wifi_modal)
    {
        lv_obj_clear_flag(g_wifi_modal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_wifi_modal);
    }
}

void wifi_list_item_cb(lv_event_t *e)
{
    const int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (idx < 0 || idx >= g_wifi_scan_count) return;
    open_wifi_modal(g_wifi_scan_cache[idx]);
}

void wifi_modal_close_cb(lv_event_t *e)
{
    (void)e;
    close_wifi_modal();
}

void wifi_pwd_eye_cb(lv_event_t *e)
{
    (void)e;
    if (!g_wifi_pwd_ta) return;
    const bool hide = !lv_textarea_get_password_mode(g_wifi_pwd_ta);
    lv_textarea_set_password_mode(g_wifi_pwd_ta, hide);
    if (g_wifi_pwd_eye_lbl)
    {
        lv_label_set_text(g_wifi_pwd_eye_lbl, hide ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
    }
}

void wifi_keyboard_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CANCEL)
    {
        close_wifi_modal();
        return;
    }
    if (code == LV_EVENT_READY)
    {
        const char *pwd = g_wifi_pwd_ta ? lv_textarea_get_text(g_wifi_pwd_ta) : "";
        close_wifi_modal();

        if (g_net_scan_status_lbl)
        {
            char sbuf[52];
            snprintf(sbuf, sizeof(sbuf), "Conectando em %s...", g_selected_ssid);
            lv_label_set_text(g_net_scan_status_lbl, sbuf);
            lv_obj_set_style_text_color(g_net_scan_status_lbl, lv_color_hex(HEX_CYAN), 0);
        }
        if (g_wifi_connect_cb && g_selected_ssid[0] != '\0')
        {
            g_wifi_connect_cb(g_selected_ssid, pwd);
        }
    }
}

void settings_nav_cb(lv_event_t *e)
{
    const auto target_id = static_cast<UiScreenId>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    ui_show_screen(target_id, true);
}

void brightness_slider_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *slider = lv_event_get_target(e);
    if (!slider) return;

    int val = static_cast<int>(lv_slider_get_value(slider));
    if (val < 10) val = 10;
    if (val > 100) val = 100;

    update_brightness_visuals(static_cast<uint8_t>(val), false);

    if (code == LV_EVENT_VALUE_CHANGED)
    {
        set_screen_brightness(static_cast<uint8_t>(val));
    }
    else if (code == LV_EVENT_RELEASED)
    {
        save_screen_brightness(static_cast<uint8_t>(val));
    }
}

void brightness_preset_cb(lv_event_t *e)
{
    const int val = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const uint8_t pct = static_cast<uint8_t>(val < 10 ? 10 : (val > 100 ? 100 : val));
    update_brightness_visuals(pct, true);
    save_screen_brightness(pct);
}

void standby_cycle_btn_cb(lv_event_t *e)
{
    (void)e;
    const uint8_t cur = get_standby_minutes();
    uint8_t next = 2;
    if (cur == 0) next = 2;
    else if (cur == 2) next = 5;
    else if (cur == 5) next = 10;
    else next = 0;

    set_standby_minutes(next);
    update_standby_visuals();
}

void touch_calib_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_sys_calib_lbl)
    {
        lv_label_set_text(g_sys_calib_lbl, "I2C OK");
        lv_obj_set_style_text_color(g_sys_calib_lbl, lv_color_hex(0x34D399), 0);
    }
}

void screensaver_tap_cb(lv_event_t *e)
{
    (void)e;
    ui_wake_screensaver();
}

void system_restart_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_sys_restart_lbl)
    {
        lv_label_set_text(g_sys_restart_lbl, "REINICIANDO MINI DECK...");
        lv_timer_handler();
    }
    restart_esp32_device();
}

// ============================================================================
// Header Universal Ergonomico (58px de altura, botões 48px úteis)
//   [ HORA (x=12) ]  [ HOME (62x48) ]  --- CENTRO ---  [ MIC (90x48) ]
// ============================================================================
void build_universal_header(
    lv_obj_t *scr,
    UiScreenId screen_id,
    const char *center_icon_utf8,
    const char *center_title,
    uint32_t center_accent_hex)
{
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 6, 4);
    lv_obj_set_size(header, 468, 58);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(HEX_CARD_BORDER), 0);
    lv_obj_set_style_radius(header, 14, 0);

    const bool is_home = (screen_id == UI_SCREEN_HOME);

    // 1. Extrema Esquerda (Fixo em TODAS as telas): Relógio Digital NTP (x=12)
    lv_obj_t *clk = lv_label_create(header);
    lv_label_set_text(clk, g_last_clock);
    lv_obj_set_style_text_font(clk, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(clk, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_align(clk, LV_ALIGN_LEFT_MID, 12, 0);
    g_header_clock[screen_id] = clk;

    // 2. Imediatamente à Direita da Hora: Botão [ HOME ] amplo (62 x 48 px)
    lv_obj_t *home_btn = lv_btn_create(header);
    lv_obj_remove_style_all(home_btn);
    lv_obj_set_size(home_btn, 62, 48);
    lv_obj_align(home_btn, LV_ALIGN_LEFT_MID, 80, 0);
    lv_obj_set_ext_click_area(home_btn, 8);
    lv_obj_set_style_radius(home_btn, 12, 0);
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_btn, 1, 0);

    if (is_home)
    {
        lv_obj_set_style_bg_color(home_btn, lv_color_hex(0x162033), 0);
        lv_obj_set_style_bg_color(home_btn, lv_color_hex(HEX_BTN_IDLE), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(home_btn, lv_color_hex(HEX_CARD_BORDER), 0);
    }
    else
    {
        lv_obj_set_style_bg_color(home_btn, lv_color_hex(HEX_BTN_IDLE), 0);
        lv_obj_set_style_bg_color(home_btn, lv_color_hex(0x083344), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(home_btn, 2, 0);
        lv_obj_set_style_border_color(home_btn, lv_color_hex(HEX_CYAN), 0);
    }
    lv_obj_add_event_cb(
        home_btn,
        home_btn_event_cb,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(static_cast<intptr_t>(screen_id)));

    lv_obj_t *home_ic = lv_label_create(home_btn);
    lv_label_set_text(home_ic, LV_SYMBOL_HOME);
    lv_obj_set_style_text_font(home_ic, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(home_ic, lv_color_hex(is_home ? 0x64748B : HEX_CYAN), 0);
    lv_obj_center(home_ic);

    // 3. Centro Dinâmico (entre HOME x=142 e MIC x=373 -> x=150..368)
    lv_obj_t *center_box = lv_obj_create(header);
    lv_obj_remove_style_all(center_box);
    lv_obj_set_size(center_box, 218, 42);
    lv_obj_align(center_box, LV_ALIGN_LEFT_MID, 150, 0);
    lv_obj_clear_flag(center_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(center_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(center_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(center_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(center_box, 8, 0);

    if (is_home)
    {
        g_home_conn_badge = lv_obj_create(center_box);
        lv_obj_remove_style_all(g_home_conn_badge);
        lv_obj_set_size(g_home_conn_badge, 118, 30);
        lv_obj_clear_flag(g_home_conn_badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(g_home_conn_badge, 15, 0);
        lv_obj_set_style_bg_opa(g_home_conn_badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_home_conn_badge, 1, 0);

        g_home_conn_label = lv_label_create(g_home_conn_badge);
        lv_obj_set_style_text_font(g_home_conn_label, &lv_font_montserrat_12, 0);
        lv_obj_center(g_home_conn_label);
    }
    else
    {
        lv_obj_t *mod_ic = nullptr;
        if (center_icon_utf8 && center_icon_utf8[0] != '\0')
        {
            mod_ic = lv_label_create(center_box);
            lv_label_set_text(mod_ic, center_icon_utf8);
            lv_obj_set_style_text_font(mod_ic, &phosphor_20, 0);
            lv_obj_set_style_text_color(mod_ic, lv_color_hex(center_accent_hex), 0);
        }

        lv_obj_t *mod_title = lv_label_create(center_box);
        lv_label_set_text(mod_title, center_title ? center_title : "");
        lv_obj_set_style_text_font(mod_title, &inter_bold_16, 0);
        lv_obj_set_style_text_color(mod_title, lv_color_hex(HEX_TEXT_MAIN), 0);

        if (screen_id == UI_SCREEN_GAME_HUD)
        {
            g_hud_header_icon = mod_ic;
            g_hud_header_title = mod_title;
        }
    }

    // 4. Extrema Direita (Fixo em TODAS as telas): Botão [ MIC ] em pílula ampla (90 x 48 px)
    HeaderMicWidgets &mic = g_header_mic[screen_id];
    mic.btn = lv_btn_create(header);
    lv_obj_remove_style_all(mic.btn);
    lv_obj_set_size(mic.btn, 90, 48);
    lv_obj_align(mic.btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_ext_click_area(mic.btn, 8);
    lv_obj_set_style_radius(mic.btn, 13, 0);
    lv_obj_set_style_bg_opa(mic.btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mic.btn, 2, 0);
    lv_obj_set_style_bg_opa(mic.btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_layout(mic.btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mic.btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mic.btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mic.btn, 5, 0);
    lv_obj_add_event_cb(mic.btn, mic_btn_event_cb, LV_EVENT_CLICKED, nullptr);

    mic.icon = lv_label_create(mic.btn);
    lv_obj_set_style_text_font(mic.icon, &phosphor_20, 0);

    mic.label = lv_label_create(mic.btn);
    lv_obj_set_style_text_font(mic.label, &inter_bold_16, 0);
}

// ============================================================================
// Sidebar Esquerda Persistente (alinhada sob o header de 58px: y=68..314)
// ============================================================================
void build_volume_sidebar(
    lv_obj_t *scr,
    SidebarWidgets &sb,
    int channel_idx,
    const char *icon_utf8,
    const char *tag_text = nullptr)
{
    const ChannelUi &ch = g_channels[channel_idx];

    lv_obj_t *sidebar = lv_obj_create(scr);
    lv_obj_remove_style_all(sidebar);
    lv_obj_set_pos(sidebar, 6, 68);
    lv_obj_set_size(sidebar, 70, 246);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(sidebar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(sidebar, 1, 0);
    lv_obj_set_style_border_color(sidebar, lv_color_hex(HEX_CARD_BORDER), 0);
    lv_obj_set_style_radius(sidebar, 14, 0);

    lv_obj_t *accent = lv_obj_create(sidebar);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 34, 3);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(ch.accent_hex), 0);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *ic_btn = lv_btn_create(sidebar);
    lv_obj_remove_style_all(ic_btn);
    lv_obj_set_size(ic_btn, 58, 26);
    lv_obj_align(ic_btn, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_radius(ic_btn, 8, 0);
    lv_obj_set_style_bg_opa(ic_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(ic_btn, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ic_btn, lv_color_hex(HEX_TRACK), LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        ic_btn,
        sidebar_mute_tap_cb,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(static_cast<intptr_t>(channel_idx)));

    sb.icon = lv_label_create(ic_btn);
    lv_label_set_text(sb.icon, icon_utf8);
    lv_obj_set_style_text_font(sb.icon, &phosphor_20, 0);
    lv_obj_set_style_text_color(sb.icon, lv_color_hex(ch.accent_hex), 0);
    lv_obj_center(sb.icon);

    const bool has_tag = (tag_text != nullptr && tag_text[0] != '\0');
    if (has_tag)
    {
        lv_obj_t *tag_lbl = lv_label_create(sidebar);
        lv_label_set_text(tag_lbl, tag_text);
        lv_obj_set_style_text_font(tag_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(tag_lbl, lv_color_hex(HEX_TEXT_DIM), 0);
        lv_obj_align(tag_lbl, LV_ALIGN_TOP_MID, 0, 32);
    }

    sb.slider = lv_slider_create(sidebar);
    lv_slider_set_range(sb.slider, 0, 100);
    lv_slider_set_value(sb.slider, ch.value, LV_ANIM_OFF);
    lv_obj_set_size(sb.slider, 18, has_tag ? 142 : 156);
    lv_obj_align(sb.slider, LV_ALIGN_TOP_MID, 0, has_tag ? 54 : 42);
    lv_obj_set_ext_click_area(sb.slider, 22);

    lv_obj_set_style_bg_opa(sb.slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sb.slider, lv_color_hex(HEX_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(sb.slider, 9, LV_PART_MAIN);

    lv_obj_set_style_bg_opa(sb.slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sb.slider, lv_color_hex(ch.accent_hex), LV_PART_INDICATOR);
    lv_obj_set_style_radius(sb.slider, 9, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(sb.slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sb.slider, lv_color_hex(HEX_TEXT_MAIN), LV_PART_KNOB);
    lv_obj_set_style_border_width(sb.slider, 3, LV_PART_KNOB);
    lv_obj_set_style_border_color(sb.slider, lv_color_hex(ch.accent_hex), LV_PART_KNOB);
    lv_obj_set_style_radius(sb.slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sb.slider, 6, LV_PART_KNOB);

    lv_obj_add_event_cb(sb.slider, sidebar_slider_cb, LV_EVENT_PRESSED, &sb);
    lv_obj_add_event_cb(sb.slider, sidebar_slider_cb, LV_EVENT_VALUE_CHANGED, &sb);
    lv_obj_add_event_cb(sb.slider, sidebar_slider_cb, LV_EVENT_RELEASED, &sb);
    lv_obj_add_event_cb(sb.slider, sidebar_slider_cb, LV_EVENT_PRESS_LOST, &sb);

    sb.pct = lv_label_create(sidebar);
    lv_label_set_text(sb.pct, "100%");
    lv_obj_set_style_text_font(sb.pct, &inter_bold_16, 0);
    lv_obj_set_style_text_color(sb.pct, lv_color_hex(ch.accent_hex), 0);
    lv_obj_align(sb.pct, LV_ALIGN_BOTTOM_MID, 0, -9);
}

// ============================================================================
// Tela 1: screen_home (Header 58px + Sidebar Master + 6 Cards Minimalistas)
// ============================================================================
void build_screen_home()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_HOME] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_HOME, nullptr, nullptr, HEX_CYAN);
    build_volume_sidebar(scr, g_home_sidebar, 0, PH_ICN_VOL, "GERAL");

    // Grade 2x3 abaixo do header de 58px: x=84..474 (390px), y=68..314 (246px)
    constexpr int16_t GRID_X = 84;
    constexpr int16_t GRID_Y = 68;
    constexpr int16_t CARD_W = 124;
    constexpr int16_t CARD_H = 118;
    constexpr int16_t GAP_X  = 9;
    constexpr int16_t GAP_Y  = 10;

    for (int i = 0; i < 6; ++i)
    {
        const HubCardDef &def = g_hub_cards[i];
        const int col = i % 3;
        const int row = i / 3;
        const int16_t x = GRID_X + col * (CARD_W + GAP_X);
        const int16_t y = GRID_Y + row * (CARD_H + GAP_Y);

        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_remove_style_all(btn);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, CARD_W, CARD_H);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(HEX_CARD_BORDER), 0);

        lv_obj_set_style_bg_color(btn, lv_color_hex(def.press_bg_hex), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(def.accent_hex), LV_STATE_PRESSED);

        // Apenas ícone Phosphor 42px centralizado (minimalista)
        lv_obj_t *icon_lbl = lv_label_create(btn);
        lv_label_set_text(icon_lbl, def.icon_utf8);
        lv_obj_set_style_text_font(icon_lbl, &phosphor_42, 0);
        lv_obj_set_style_text_color(icon_lbl, lv_color_hex(def.accent_hex), 0);
        lv_obj_center(icon_lbl);

        lv_obj_add_event_cb(btn, hub_card_click_cb, LV_EVENT_CLICKED, const_cast<HubCardDef *>(&def));
    }

    // Toast flutuante discreto
    g_home_toast = lv_obj_create(scr);
    lv_obj_remove_style_all(g_home_toast);
    lv_obj_set_size(g_home_toast, 236, 34);
    lv_obj_align(g_home_toast, LV_ALIGN_BOTTOM_MID, 39, -10);
    lv_obj_set_style_radius(g_home_toast, 17, 0);
    lv_obj_set_style_bg_opa(g_home_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_home_toast, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_width(g_home_toast, 1, 0);
    lv_obj_set_style_border_color(g_home_toast, lv_color_hex(HEX_CYAN), 0);
    lv_obj_add_flag(g_home_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_home_toast, LV_OBJ_FLAG_CLICKABLE);

    g_home_toast_label = lv_label_create(g_home_toast);
    lv_label_set_text(g_home_toast_label, "");
    lv_obj_set_style_text_font(g_home_toast_label, &inter_medium_16, 0);
    lv_obj_set_style_text_color(g_home_toast_label, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_center(g_home_toast_label);
}

// ============================================================================
// Tela 2: screen_audio (Header 58px + 4 Colunas de Mixer em y=68..314)
// ============================================================================
void build_audio_channel_card(lv_obj_t *scr, ChannelUi &ch, int16_t x)
{
    constexpr int16_t CARD_W = 111;
    constexpr int16_t CARD_H = 246;
    constexpr int16_t CARD_Y = 68;

    ch.card = lv_obj_create(scr);
    lv_obj_remove_style_all(ch.card);
    lv_obj_set_pos(ch.card, x, CARD_Y);
    lv_obj_set_size(ch.card, CARD_W, CARD_H);
    lv_obj_clear_flag(ch.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ch.card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ch.card, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(ch.card, 1, 0);
    lv_obj_set_style_border_color(ch.card, lv_color_hex(HEX_CARD_BORDER), 0);
    lv_obj_set_style_radius(ch.card, 14, 0);

    lv_obj_t *accent = lv_obj_create(ch.card);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 48, 3);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(ch.accent_hex), 0);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);

    ch.name_label = lv_label_create(ch.card);
    lv_label_set_text(ch.name_label, ch.default_name);
    lv_label_set_long_mode(ch.name_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(ch.name_label, CARD_W - 10);
    lv_obj_set_style_text_align(ch.name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ch.name_label, &inter_bold_16, 0);
    lv_obj_set_style_text_color(ch.name_label, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_align(ch.name_label, LV_ALIGN_TOP_MID, 0, 10);

    ch.pct_label = lv_label_create(ch.card);
    lv_label_set_text(ch.pct_label, "100%");
    lv_obj_set_style_text_font(ch.pct_label, &inter_bold_16, 0);
    lv_obj_set_style_text_color(ch.pct_label, lv_color_hex(ch.accent_hex), 0);
    lv_obj_align(ch.pct_label, LV_ALIGN_TOP_MID, 0, 32);

    ch.slider = lv_slider_create(ch.card);
    lv_slider_set_range(ch.slider, 0, 100);
    lv_slider_set_value(ch.slider, ch.value, LV_ANIM_OFF);
    lv_obj_set_size(ch.slider, 18, 126);
    lv_obj_align(ch.slider, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_ext_click_area(ch.slider, 22);

    lv_obj_set_style_bg_opa(ch.slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ch.slider, lv_color_hex(HEX_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(ch.slider, 9, LV_PART_MAIN);

    lv_obj_set_style_bg_opa(ch.slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ch.slider, lv_color_hex(ch.accent_hex), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ch.slider, 9, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(ch.slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(ch.slider, lv_color_hex(HEX_TEXT_MAIN), LV_PART_KNOB);
    lv_obj_set_style_border_width(ch.slider, 3, LV_PART_KNOB);
    lv_obj_set_style_border_color(ch.slider, lv_color_hex(ch.accent_hex), LV_PART_KNOB);
    lv_obj_set_style_radius(ch.slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(ch.slider, 6, LV_PART_KNOB);

    lv_obj_add_event_cb(ch.slider, audio_slider_event_cb, LV_EVENT_PRESSED, &ch);
    lv_obj_add_event_cb(ch.slider, audio_slider_event_cb, LV_EVENT_VALUE_CHANGED, &ch);
    lv_obj_add_event_cb(ch.slider, audio_slider_event_cb, LV_EVENT_RELEASED, &ch);
    lv_obj_add_event_cb(ch.slider, audio_slider_event_cb, LV_EVENT_PRESS_LOST, &ch);

    ch.mute_btn = lv_btn_create(ch.card);
    lv_obj_remove_style_all(ch.mute_btn);
    lv_obj_set_size(ch.mute_btn, 93, 32);
    lv_obj_align(ch.mute_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(ch.mute_btn, 9, 0);
    lv_obj_set_style_bg_opa(ch.mute_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ch.mute_btn, 1, 0);
    lv_obj_set_style_bg_opa(ch.mute_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_add_event_cb(ch.mute_btn, channel_mute_event_cb, LV_EVENT_CLICKED, &ch);

    ch.mute_label = lv_label_create(ch.mute_btn);
    lv_obj_set_style_text_font(ch.mute_label, &lv_font_montserrat_12, 0);
    lv_obj_center(ch.mute_label);

    update_channel_visuals(ch);
}

void build_screen_audio()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_AUDIO] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_AUDIO, PH_ICN_MIXER, "MIXER", HEX_CYAN);

    const int16_t colX[4] = {6, 125, 244, 363};
    for (int i = 0; i < 4; ++i)
    {
        build_audio_channel_card(scr, g_channels[i], colX[i]);
    }
}

// ============================================================================
// Tela 3: screen_media (Header 58px + Sidebar Edge + Player em y=68..314)
// ============================================================================
void build_screen_media()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_MEDIA] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_MEDIA, PH_ICN_MUSIC, "MÍDIA", HEX_ROSE);
    build_volume_sidebar(scr, g_media_sidebar, 1, PH_ICN_BROWSER);

    // Painel Central "Now Playing": x=84, y=68, w=390, h=138
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 84, 68);
    lv_obj_set_size(card, 390, 138);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(HEX_CARD_BORDER), 0);

    lv_obj_t *accent = lv_obj_create(card);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 60, 3);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(HEX_ROSE), 0);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);

    g_media_icon_box = lv_obj_create(card);
    lv_obj_remove_style_all(g_media_icon_box);
    lv_obj_set_pos(g_media_icon_box, 16, 35);
    lv_obj_set_size(g_media_icon_box, 70, 70);
    lv_obj_set_style_radius(g_media_icon_box, 18, 0);
    lv_obj_set_style_bg_opa(g_media_icon_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_media_icon_box, 2, 0);

    g_media_icon_lbl = lv_label_create(g_media_icon_box);
    lv_label_set_text(g_media_icon_lbl, PH_ICN_MUSIC);
    lv_obj_set_style_text_font(g_media_icon_lbl, &phosphor_42, 0);
    lv_obj_center(g_media_icon_lbl);

    g_media_state_lbl = lv_label_create(card);
    lv_label_set_text(g_media_state_lbl, "PAUSADO");
    lv_obj_set_pos(g_media_state_lbl, 100, 20);
    lv_obj_set_style_text_font(g_media_state_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_media_state_lbl, lv_color_hex(HEX_TEXT_DIM), 0);

    g_media_title_lbl = lv_label_create(card);
    lv_label_set_text(g_media_title_lbl, "Nenhuma mídia");
    lv_label_set_long_mode(g_media_title_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(g_media_title_lbl, 100, 48);
    lv_obj_set_width(g_media_title_lbl, 276);
    lv_obj_set_style_text_font(g_media_title_lbl, &inter_bold_16, 0);
    lv_obj_set_style_text_color(g_media_title_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_set_style_anim_speed(g_media_title_lbl, 34, 0);

    g_media_artist_lbl = lv_label_create(card);
    lv_label_set_text(g_media_artist_lbl, "Aguardando reprodução");
    lv_label_set_long_mode(g_media_artist_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(g_media_artist_lbl, 100, 84);
    lv_obj_set_width(g_media_artist_lbl, 276);
    lv_obj_set_style_text_font(g_media_artist_lbl, &inter_medium_16, 0);
    lv_obj_set_style_text_color(g_media_artist_lbl, lv_color_hex(HEX_TEXT_DIM), 0);
    lv_obj_set_style_anim_speed(g_media_artist_lbl, 28, 0);

    // Barra de Ações centralizada na área direita (x=84..474): Anterior | Play/Pause | Próximo
    auto create_side_media_btn = [&](int16_t x, const char *icon_utf8, const char *action_name) {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_remove_style_all(btn);
        lv_obj_set_pos(btn, x, 222);
        lv_obj_set_size(btn, 94, 80);
        lv_obj_set_style_radius(btn, 18, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(HEX_CARD_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(HEX_CARD_BORDER), 0);
        lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(HEX_ROSE), LV_STATE_PRESSED);
        lv_obj_add_event_cb(btn, media_action_btn_cb, LV_EVENT_CLICKED, const_cast<char *>(action_name));

        lv_obj_t *ic = lv_label_create(btn);
        lv_label_set_text(ic, icon_utf8);
        lv_obj_set_style_text_font(ic, &phosphor_32, 0);
        lv_obj_set_style_text_color(ic, lv_color_hex(HEX_TEXT_MAIN), 0);
        lv_obj_center(ic);
    };

    create_side_media_btn(114, PH_ICN_PREV, "prev");
    create_side_media_btn(350, PH_ICN_NEXT, "next");

    g_media_play_btn = lv_btn_create(scr);
    lv_obj_remove_style_all(g_media_play_btn);
    lv_obj_set_pos(g_media_play_btn, 222, 216);
    lv_obj_set_size(g_media_play_btn, 114, 94);
    lv_obj_set_style_radius(g_media_play_btn, 24, 0);
    lv_obj_set_style_bg_opa(g_media_play_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(g_media_play_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(g_media_play_btn, 2, 0);
    lv_obj_add_event_cb(g_media_play_btn, media_action_btn_cb, LV_EVENT_CLICKED, const_cast<char *>("play_pause"));

    g_media_play_icon = lv_label_create(g_media_play_btn);
    lv_obj_set_style_text_font(g_media_play_icon, &phosphor_42, 0);
    lv_obj_set_style_text_color(g_media_play_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(g_media_play_icon);

    update_media_play_visuals();
}

// ============================================================================
// Tela 4: screen_hardware (Header 58px + Sidebar Master + Cards CPU, GPU & RAM)
// ============================================================================
void build_screen_hardware()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_HARDWARE] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_HARDWARE, PH_ICN_CPU, "HARDWARE", 0x34D399);
    build_volume_sidebar(scr, g_hw_sidebar, 0, PH_ICN_VOL);

    // Helper para criar cada um dos 3 blocos de telemetria (390x76, gap 9px -> y=68, 153, 238)
    auto create_hw_card = [&](int16_t y, uint32_t accent_hex, lv_obj_t *&out_accent) -> lv_obj_t * {
        lv_obj_t *card = lv_obj_create(scr);
        lv_obj_remove_style_all(card);
        lv_obj_set_pos(card, 84, y);
        lv_obj_set_size(card, 390, 76);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(HEX_CARD_BORDER), 0);

        out_accent = lv_obj_create(card);
        lv_obj_remove_style_all(out_accent);
        lv_obj_set_size(out_accent, 50, 3);
        lv_obj_set_style_radius(out_accent, 2, 0);
        lv_obj_set_style_bg_opa(out_accent, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(out_accent, lv_color_hex(accent_hex), 0);
        lv_obj_align(out_accent, LV_ALIGN_TOP_MID, 0, 0);
        return card;
    };

    auto create_hw_bar = [&](lv_obj_t *parent, uint32_t bar_hex) -> lv_obj_t * {
        lv_obj_t *bar = lv_bar_create(parent);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_pos(bar, 14, 38);
        lv_obj_set_size(bar, 362, 14);
        lv_obj_set_style_radius(bar, 7, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(HEX_TRACK), LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 7, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar, lv_color_hex(bar_hex), LV_PART_INDICATOR);
        lv_obj_set_style_anim_time(bar, 250, 0);
        return bar;
    };

    // ---- Card 1: PROCESSADOR (CPU) -> x=84, y=68, w=390, h=76 ----
    lv_obj_t *cpu_card = create_hw_card(68, 0x34D399, g_hw_cpu_accent);

    lv_obj_t *cpu_ic = lv_label_create(cpu_card);
    lv_label_set_text(cpu_ic, PH_ICN_CPU);
    lv_obj_set_pos(cpu_ic, 14, 11);
    lv_obj_set_style_text_font(cpu_ic, &phosphor_20, 0);
    lv_obj_set_style_text_color(cpu_ic, lv_color_hex(0x34D399), 0);

    lv_obj_t *cpu_title = lv_label_create(cpu_card);
    lv_label_set_text(cpu_title, "PROCESSADOR (CPU)");
    lv_obj_set_pos(cpu_title, 40, 12);
    lv_obj_set_style_text_font(cpu_title, &inter_bold_16, 0);
    lv_obj_set_style_text_color(cpu_title, lv_color_hex(HEX_TEXT_MAIN), 0);

    g_hw_cpu_temp_lbl = lv_label_create(cpu_card);
    lv_label_set_text(g_hw_cpu_temp_lbl, "");
    lv_obj_set_style_text_font(g_hw_cpu_temp_lbl, &inter_bold_16, 0);
    lv_obj_set_style_text_color(g_hw_cpu_temp_lbl, lv_color_hex(0xCBD5E1), 0);
    lv_obj_align(g_hw_cpu_temp_lbl, LV_ALIGN_TOP_RIGHT, -14, 12);
    lv_obj_add_flag(g_hw_cpu_temp_lbl, LV_OBJ_FLAG_HIDDEN);

    g_hw_cpu_pct_lbl = lv_label_create(cpu_card);
    lv_label_set_text(g_hw_cpu_pct_lbl, "--%");
    lv_obj_set_style_text_font(g_hw_cpu_pct_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_hw_cpu_pct_lbl, lv_color_hex(0x34D399), 0);
    lv_obj_align(g_hw_cpu_pct_lbl, LV_ALIGN_TOP_RIGHT, -14, 8);

    g_hw_cpu_bar = create_hw_bar(cpu_card, 0x34D399);

    g_hw_cpu_state_lbl = lv_label_create(cpu_card);
    lv_label_set_text(g_hw_cpu_state_lbl, "Aguardando telemetria...");
    lv_obj_set_pos(g_hw_cpu_state_lbl, 14, 57);
    lv_obj_set_style_text_font(g_hw_cpu_state_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_hw_cpu_state_lbl, lv_color_hex(HEX_TEXT_DIM), 0);

    g_hw_disk_lbl = lv_label_create(cpu_card);
    lv_label_set_text(g_hw_disk_lbl, "");
    lv_obj_set_style_text_font(g_hw_disk_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_hw_disk_lbl, lv_color_hex(HEX_TEXT_DIM), 0);
    lv_obj_align(g_hw_disk_lbl, LV_ALIGN_BOTTOM_RIGHT, -14, -6);

    // ---- Card 2: PLACA DE VÍDEO (GPU) -> x=84, y=153, w=390, h=76 ----
    lv_obj_t *gpu_card = create_hw_card(153, 0xA78BFA, g_hw_gpu_accent);

    lv_obj_t *gpu_ic = lv_label_create(gpu_card);
    lv_label_set_text(gpu_ic, PH_ICN_GPU);
    lv_obj_set_pos(gpu_ic, 14, 11);
    lv_obj_set_style_text_font(gpu_ic, &phosphor_20, 0);
    lv_obj_set_style_text_color(gpu_ic, lv_color_hex(0xA78BFA), 0);

    lv_obj_t *gpu_title = lv_label_create(gpu_card);
    lv_label_set_text(gpu_title, "PLACA DE VÍDEO (GPU)");
    lv_obj_set_pos(gpu_title, 40, 12);
    lv_obj_set_style_text_font(gpu_title, &inter_bold_16, 0);
    lv_obj_set_style_text_color(gpu_title, lv_color_hex(HEX_TEXT_MAIN), 0);

    g_hw_gpu_temp_lbl = lv_label_create(gpu_card);
    lv_label_set_text(g_hw_gpu_temp_lbl, "");
    lv_obj_set_style_text_font(g_hw_gpu_temp_lbl, &inter_bold_16, 0);
    lv_obj_set_style_text_color(g_hw_gpu_temp_lbl, lv_color_hex(0xCBD5E1), 0);
    lv_obj_align(g_hw_gpu_temp_lbl, LV_ALIGN_TOP_RIGHT, -14, 12);
    lv_obj_add_flag(g_hw_gpu_temp_lbl, LV_OBJ_FLAG_HIDDEN);

    g_hw_gpu_pct_lbl = lv_label_create(gpu_card);
    lv_label_set_text(g_hw_gpu_pct_lbl, "--%");
    lv_obj_set_style_text_font(g_hw_gpu_pct_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_hw_gpu_pct_lbl, lv_color_hex(0xA78BFA), 0);
    lv_obj_align(g_hw_gpu_pct_lbl, LV_ALIGN_TOP_RIGHT, -14, 8);

    g_hw_gpu_bar = create_hw_bar(gpu_card, 0xA78BFA);

    g_hw_gpu_state_lbl = lv_label_create(gpu_card);
    lv_label_set_text(g_hw_gpu_state_lbl, "Aguardando telemetria...");
    lv_obj_set_pos(g_hw_gpu_state_lbl, 14, 57);
    lv_obj_set_style_text_font(g_hw_gpu_state_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_hw_gpu_state_lbl, lv_color_hex(HEX_TEXT_DIM), 0);

    // ---- Card 3: MEMÓRIA RAM -> x=84, y=238, w=390, h=76 ----
    lv_obj_t *ram_card = create_hw_card(238, 0x38BDF8, g_hw_ram_accent);

    lv_obj_t *ram_ic = lv_label_create(ram_card);
    lv_label_set_text(ram_ic, LV_SYMBOL_SD_CARD);
    lv_obj_set_pos(ram_ic, 14, 12);
    lv_obj_set_style_text_font(ram_ic, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(ram_ic, lv_color_hex(0x38BDF8), 0);

    lv_obj_t *ram_title = lv_label_create(ram_card);
    lv_label_set_text(ram_title, "MEMÓRIA RAM");
    lv_obj_set_pos(ram_title, 40, 12);
    lv_obj_set_style_text_font(ram_title, &inter_bold_16, 0);
    lv_obj_set_style_text_color(ram_title, lv_color_hex(HEX_TEXT_MAIN), 0);

    g_hw_ram_pct_lbl = lv_label_create(ram_card);
    lv_label_set_text(g_hw_ram_pct_lbl, "--%");
    lv_obj_set_style_text_font(g_hw_ram_pct_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_hw_ram_pct_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_align(g_hw_ram_pct_lbl, LV_ALIGN_TOP_RIGHT, -14, 8);

    g_hw_ram_bar = create_hw_bar(ram_card, 0x38BDF8);

    g_hw_ram_gb_lbl = lv_label_create(ram_card);
    lv_label_set_text(g_hw_ram_gb_lbl, "-- GB / -- GB");
    lv_obj_set_pos(g_hw_ram_gb_lbl, 14, 55);
    lv_obj_set_style_text_font(g_hw_ram_gb_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_hw_ram_gb_lbl, lv_color_hex(0xCBD5E1), 0);
}

// ============================================================================
// Tela 5: screen_macros (Header 58px + Sidebar Master + Grade 2x3 + CS2 C4)
// ============================================================================
struct MacroCardDef {
    const char *action;
    const char *title;
    const char *icon_utf8;
    uint32_t accent_hex;
    uint32_t press_bg_hex;

    MacroCardDef(const char *a, const char *t, const char *ic, uint32_t acc, uint32_t pbg)
        : action(a), title(t), icon_utf8(ic), accent_hex(acc), press_bg_hex(pbg) {}
};

const MacroCardDef g_macro_cards[5] = {
    MacroCardDef("edge",    "EDGE",    PH_ICN_BROWSER, 0x38BDF8, 0x0A2A43),
    MacroCardDef("steam",   "STEAM",   PH_ICN_GAME,    0x60A5FA, 0x102542),
    MacroCardDef("discord", "DISCORD", PH_ICN_DISCORD, 0x818CF8, 0x1E1B4B),
    MacroCardDef("disney",  "DISNEY+", PH_ICN_FILM,    0x22D3EE, 0x083344),
    MacroCardDef("hbo",     "MAX",     PH_ICN_MPLAY,   0xA855F7, 0x2E1065),
};

void build_screen_macros()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_MACROS] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_MACROS, PH_ICN_APP, "ATALHOS", 0xA78BFA);
    build_volume_sidebar(scr, g_macros_sidebar, 0, PH_ICN_VOL);

    // Grade 2x3: x=84..474 (390px), y=68..314 (246px)
    constexpr int16_t GRID_X = 84;
    constexpr int16_t GRID_Y = 68;
    constexpr int16_t CARD_W = 124;
    constexpr int16_t CARD_H = 118;
    constexpr int16_t GAP_X  = 9;
    constexpr int16_t GAP_Y  = 10;

    // 5 Cards de Aplicativos / Streaming
    for (int i = 0; i < 5; ++i)
    {
        const MacroCardDef &def = g_macro_cards[i];
        const int col = i % 3;
        const int row = i / 3;
        const int16_t x = GRID_X + col * (CARD_W + GAP_X);
        const int16_t y = GRID_Y + row * (CARD_H + GAP_Y);

        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_remove_style_all(btn);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, CARD_W, CARD_H);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(HEX_CARD_BORDER), 0);

        lv_obj_set_style_bg_color(btn, lv_color_hex(def.press_bg_hex), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(def.accent_hex), LV_STATE_PRESSED);

        lv_obj_t *accent = lv_obj_create(btn);
        lv_obj_remove_style_all(accent);
        lv_obj_set_size(accent, 42, 3);
        lv_obj_set_style_radius(accent, 2, 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(accent, lv_color_hex(def.accent_hex), 0);
        lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t *icon_lbl = lv_label_create(btn);
        lv_label_set_text(icon_lbl, def.icon_utf8);
        lv_obj_set_style_text_font(icon_lbl, &phosphor_32, 0);
        lv_obj_set_style_text_color(icon_lbl, lv_color_hex(def.accent_hex), 0);
        lv_obj_align(icon_lbl, LV_ALIGN_CENTER, 0, -14);

        lv_obj_t *title_lbl = lv_label_create(btn);
        lv_label_set_text(title_lbl, def.title);
        lv_obj_set_style_text_font(title_lbl, &inter_bold_16, 0);
        lv_obj_set_style_text_color(title_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
        lv_obj_align(title_lbl, LV_ALIGN_BOTTOM_MID, 0, -16);

        lv_obj_add_event_cb(btn, macro_btn_cb, LV_EVENT_CLICKED, const_cast<char *>(def.action));
    }

    // Card 6 (col=2, row=1): Botão Principal "JOGOS" -> abre screen_games
    const int16_t games_x = GRID_X + 2 * (CARD_W + GAP_X);
    const int16_t games_y = GRID_Y + 1 * (CARD_H + GAP_Y);

    lv_obj_t *games_btn = lv_btn_create(scr);
    lv_obj_remove_style_all(games_btn);
    lv_obj_set_pos(games_btn, games_x, games_y);
    lv_obj_set_size(games_btn, CARD_W, CARD_H);
    lv_obj_clear_flag(games_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(games_btn, 16, 0);
    lv_obj_set_style_bg_opa(games_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(games_btn, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(games_btn, 1, 0);
    lv_obj_set_style_border_color(games_btn, lv_color_hex(HEX_CARD_BORDER), 0);
    lv_obj_set_style_bg_color(games_btn, lv_color_hex(0x064E3B), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(games_btn, 2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(games_btn, lv_color_hex(0x34D399), LV_STATE_PRESSED);
    lv_obj_add_event_cb(games_btn, open_games_btn_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *games_accent = lv_obj_create(games_btn);
    lv_obj_remove_style_all(games_accent);
    lv_obj_set_size(games_accent, 42, 3);
    lv_obj_set_style_radius(games_accent, 2, 0);
    lv_obj_set_style_bg_opa(games_accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(games_accent, lv_color_hex(0x34D399), 0);
    lv_obj_align(games_accent, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *games_ic = lv_label_create(games_btn);
    lv_label_set_text(games_ic, PH_ICN_GAME);
    lv_obj_set_style_text_font(games_ic, &phosphor_32, 0);
    lv_obj_set_style_text_color(games_ic, lv_color_hex(0x34D399), 0);
    lv_obj_align(games_ic, LV_ALIGN_CENTER, 0, -14);

    lv_obj_t *games_lbl = lv_label_create(games_btn);
    lv_label_set_text(games_lbl, "JOGOS");
    lv_obj_set_style_text_font(games_lbl, &inter_bold_16, 0);
    lv_obj_set_style_text_color(games_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_align(games_lbl, LV_ALIGN_BOTTOM_MID, 0, -16);
}

// ============================================================================
// Tela 6: screen_games (Menu de Seleção de Jogos — Grade 2x2 Grande)
// ============================================================================
void build_screen_games()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_GAMES] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_GAMES, PH_ICN_GAME, "SELECIONE O JOGO", 0x34D399);
    build_volume_sidebar(scr, g_games_sidebar, 0, PH_ICN_VOL);

    // Grade 2x2 grande: x=84..474 (390px), y=68..314 (246px) -> cards 190x118, gap 10px
    constexpr int16_t GRID_X = 84;
    constexpr int16_t GRID_Y = 68;
    constexpr int16_t CARD_W = 190;
    constexpr int16_t CARD_H = 118;
    constexpr int16_t GAP_X  = 10;
    constexpr int16_t GAP_Y  = 10;

    for (int i = 0; i < 4; ++i)
    {
        const GameCardDef &game = g_game_cards[i];
        const int col = i % 2;
        const int row = i / 2;
        const int16_t x = GRID_X + col * (CARD_W + GAP_X);
        const int16_t y = GRID_Y + row * (CARD_H + GAP_Y);

        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_remove_style_all(btn);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, CARD_W, CARD_H);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(HEX_CARD_BORDER), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(game.press_bg_hex), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(game.accent_hex), LV_STATE_PRESSED);

        lv_obj_t *accent = lv_obj_create(btn);
        lv_obj_remove_style_all(accent);
        lv_obj_set_size(accent, 52, 3);
        lv_obj_set_style_radius(accent, 2, 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(accent, lv_color_hex(game.accent_hex), 0);
        lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t *ic = lv_label_create(btn);
        lv_label_set_text(ic, game.icon_utf8);
        lv_obj_set_style_text_font(ic, &phosphor_32, 0);
        lv_obj_set_style_text_color(ic, lv_color_hex(game.accent_hex), 0);
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 16);

        lv_obj_t *title = lv_label_create(btn);
        lv_label_set_text(title, game.title);
        lv_obj_set_style_text_font(title, &inter_bold_16, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(HEX_TEXT_MAIN), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 15);

        lv_obj_t *sub = lv_label_create(btn);
        lv_label_set_text(sub, game.subtitle);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(HEX_TEXT_DIM), 0);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -11);

        lv_obj_add_event_cb(
            btn,
            game_card_click_cb,
            LV_EVENT_CLICKED,
            const_cast<GameCardDef *>(&game));
    }
}

// ============================================================================
// Tela 7: screen_game_hud (Painel Unificado em Jogo — Hardware + CS2 C4/Status)
// ============================================================================
void build_screen_game_hud()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_GAME_HUD] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_GAME_HUD, PH_ICN_CROSSHAIR, "CS2", 0xF59E0B);
    build_volume_sidebar(scr, g_hud_sidebar, 3, PH_ICN_GAME, "JOGO");

    // ---- Bloco Superior (y=68..180, h=112): 3 Tiles de Hardware (CPU | GPU | RAM) ----
    // 3 * 124 + 2 * 9 = 390 px (x=84, 217, 350)
    auto create_hud_stat_tile = [&](
        int16_t x,
        const char *icon_txt,
        const lv_font_t *icon_font,
        const char *title_txt,
        uint32_t accent_hex,
        lv_obj_t *&out_accent,
        lv_obj_t *&out_pct,
        lv_obj_t *&out_sub,
        const lv_font_t *sub_font,
        lv_obj_t *&out_bar) {
        lv_obj_t *tile = lv_obj_create(scr);
        lv_obj_remove_style_all(tile);
        lv_obj_set_pos(tile, x, 68);
        lv_obj_set_size(tile, 124, 112);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(tile, 14, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(HEX_CARD_BORDER), 0);

        out_accent = lv_obj_create(tile);
        lv_obj_remove_style_all(out_accent);
        lv_obj_set_size(out_accent, 42, 3);
        lv_obj_set_style_radius(out_accent, 2, 0);
        lv_obj_set_style_bg_opa(out_accent, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(out_accent, lv_color_hex(accent_hex), 0);
        lv_obj_align(out_accent, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t *ic = lv_label_create(tile);
        lv_label_set_text(ic, icon_txt);
        lv_obj_set_pos(ic, 12, 11);
        lv_obj_set_style_text_font(ic, icon_font, 0);
        lv_obj_set_style_text_color(ic, lv_color_hex(accent_hex), 0);

        lv_obj_t *ttl = lv_label_create(tile);
        lv_label_set_text(ttl, title_txt);
        lv_obj_set_pos(ttl, 36, 13);
        lv_obj_set_style_text_font(ttl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ttl, lv_color_hex(HEX_TEXT_DIM), 0);

        out_pct = lv_label_create(tile);
        lv_label_set_text(out_pct, "--%");
        lv_obj_set_style_text_font(out_pct, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(out_pct, lv_color_hex(accent_hex), 0);
        lv_obj_align(out_pct, LV_ALIGN_TOP_MID, 0, 38);

        out_sub = lv_label_create(tile);
        lv_label_set_text(out_sub, "--");
        lv_obj_set_style_text_font(out_sub, sub_font, 0);
        lv_obj_set_style_text_color(out_sub, lv_color_hex(0xCBD5E1), 0);
        lv_obj_align(out_sub, LV_ALIGN_TOP_MID, 0, 66);

        out_bar = lv_bar_create(tile);
        lv_bar_set_range(out_bar, 0, 100);
        lv_bar_set_value(out_bar, 0, LV_ANIM_OFF);
        lv_obj_set_size(out_bar, 100, 8);
        lv_obj_align(out_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_radius(out_bar, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(out_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(out_bar, lv_color_hex(HEX_TRACK), LV_PART_MAIN);
        lv_obj_set_style_radius(out_bar, 4, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(out_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(out_bar, lv_color_hex(accent_hex), LV_PART_INDICATOR);
        lv_obj_set_style_anim_time(out_bar, 250, 0);
    };

    create_hud_stat_tile(
        84, PH_ICN_CPU, &phosphor_20, "CPU", 0x34D399,
        g_hud_cpu_accent, g_hud_cpu_pct_lbl, g_hud_cpu_temp_lbl, &inter_bold_16, g_hud_cpu_bar);

    create_hud_stat_tile(
        217, PH_ICN_GPU, &phosphor_20, "GPU", 0xA78BFA,
        g_hud_gpu_accent, g_hud_gpu_pct_lbl, g_hud_gpu_temp_lbl, &inter_bold_16, g_hud_gpu_bar);

    create_hud_stat_tile(
        350, LV_SYMBOL_SD_CARD, &lv_font_montserrat_18, "RAM", 0x38BDF8,
        g_hud_ram_accent, g_hud_ram_pct_lbl, g_hud_ram_gb_lbl, &lv_font_montserrat_12, g_hud_ram_bar);

    // ---- Bloco Inferior A (Modo CS2): Widget Ativo da C4 (x=84, y=190, w=390, h=124) ----
    g_hud_cs2_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(g_hud_cs2_panel);
    lv_obj_set_pos(g_hud_cs2_panel, 84, 190);
    lv_obj_set_size(g_hud_cs2_panel, 390, 124);
    lv_obj_clear_flag(g_hud_cs2_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(g_hud_cs2_panel, 16, 0);
    lv_obj_set_style_bg_opa(g_hud_cs2_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_hud_cs2_panel, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(g_hud_cs2_panel, 1, 0);
    lv_obj_set_style_border_color(g_hud_cs2_panel, lv_color_hex(HEX_CARD_BORDER), 0);

    g_hud_c4_accent = lv_obj_create(g_hud_cs2_panel);
    lv_obj_remove_style_all(g_hud_c4_accent);
    lv_obj_set_size(g_hud_c4_accent, 64, 3);
    lv_obj_set_style_radius(g_hud_c4_accent, 2, 0);
    lv_obj_set_style_bg_opa(g_hud_c4_accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_hud_c4_accent, lv_color_hex(HEX_WARN), 0);
    lv_obj_align(g_hud_c4_accent, LV_ALIGN_TOP_MID, 0, 0);

    g_hud_c4_icon_box = lv_obj_create(g_hud_cs2_panel);
    lv_obj_remove_style_all(g_hud_c4_icon_box);
    lv_obj_set_pos(g_hud_c4_icon_box, 16, 25);
    lv_obj_set_size(g_hud_c4_icon_box, 74, 74);
    lv_obj_set_style_radius(g_hud_c4_icon_box, 18, 0);
    lv_obj_set_style_bg_opa(g_hud_c4_icon_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_hud_c4_icon_box, lv_color_hex(0x2A2012), 0);
    lv_obj_set_style_border_width(g_hud_c4_icon_box, 2, 0);
    lv_obj_set_style_border_color(g_hud_c4_icon_box, lv_color_hex(HEX_WARN), 0);

    g_hud_c4_icon_lbl = lv_label_create(g_hud_c4_icon_box);
    lv_label_set_text(g_hud_c4_icon_lbl, PH_ICN_CROSSHAIR);
    lv_obj_set_style_text_font(g_hud_c4_icon_lbl, &phosphor_42, 0);
    lv_obj_set_style_text_color(g_hud_c4_icon_lbl, lv_color_hex(HEX_WARN), 0);
    lv_obj_center(g_hud_c4_icon_lbl);

    g_hud_c4_title_lbl = lv_label_create(g_hud_cs2_panel);
    lv_label_set_text(g_hud_c4_title_lbl, "CS2 C4 TIMER (GSI)");
    lv_obj_set_pos(g_hud_c4_title_lbl, 106, 18);
    lv_obj_set_style_text_font(g_hud_c4_title_lbl, &inter_bold_16, 0);
    lv_obj_set_style_text_color(g_hud_c4_title_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);

    g_hud_c4_time_lbl = lv_label_create(g_hud_cs2_panel);
    lv_label_set_text(g_hud_c4_time_lbl, "--s");
    lv_obj_set_style_text_font(g_hud_c4_time_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_hud_c4_time_lbl, lv_color_hex(HEX_WARN), 0);
    lv_obj_align(g_hud_c4_time_lbl, LV_ALIGN_TOP_RIGHT, -18, 14);

    g_hud_c4_sub_lbl = lv_label_create(g_hud_cs2_panel);
    lv_label_set_text(g_hud_c4_sub_lbl, "Aguardando plant na partida...");
    lv_obj_set_pos(g_hud_c4_sub_lbl, 106, 50);
    lv_obj_set_style_text_font(g_hud_c4_sub_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_hud_c4_sub_lbl, lv_color_hex(HEX_TEXT_DIM), 0);

    g_hud_c4_bar = lv_bar_create(g_hud_cs2_panel);
    lv_bar_set_range(g_hud_c4_bar, 0, 40);
    lv_bar_set_value(g_hud_c4_bar, 0, LV_ANIM_OFF);
    lv_obj_set_pos(g_hud_c4_bar, 106, 82);
    lv_obj_set_size(g_hud_c4_bar, 266, 18);
    lv_obj_set_style_radius(g_hud_c4_bar, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_hud_c4_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_hud_c4_bar, lv_color_hex(HEX_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(g_hud_c4_bar, 9, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_hud_c4_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_hud_c4_bar, lv_color_hex(HEX_WARN), LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(g_hud_c4_bar, 200, 0);

    // ---- Bloco Inferior B (Modo Genérico): Sessão Ativa + RAM & Disco C: (x=84, y=190, w=390, h=124) ----
    g_hud_gen_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(g_hud_gen_panel);
    lv_obj_set_pos(g_hud_gen_panel, 84, 190);
    lv_obj_set_size(g_hud_gen_panel, 390, 124);
    lv_obj_clear_flag(g_hud_gen_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(g_hud_gen_panel, 16, 0);
    lv_obj_set_style_bg_opa(g_hud_gen_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_hud_gen_panel, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(g_hud_gen_panel, 1, 0);
    lv_obj_set_style_border_color(g_hud_gen_panel, lv_color_hex(HEX_CARD_BORDER), 0);
    lv_obj_add_flag(g_hud_gen_panel, LV_OBJ_FLAG_HIDDEN);

    g_hud_gen_accent = lv_obj_create(g_hud_gen_panel);
    lv_obj_remove_style_all(g_hud_gen_accent);
    lv_obj_set_size(g_hud_gen_accent, 64, 3);
    lv_obj_set_style_radius(g_hud_gen_accent, 2, 0);
    lv_obj_set_style_bg_opa(g_hud_gen_accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_hud_gen_accent, lv_color_hex(0x38BDF8), 0);
    lv_obj_align(g_hud_gen_accent, LV_ALIGN_TOP_MID, 0, 0);

    g_hud_gen_icon_box = lv_obj_create(g_hud_gen_panel);
    lv_obj_remove_style_all(g_hud_gen_icon_box);
    lv_obj_set_pos(g_hud_gen_icon_box, 16, 28);
    lv_obj_set_size(g_hud_gen_icon_box, 68, 68);
    lv_obj_set_style_radius(g_hud_gen_icon_box, 18, 0);
    lv_obj_set_style_bg_opa(g_hud_gen_icon_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_hud_gen_icon_box, lv_color_hex(0x0A2A43), 0);
    lv_obj_set_style_border_width(g_hud_gen_icon_box, 2, 0);
    lv_obj_set_style_border_color(g_hud_gen_icon_box, lv_color_hex(0x38BDF8), 0);

    g_hud_gen_icon_lbl = lv_label_create(g_hud_gen_icon_box);
    lv_label_set_text(g_hud_gen_icon_lbl, PH_ICN_GAME);
    lv_obj_set_style_text_font(g_hud_gen_icon_lbl, &phosphor_32, 0);
    lv_obj_set_style_text_color(g_hud_gen_icon_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(g_hud_gen_icon_lbl);

    g_hud_gen_title = lv_label_create(g_hud_gen_panel);
    lv_label_set_text(g_hud_gen_title, "SESSÃO ATIVA");
    lv_obj_set_pos(g_hud_gen_title, 100, 18);
    lv_obj_set_style_text_font(g_hud_gen_title, &inter_bold_16, 0);
    lv_obj_set_style_text_color(g_hud_gen_title, lv_color_hex(HEX_TEXT_MAIN), 0);

    g_hud_gen_ram_lbl = lv_label_create(g_hud_gen_panel);
    lv_label_set_text(g_hud_gen_ram_lbl, "RAM: -- GB / -- GB");
    lv_obj_set_pos(g_hud_gen_ram_lbl, 100, 48);
    lv_obj_set_style_text_font(g_hud_gen_ram_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_hud_gen_ram_lbl, lv_color_hex(0xCBD5E1), 0);

    g_hud_gen_disk_lbl = lv_label_create(g_hud_gen_panel);
    lv_label_set_text(g_hud_gen_disk_lbl, "Disco C: --%");
    lv_obj_set_pos(g_hud_gen_disk_lbl, 100, 74);
    lv_obj_set_style_text_font(g_hud_gen_disk_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_hud_gen_disk_lbl, lv_color_hex(HEX_TEXT_DIM), 0);

    g_hud_gen_disk_bar = lv_bar_create(g_hud_gen_panel);
    lv_bar_set_range(g_hud_gen_disk_bar, 0, 100);
    lv_bar_set_value(g_hud_gen_disk_bar, 0, LV_ANIM_OFF);
    lv_obj_set_pos(g_hud_gen_disk_bar, 100, 94);
    lv_obj_set_size(g_hud_gen_disk_bar, 272, 12);
    lv_obj_set_style_radius(g_hud_gen_disk_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_hud_gen_disk_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_hud_gen_disk_bar, lv_color_hex(HEX_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(g_hud_gen_disk_bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_hud_gen_disk_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_hud_gen_disk_bar, lv_color_hex(0x34D399), LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(g_hud_gen_disk_bar, 250, 0);
}

// ============================================================================
// Helper: Botão Voltar para screen_settings (usado nas subtelas dedicadas)
// ============================================================================
lv_obj_t *build_back_to_settings_btn(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_ext_click_area(btn, 6);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(HEX_BTN_IDLE), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0A2A43), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(HEX_CARD_BORDER), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x38BDF8), LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        btn,
        settings_nav_cb,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(static_cast<intptr_t>(UI_SCREEN_SETTINGS)));

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Voltar");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_center(lbl);
    return btn;
}

// ============================================================================
// Tela 8: screen_settings (Hub de Configurações — Grade 2x2 Espaçada)
// ============================================================================
void build_screen_settings()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_SETTINGS] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_SETTINGS, PH_ICN_GEAR, "CONFIGURAÇÕES", 0x38BDF8);
    build_volume_sidebar(scr, g_settings_sidebar, 0, PH_ICN_VOL, "GERAL");

    // Grade 2x2 ampla: x=84..474 (390px), y=68..314 (246px) -> cards 190x118, gap 10px
    constexpr int16_t GRID_X = 84;
    constexpr int16_t GRID_Y = 68;
    constexpr int16_t CARD_W = 190;
    constexpr int16_t CARD_H = 118;
    constexpr int16_t GAP_X  = 10;
    constexpr int16_t GAP_Y  = 10;

    auto create_settings_card = [&](
        int col,
        int row,
        uint32_t accent_hex,
        uint32_t press_bg_hex,
        const char *icon_utf8,
        const char *title_txt,
        const char *init_sub_txt,
        lv_obj_t *&out_accent,
        lv_obj_t *&out_icon,
        lv_obj_t *&out_sub) -> lv_obj_t * {
        const int16_t x = GRID_X + col * (CARD_W + GAP_X);
        const int16_t y = GRID_Y + row * (CARD_H + GAP_Y);

        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_remove_style_all(btn);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, CARD_W, CARD_H);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(HEX_CARD_BORDER), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(press_bg_hex), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(accent_hex), LV_STATE_PRESSED);

        out_accent = lv_obj_create(btn);
        lv_obj_remove_style_all(out_accent);
        lv_obj_set_size(out_accent, 52, 3);
        lv_obj_set_style_radius(out_accent, 2, 0);
        lv_obj_set_style_bg_opa(out_accent, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(out_accent, lv_color_hex(accent_hex), 0);
        lv_obj_align(out_accent, LV_ALIGN_TOP_MID, 0, 0);

        out_icon = lv_label_create(btn);
        lv_label_set_text(out_icon, icon_utf8);
        lv_obj_set_style_text_font(out_icon, &phosphor_42, 0);
        lv_obj_set_style_text_color(out_icon, lv_color_hex(accent_hex), 0);
        lv_obj_align(out_icon, LV_ALIGN_TOP_MID, 0, 12);

        lv_obj_t *title = lv_label_create(btn);
        lv_label_set_text(title, title_txt);
        lv_obj_set_style_text_font(title, &inter_bold_16, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(HEX_TEXT_MAIN), 0);
        lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -28);

        out_sub = lv_label_create(btn);
        lv_label_set_text(out_sub, init_sub_txt);
        lv_obj_set_width(out_sub, CARD_W - 18);
        lv_label_set_long_mode(out_sub, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(out_sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(out_sub, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(out_sub, lv_color_hex(HEX_TEXT_DIM), 0);
        lv_obj_align(out_sub, LV_ALIGN_BOTTOM_MID, 0, -9);

        return btn;
    };

    // Card 1 (0,0): WI-FI -> abre screen_wifi_manager
    lv_obj_t *wifi_acc = nullptr;
    lv_obj_t *wifi_ic = nullptr;
    lv_obj_t *wifi_btn = create_settings_card(
        0, 0, 0x38BDF8, 0x0A2A43, PH_ICN_WIFI, "WI-FI", "Desconectado",
        wifi_acc, wifi_ic, g_set_wifi_sub);
    lv_obj_add_event_cb(
        wifi_btn,
        settings_nav_cb,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(static_cast<intptr_t>(UI_SCREEN_WIFI)));

    // Card 2 (1,0): BLUETOOTH PC -> toggle direto
    g_set_bt_card = create_settings_card(
        1, 0, 0x38BDF8, 0x0B2545, PH_ICN_BT_OFF, "BLUETOOTH PC", "DESLIGADO",
        g_set_bt_accent, g_set_bt_icon, g_set_bt_sub);
    lv_obj_set_style_border_width(g_set_bt_card, 2, 0);
    lv_obj_add_event_cb(g_set_bt_card, pc_bt_toggle_btn_cb, LV_EVENT_CLICKED, nullptr);

    g_set_bt_pill = lv_obj_create(g_set_bt_card);
    lv_obj_remove_style_all(g_set_bt_pill);
    lv_obj_set_size(g_set_bt_pill, 40, 20);
    lv_obj_align(g_set_bt_pill, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_clear_flag(g_set_bt_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(g_set_bt_pill, 10, 0);
    lv_obj_set_style_bg_opa(g_set_bt_pill, LV_OPA_COVER, 0);

    g_set_bt_pill_lbl = lv_label_create(g_set_bt_pill);
    lv_obj_set_style_text_font(g_set_bt_pill_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(g_set_bt_pill_lbl);

    // Card 3 (0,1): TELA & BRILHO -> abre screen_brightness
    lv_obj_t *br_acc = nullptr;
    lv_obj_t *br_ic = nullptr;
    lv_obj_t *bright_btn = create_settings_card(
        0, 1, 0xF59E0B, 0x3B2308, PH_ICN_SMART, "TELA & BRILHO", "Brilho: 80%",
        br_acc, br_ic, g_set_bright_sub);
    lv_obj_add_event_cb(
        bright_btn,
        settings_nav_cb,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(static_cast<intptr_t>(UI_SCREEN_BRIGHTNESS)));

    // Card 4 (1,1): SISTEMA / INFO -> abre screen_system
    lv_obj_t *sys_acc = nullptr;
    lv_obj_t *sys_ic = nullptr;
    lv_obj_t *sys_btn = create_settings_card(
        1, 1, 0x34D399, 0x064E3B, PH_ICN_INFO, "SISTEMA", "v2.2  •  Info & Reset",
        sys_acc, sys_ic, g_set_sys_sub);
    lv_obj_add_event_cb(
        sys_btn,
        settings_nav_cb,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(static_cast<intptr_t>(UI_SCREEN_SYSTEM)));
}

// ============================================================================
// Tela 9: screen_wifi_manager (Tela Dedicada Wi-Fi + Scanner Amplo + Teclado)
// ============================================================================
void build_screen_wifi_manager()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_WIFI] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_WIFI, PH_ICN_WIFI, "GERENCIAR WI-FI", 0x38BDF8);
    build_volume_sidebar(scr, g_wifi_sidebar, 0, PH_ICN_VOL, "GERAL");

    // ---- Card Superior: Voltar + Status Atual (SSID, IP, RSSI) -> x=84, y=68, w=390, h=60 ----
    lv_obj_t *wifi_card = lv_obj_create(scr);
    lv_obj_remove_style_all(wifi_card);
    lv_obj_set_pos(wifi_card, 84, 68);
    lv_obj_set_size(wifi_card, 390, 60);
    lv_obj_clear_flag(wifi_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(wifi_card, 14, 0);
    lv_obj_set_style_bg_opa(wifi_card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(wifi_card, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(wifi_card, 1, 0);
    lv_obj_set_style_border_color(wifi_card, lv_color_hex(HEX_CARD_BORDER), 0);

    lv_obj_t *wifi_accent = lv_obj_create(wifi_card);
    lv_obj_remove_style_all(wifi_accent);
    lv_obj_set_size(wifi_accent, 48, 3);
    lv_obj_set_style_radius(wifi_accent, 2, 0);
    lv_obj_set_style_bg_opa(wifi_accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(wifi_accent, lv_color_hex(0x38BDF8), 0);
    lv_obj_align(wifi_accent, LV_ALIGN_TOP_MID, 0, 0);

    build_back_to_settings_btn(wifi_card, 8, 10, 74, 40);

    g_net_wifi_icon = lv_label_create(wifi_card);
    lv_obj_set_pos(g_net_wifi_icon, 90, 9);
    lv_obj_set_style_text_font(g_net_wifi_icon, &phosphor_20, 0);

    g_net_ssid_lbl = lv_label_create(wifi_card);
    lv_obj_set_pos(g_net_ssid_lbl, 116, 10);
    lv_obj_set_width(g_net_ssid_lbl, 190);
    lv_label_set_long_mode(g_net_ssid_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_net_ssid_lbl, &inter_bold_16, 0);

    g_net_rssi_lbl = lv_label_create(wifi_card);
    lv_obj_set_style_text_font(g_net_rssi_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(g_net_rssi_lbl, LV_ALIGN_TOP_RIGHT, -10, 12);

    g_net_ip_lbl = lv_label_create(wifi_card);
    lv_obj_set_pos(g_net_ip_lbl, 90, 36);
    lv_obj_set_width(g_net_ip_lbl, 290);
    lv_label_set_long_mode(g_net_ip_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_net_ip_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_net_ip_lbl, lv_color_hex(HEX_TEXT_DIM), 0);

    // ---- Card Principal Amplo: Scanner e Lista Confortável de Redes -> x=84, y=136, w=390, h=178 ----
    lv_obj_t *scan_card = lv_obj_create(scr);
    lv_obj_remove_style_all(scan_card);
    lv_obj_set_pos(scan_card, 84, 136);
    lv_obj_set_size(scan_card, 390, 178);
    lv_obj_clear_flag(scan_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(scan_card, 14, 0);
    lv_obj_set_style_bg_opa(scan_card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scan_card, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(scan_card, 1, 0);
    lv_obj_set_style_border_color(scan_card, lv_color_hex(HEX_CARD_BORDER), 0);

    g_net_scan_status_lbl = lv_label_create(scan_card);
    lv_label_set_text(g_net_scan_status_lbl, "Toque em Escanear Redes");
    lv_obj_set_pos(g_net_scan_status_lbl, 12, 14);
    lv_obj_set_width(g_net_scan_status_lbl, 218);
    lv_label_set_long_mode(g_net_scan_status_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(g_net_scan_status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_net_scan_status_lbl, lv_color_hex(HEX_TEXT_DIM), 0);

    g_net_scan_btn = lv_btn_create(scan_card);
    lv_obj_remove_style_all(g_net_scan_btn);
    lv_obj_set_size(g_net_scan_btn, 144, 32);
    lv_obj_align(g_net_scan_btn, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_set_style_radius(g_net_scan_btn, 9, 0);
    lv_obj_set_style_bg_opa(g_net_scan_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_net_scan_btn, lv_color_hex(0x0A2A43), 0);
    lv_obj_set_style_bg_color(g_net_scan_btn, lv_color_hex(0x0284C7), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(g_net_scan_btn, 1, 0);
    lv_obj_set_style_border_color(g_net_scan_btn, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_layout(g_net_scan_btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_net_scan_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_net_scan_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_net_scan_btn, 5, 0);
    lv_obj_add_event_cb(g_net_scan_btn, wifi_scan_btn_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *scan_ic = lv_label_create(g_net_scan_btn);
    lv_label_set_text(scan_ic, PH_ICN_SEARCH);
    lv_obj_set_style_text_font(scan_ic, &phosphor_20, 0);
    lv_obj_set_style_text_color(scan_ic, lv_color_hex(0x38BDF8), 0);

    lv_obj_t *scan_txt = lv_label_create(g_net_scan_btn);
    lv_label_set_text(scan_txt, "Escanear Redes");
    lv_obj_set_style_text_font(scan_txt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(scan_txt, lv_color_hex(HEX_TEXT_MAIN), 0);

    g_net_wifi_list = lv_list_create(scan_card);
    lv_obj_set_pos(g_net_wifi_list, 8, 44);
    lv_obj_set_size(g_net_wifi_list, 374, 126);
    lv_obj_set_style_radius(g_net_wifi_list, 10, 0);
    lv_obj_set_style_bg_opa(g_net_wifi_list, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_net_wifi_list, lv_color_hex(HEX_BG), 0);
    lv_obj_set_style_border_width(g_net_wifi_list, 1, 0);
    lv_obj_set_style_border_color(g_net_wifi_list, lv_color_hex(HEX_TRACK), 0);
    lv_obj_set_style_pad_all(g_net_wifi_list, 5, 0);
    lv_obj_set_style_pad_row(g_net_wifi_list, 5, 0);

    // ---- Modal Fullscreen Dedicado (480x320): Cabeçalho (76px) + Teclado (480x240) ----
    g_wifi_modal = lv_obj_create(scr);
    lv_obj_remove_style_all(g_wifi_modal);
    lv_obj_set_pos(g_wifi_modal, 0, 0);
    lv_obj_set_size(g_wifi_modal, 480, 320);
    lv_obj_clear_flag(g_wifi_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(g_wifi_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_wifi_modal, lv_color_hex(0x050811), 0);
    lv_obj_add_flag(g_wifi_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *modal_header = lv_obj_create(g_wifi_modal);
    lv_obj_remove_style_all(modal_header);
    lv_obj_set_pos(modal_header, 0, 0);
    lv_obj_set_size(modal_header, 480, 76);
    lv_obj_clear_flag(modal_header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(modal_header, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(modal_header, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_side(modal_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(modal_header, 2, 0);
    lv_obj_set_style_border_color(modal_header, lv_color_hex(0x38BDF8), 0);

    g_wifi_modal_title = lv_label_create(modal_header);
    lv_label_set_text(g_wifi_modal_title, "Conectar a: Wi-Fi");
    lv_obj_set_pos(g_wifi_modal_title, 12, 6);
    lv_obj_set_width(g_wifi_modal_title, 412);
    lv_label_set_long_mode(g_wifi_modal_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_wifi_modal_title, &inter_bold_16, 0);
    lv_obj_set_style_text_color(g_wifi_modal_title, lv_color_hex(HEX_TEXT_MAIN), 0);

    lv_obj_t *close_btn = lv_btn_create(modal_header);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 38, 26);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -10, 3);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x450A0A), 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x7F1D1D), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_border_color(close_btn, lv_color_hex(0xEF4444), 0);
    lv_obj_add_event_cb(close_btn, wifi_modal_close_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *close_ic = lv_label_create(close_btn);
    lv_label_set_text(close_ic, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(close_ic, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(close_ic, lv_color_hex(0xFCA5A5), 0);
    lv_obj_center(close_ic);

    g_wifi_pwd_ta = lv_textarea_create(modal_header);
    lv_obj_set_pos(g_wifi_pwd_ta, 10, 31);
    lv_obj_set_size(g_wifi_pwd_ta, 412, 40);
    lv_textarea_set_one_line(g_wifi_pwd_ta, true);
    lv_textarea_set_password_mode(g_wifi_pwd_ta, true);
    lv_textarea_set_placeholder_text(g_wifi_pwd_ta, "Digite a senha do Wi-Fi...");
    lv_obj_set_style_bg_color(g_wifi_pwd_ta, lv_color_hex(HEX_BG), 0);
    lv_obj_set_style_text_color(g_wifi_pwd_ta, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_set_style_border_color(g_wifi_pwd_ta, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_border_width(g_wifi_pwd_ta, 1, 0);
    lv_obj_set_style_radius(g_wifi_pwd_ta, 8, 0);
    lv_obj_set_style_pad_ver(g_wifi_pwd_ta, 8, 0);
    lv_obj_set_style_pad_hor(g_wifi_pwd_ta, 10, 0);
    lv_obj_set_style_text_font(g_wifi_pwd_ta, &lv_font_montserrat_16, 0);

    lv_obj_t *eye_btn = lv_btn_create(modal_header);
    lv_obj_remove_style_all(eye_btn);
    lv_obj_set_pos(eye_btn, 428, 31);
    lv_obj_set_size(eye_btn, 42, 40);
    lv_obj_set_style_radius(eye_btn, 8, 0);
    lv_obj_set_style_bg_opa(eye_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(eye_btn, lv_color_hex(HEX_BTN_IDLE), 0);
    lv_obj_set_style_border_width(eye_btn, 1, 0);
    lv_obj_set_style_border_color(eye_btn, lv_color_hex(HEX_CARD_BORDER), 0);
    lv_obj_add_event_cb(eye_btn, wifi_pwd_eye_cb, LV_EVENT_CLICKED, nullptr);

    g_wifi_pwd_eye_lbl = lv_label_create(eye_btn);
    lv_label_set_text(g_wifi_pwd_eye_lbl, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(g_wifi_pwd_eye_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_wifi_pwd_eye_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_center(g_wifi_pwd_eye_lbl);

    // Teclado Virtual LVGL 480x240 ancorado exatamente na base (y=80..320 -> todas as 4 linhas completas)
    g_wifi_kb = lv_keyboard_create(g_wifi_modal);
    lv_obj_set_size(g_wifi_kb, 480, 240);
    lv_obj_align(g_wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(g_wifi_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(g_wifi_kb, g_wifi_pwd_ta);
    lv_obj_set_style_bg_opa(g_wifi_kb, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_wifi_kb, lv_color_hex(HEX_BG), 0);
    lv_obj_set_style_pad_all(g_wifi_kb, 4, 0);
    lv_obj_set_style_pad_row(g_wifi_kb, 4, 0);
    lv_obj_set_style_pad_column(g_wifi_kb, 4, 0);
    lv_obj_set_style_bg_opa(g_wifi_kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(g_wifi_kb, lv_color_hex(HEX_CARD), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(g_wifi_kb, lv_color_hex(0x0284C7), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(g_wifi_kb, lv_color_hex(0x1E3A8A), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(g_wifi_kb, lv_color_hex(HEX_TEXT_MAIN), LV_PART_ITEMS);
    lv_obj_set_style_text_font(g_wifi_kb, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_border_color(g_wifi_kb, lv_color_hex(HEX_CARD_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_border_width(g_wifi_kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(g_wifi_kb, 8, LV_PART_ITEMS);
    lv_obj_add_event_cb(g_wifi_kb, wifi_keyboard_event_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(g_wifi_kb, wifi_keyboard_event_cb, LV_EVENT_CANCEL, nullptr);
}

// ============================================================================
// Tela 10: screen_brightness (Tela Dedicada para Brilho do Display via PWM)
// ============================================================================
void build_screen_brightness()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_BRIGHTNESS] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_BRIGHTNESS, PH_ICN_SMART, "TELA & BRILHO", 0xF59E0B);
    build_volume_sidebar(scr, g_bright_sidebar, 0, PH_ICN_VOL, "GERAL");

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 84, 68);
    lv_obj_set_size(card, 390, 246);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(HEX_CARD_BORDER), 0);

    lv_obj_t *accent = lv_obj_create(card);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 64, 3);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0xF59E0B), 0);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);

    build_back_to_settings_btn(card, 12, 12, 82, 34);

    // Área Hero Central: Ícone grande de brilho + Porcentagem destacada
    g_bright_icon_box = lv_obj_create(card);
    lv_obj_remove_style_all(g_bright_icon_box);
    lv_obj_set_pos(g_bright_icon_box, 108, 48);
    lv_obj_set_size(g_bright_icon_box, 76, 76);
    lv_obj_set_style_radius(g_bright_icon_box, 38, 0);
    lv_obj_set_style_bg_opa(g_bright_icon_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_bright_icon_box, lv_color_hex(0x2D1E07), 0);
    lv_obj_set_style_border_width(g_bright_icon_box, 2, 0);
    lv_obj_set_style_border_color(g_bright_icon_box, lv_color_hex(0xF59E0B), 0);

    lv_obj_t *hero_ic = lv_label_create(g_bright_icon_box);
    lv_label_set_text(hero_ic, PH_ICN_SMART);
    lv_obj_set_style_text_font(hero_ic, &phosphor_42, 0);
    lv_obj_set_style_text_color(hero_ic, lv_color_hex(0xFBBF24), 0);
    lv_obj_center(hero_ic);

    g_bright_pct_lbl = lv_label_create(card);
    lv_label_set_text(g_bright_pct_lbl, "80%");
    lv_obj_set_pos(g_bright_pct_lbl, 200, 64);
    lv_obj_set_style_text_font(g_bright_pct_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_bright_pct_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);

    lv_obj_t *hero_sub = lv_label_create(card);
    lv_label_set_text(hero_sub, "BRILHO DO DISPLAY");
    lv_obj_set_pos(hero_sub, 200, 96);
    lv_obj_set_style_text_font(hero_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hero_sub, lv_color_hex(HEX_TEXT_DIM), 0);

    // Slider Horizontal Largo e Macio (10% a 100%, salva em NVS ao soltar)
    const uint8_t init_bright = get_screen_brightness();
    g_bright_slider = lv_slider_create(card);
    lv_slider_set_range(g_bright_slider, 10, 100);
    lv_slider_set_value(g_bright_slider, init_bright, LV_ANIM_OFF);
    lv_obj_set_pos(g_bright_slider, 24, 152);
    lv_obj_set_size(g_bright_slider, 342, 18);
    lv_obj_set_ext_click_area(g_bright_slider, 18);
    lv_obj_set_style_radius(g_bright_slider, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_bright_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bright_slider, lv_color_hex(HEX_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(g_bright_slider, 9, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_bright_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_bright_slider, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(g_bright_slider, lv_color_hex(0xFDE047), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(g_bright_slider, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_bright_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(g_bright_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_border_width(g_bright_slider, 3, LV_PART_KNOB);
    lv_obj_set_style_border_color(g_bright_slider, lv_color_hex(0xF59E0B), LV_PART_KNOB);
    lv_obj_set_style_pad_all(g_bright_slider, 7, LV_PART_KNOB);
    lv_obj_add_event_cb(g_bright_slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(g_bright_slider, brightness_slider_cb, LV_EVENT_RELEASED, nullptr);

    // 3 Botões Rápidos de Brilho na base: [ 25% ] [ 60% ] [ 100% ]
    static constexpr int PRESETS[3] = {25, 60, 100};
    static const char *PRESET_LABELS[3] = {"25% Noite", "60% Médio", "100% Máx"};
    for (int i = 0; i < 3; ++i)
    {
        lv_obj_t *pbtn = lv_btn_create(card);
        lv_obj_remove_style_all(pbtn);
        lv_obj_set_pos(pbtn, 18 + i * 120, 196);
        lv_obj_set_size(pbtn, 112, 36);
        lv_obj_set_style_radius(pbtn, 10, 0);
        lv_obj_set_style_bg_opa(pbtn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pbtn, lv_color_hex(HEX_BTN_IDLE), 0);
        lv_obj_set_style_bg_color(pbtn, lv_color_hex(0x3B2308), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(pbtn, 1, 0);
        lv_obj_set_style_border_color(pbtn, lv_color_hex(HEX_CARD_BORDER), 0);
        lv_obj_set_style_border_color(pbtn, lv_color_hex(0xF59E0B), LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            pbtn,
            brightness_preset_cb,
            LV_EVENT_CLICKED,
            reinterpret_cast<void *>(static_cast<intptr_t>(PRESETS[i])));

        lv_obj_t *plbl = lv_label_create(pbtn);
        lv_label_set_text(plbl, PRESET_LABELS[i]);
        lv_obj_set_style_text_font(plbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(plbl, lv_color_hex(HEX_TEXT_MAIN), 0);
        lv_obj_center(plbl);
    }

    update_brightness_visuals(init_bright, true);
}

// ============================================================================
// Tela 11: screen_system_info (Central Completa do Sistema — Lista Rolável)
// ============================================================================
void build_screen_system()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_SYSTEM] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_universal_header(scr, UI_SCREEN_SYSTEM, PH_ICN_INFO, "SISTEMA / INFO", 0x34D399);
    build_volume_sidebar(scr, g_sys_sidebar, 0, PH_ICN_VOL, "GERAL");

    // Barra superior fixa com botão Voltar + Versão (x=84, y=68, w=390, h=42)
    lv_obj_t *top_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_pos(top_bar, 84, 68);
    lv_obj_set_size(top_bar, 390, 42);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(top_bar, 12, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_border_color(top_bar, lv_color_hex(HEX_CARD_BORDER), 0);

    build_back_to_settings_btn(top_bar, 6, 5, 78, 32);

    lv_obj_t *app_title = lv_label_create(top_bar);
    lv_label_set_text(app_title, "MINI DECK IoT");
    lv_obj_set_pos(app_title, 96, 11);
    lv_obj_set_style_text_font(app_title, &inter_bold_16, 0);
    lv_obj_set_style_text_color(app_title, lv_color_hex(HEX_TEXT_MAIN), 0);

    lv_obj_t *ver_pill = lv_obj_create(top_bar);
    lv_obj_remove_style_all(ver_pill);
    lv_obj_set_size(ver_pill, 58, 24);
    lv_obj_align(ver_pill, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_radius(ver_pill, 12, 0);
    lv_obj_set_style_bg_opa(ver_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ver_pill, lv_color_hex(0x064E3B), 0);
    lv_obj_set_style_border_width(ver_pill, 1, 0);
    lv_obj_set_style_border_color(ver_pill, lv_color_hex(0x34D399), 0);

    lv_obj_t *ver_lbl = lv_label_create(ver_pill);
    lv_label_set_text(ver_lbl, "v2.2");
    lv_obj_set_style_text_font(ver_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0x34D399), 0);
    lv_obj_center(ver_lbl);

    // Container vertical rolável com cards espaçados (x=84, y=116, w=390, h=198)
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 84, 116);
    lv_obj_set_size(list, 390, 198);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_style_pad_bottom(list, 6, 0);

    auto create_sys_row = [&](int16_t h) -> lv_obj_t * {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 386, h);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(HEX_CARD_BORDER), 0);
        return row;
    };

    auto add_info_card = [&](const char *title_txt, const char *init_val) -> lv_obj_t * {
        lv_obj_t *row = create_sys_row(48);

        lv_obj_t *t = lv_label_create(row);
        lv_label_set_text(t, title_txt);
        lv_obj_set_pos(t, 12, 6);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(HEX_TEXT_DIM), 0);

        lv_obj_t *v = lv_label_create(row);
        lv_label_set_text(v, init_val);
        lv_obj_set_pos(v, 12, 24);
        lv_obj_set_width(v, 362);
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(v, lv_color_hex(HEX_TEXT_MAIN), 0);
        return v;
    };

    // 1. Hardware & Firmware
    add_info_card("Hardware & Firmware", "JC3248W535C (ESP32-S3)  |  Firmware v2.2");

    // 2. Memória Livre (Heap + PSRAM + Flash)
    g_sys_mem_lbl = add_info_card("Memória Livre (RAM / PSRAM / Flash)", "Calculando memória...");

    // 3. Conexão Host (WebSocket + IP do Windows) & Wi-Fi local
    g_sys_ws_lbl   = add_info_card("Conexão Host (WebSocket)", "WS :81  •  Aguardando PC");
    g_sys_wifi_lbl = add_info_card("Rede Wi-Fi do Mini Deck", "Desconectado");

    // 4. Standby / Auto-Sleep (seletor: Desativado / 2 min / 5 min / 10 min)
    lv_obj_t *standby_row = create_sys_row(52);
    lv_obj_t *st_title = lv_label_create(standby_row);
    lv_label_set_text(st_title, "Standby / Auto-Sleep");
    lv_obj_set_pos(st_title, 12, 8);
    lv_obj_set_style_text_font(st_title, &inter_bold_16, 0);
    lv_obj_set_style_text_color(st_title, lv_color_hex(HEX_TEXT_MAIN), 0);

    lv_obj_t *st_sub = lv_label_create(standby_row);
    lv_label_set_text(st_sub, "Descanso de tela automático");
    lv_obj_set_pos(st_sub, 12, 29);
    lv_obj_set_style_text_font(st_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(st_sub, lv_color_hex(HEX_TEXT_DIM), 0);

    lv_obj_t *st_btn = lv_btn_create(standby_row);
    lv_obj_remove_style_all(st_btn);
    lv_obj_set_size(st_btn, 114, 34);
    lv_obj_align(st_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_radius(st_btn, 10, 0);
    lv_obj_set_style_bg_opa(st_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(st_btn, lv_color_hex(0x0A2A43), 0);
    lv_obj_set_style_bg_color(st_btn, lv_color_hex(0x0284C7), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(st_btn, 1, 0);
    lv_obj_set_style_border_color(st_btn, lv_color_hex(0x38BDF8), 0);
    lv_obj_add_event_cb(st_btn, standby_cycle_btn_cb, LV_EVENT_CLICKED, nullptr);

    g_sys_standby_lbl = lv_label_create(st_btn);
    lv_obj_set_style_text_font(g_sys_standby_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(g_sys_standby_lbl);

    // 5. Calibração Touch (reservado para rotina touch)
    lv_obj_t *calib_row = create_sys_row(50);
    lv_obj_t *cb_title = lv_label_create(calib_row);
    lv_label_set_text(cb_title, "Calibração Touch");
    lv_obj_set_pos(cb_title, 12, 8);
    lv_obj_set_style_text_font(cb_title, &inter_bold_16, 0);
    lv_obj_set_style_text_color(cb_title, lv_color_hex(HEX_TEXT_MAIN), 0);

    lv_obj_t *cb_sub = lv_label_create(calib_row);
    lv_label_set_text(cb_sub, "Capacitivo AXS15231B (I2C)");
    lv_obj_set_pos(cb_sub, 12, 28);
    lv_obj_set_style_text_font(cb_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cb_sub, lv_color_hex(HEX_TEXT_DIM), 0);

    lv_obj_t *cb_btn = lv_btn_create(calib_row);
    lv_obj_remove_style_all(cb_btn);
    lv_obj_set_size(cb_btn, 114, 34);
    lv_obj_align(cb_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_radius(cb_btn, 10, 0);
    lv_obj_set_style_bg_opa(cb_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cb_btn, lv_color_hex(HEX_BTN_IDLE), 0);
    lv_obj_set_style_bg_color(cb_btn, lv_color_hex(0x064E3B), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(cb_btn, 1, 0);
    lv_obj_set_style_border_color(cb_btn, lv_color_hex(HEX_CARD_BORDER), 0);
    lv_obj_add_event_cb(cb_btn, touch_calib_btn_cb, LV_EVENT_CLICKED, nullptr);

    g_sys_calib_lbl = lv_label_create(cb_btn);
    lv_label_set_text(g_sys_calib_lbl, "Calibrar");
    lv_obj_set_style_text_font(g_sys_calib_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_sys_calib_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_center(g_sys_calib_lbl);

    // 6. Botão Seguro de Reiniciar Mini Deck
    lv_obj_t *rst_btn = lv_btn_create(list);
    lv_obj_remove_style_all(rst_btn);
    lv_obj_set_size(rst_btn, 386, 48);
    lv_obj_set_style_radius(rst_btn, 12, 0);
    lv_obj_set_style_bg_opa(rst_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rst_btn, lv_color_hex(0x450A0A), 0);
    lv_obj_set_style_bg_color(rst_btn, lv_color_hex(0x991B1B), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(rst_btn, 2, 0);
    lv_obj_set_style_border_color(rst_btn, lv_color_hex(0xEF4444), 0);
    lv_obj_set_layout(rst_btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rst_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rst_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(rst_btn, 10, 0);
    lv_obj_add_event_cb(rst_btn, system_restart_btn_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *rst_ic = lv_label_create(rst_btn);
    lv_label_set_text(rst_ic, PH_ICN_POWER);
    lv_obj_set_style_text_font(rst_ic, &phosphor_20, 0);
    lv_obj_set_style_text_color(rst_ic, lv_color_hex(0xFCA5A5), 0);

    g_sys_restart_lbl = lv_label_create(rst_btn);
    lv_label_set_text(g_sys_restart_lbl, "REINICIAR MINI DECK");
    lv_obj_set_style_text_font(g_sys_restart_lbl, &inter_bold_16, 0);
    lv_obj_set_style_text_color(g_sys_restart_lbl, lv_color_hex(0xFEE2E2), 0);

    update_standby_visuals();
    update_pc_bt_visuals();
    update_wifi_card_visuals();
    update_system_info_visuals();
}

// ============================================================================
// Tela 12: screen_screensaver (Descanso de Tela Inteligente — Relógio + Clima)
// ============================================================================
void build_screen_screensaver()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    g_screens[UI_SCREEN_SCREENSAVER] = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x03060E), 0);
    lv_obj_add_event_cb(scr, screensaver_tap_cb, LV_EVENT_CLICKED, nullptr);

    // Detalhe sutil no topo
    lv_obj_t *top_line = lv_obj_create(scr);
    lv_obj_remove_style_all(top_line);
    lv_obj_set_size(top_line, 72, 3);
    lv_obj_set_style_radius(top_line, 2, 0);
    lv_obj_set_style_bg_opa(top_line, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(top_line, lv_color_hex(HEX_CYAN), 0);
    lv_obj_align(top_line, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_clear_flag(top_line, LV_OBJ_FLAG_CLICKABLE);

    // Relógio Digital Grande Centralizado (48px)
    g_ss_clock_lbl = lv_label_create(scr);
    lv_label_set_text(g_ss_clock_lbl, g_last_clock);
    lv_obj_set_style_text_font(g_ss_clock_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_ss_clock_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_set_style_text_letter_space(g_ss_clock_lbl, 4, 0);
    lv_obj_align(g_ss_clock_lbl, LV_ALIGN_CENTER, 0, -36);
    lv_obj_clear_flag(g_ss_clock_lbl, LV_OBJ_FLAG_CLICKABLE);

    // Data NTP logo abaixo do relógio
    g_ss_date_lbl = lv_label_create(scr);
    lv_label_set_text(g_ss_date_lbl, g_last_date);
    lv_obj_set_style_text_font(g_ss_date_lbl, &inter_medium_16, 0);
    lv_obj_set_style_text_color(g_ss_date_lbl, lv_color_hex(HEX_TEXT_DIM), 0);
    lv_obj_align(g_ss_date_lbl, LV_ALIGN_CENTER, 0, 22);
    lv_obj_clear_flag(g_ss_date_lbl, LV_OBJ_FLAG_CLICKABLE);

    // Pílula de Clima (São Luís • 30°C)
    lv_obj_t *weather_pill = lv_obj_create(scr);
    lv_obj_remove_style_all(weather_pill);
    lv_obj_set_size(weather_pill, 228, 38);
    lv_obj_align(weather_pill, LV_ALIGN_CENTER, 0, 72);
    lv_obj_set_style_radius(weather_pill, 19, 0);
    lv_obj_set_style_bg_opa(weather_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(weather_pill, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_width(weather_pill, 1, 0);
    lv_obj_set_style_border_color(weather_pill, lv_color_hex(0x1E293B), 0);
    lv_obj_set_layout(weather_pill, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(weather_pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(weather_pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(weather_pill, 8, 0);
    lv_obj_clear_flag(weather_pill, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *w_ic = lv_label_create(weather_pill);
    lv_label_set_text(w_ic, PH_ICN_SMART);
    lv_obj_set_style_text_font(w_ic, &phosphor_20, 0);
    lv_obj_set_style_text_color(w_ic, lv_color_hex(0xFBBF24), 0);

    g_ss_weather_lbl = lv_label_create(weather_pill);
    lv_obj_set_style_text_font(g_ss_weather_lbl, &inter_bold_16, 0);
    lv_obj_set_style_text_color(g_ss_weather_lbl, lv_color_hex(0xE2E8F0), 0);

    // Aviso discreto na base
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Toque na tela para acordar");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x334155), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_clear_flag(hint, LV_OBJ_FLAG_CLICKABLE);

    update_screensaver_weather_visuals();
}

void apply_home_conn_badge_style(UiConnState state)
{
    if (!g_home_conn_badge || !g_home_conn_label) return;

    uint32_t bg_hex = 0x2A2312;
    uint32_t bdr_hex = HEX_WARN;
    uint32_t txt_hex = 0xFCD34D;
    const char *txt = "AGUARDANDO";

    switch (state)
    {
    case UI_CONN_WIFI:
        bg_hex = 0x064E3B;
        bdr_hex = HEX_OK;
        txt_hex = 0x34D399;
        txt = LV_SYMBOL_WIFI " PC OK";
        break;

    case UI_CONN_SYNCED:
        bg_hex = 0x064E3B;
        bdr_hex = HEX_OK;
        txt_hex = 0x34D399;
        txt = LV_SYMBOL_USB " PC OK";
        break;

    case UI_CONN_DISCONNECTED:
        bg_hex = 0x450A0A;
        bdr_hex = 0xEF4444;
        txt_hex = 0xF87171;
        txt = "OFFLINE";
        break;

    case UI_CONN_WAITING:
    default:
        break;
    }

    lv_obj_set_style_bg_color(g_home_conn_badge, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_border_color(g_home_conn_badge, lv_color_hex(bdr_hex), 0);
    lv_label_set_text(g_home_conn_label, txt);
    lv_obj_set_style_text_color(g_home_conn_label, lv_color_hex(txt_hex), 0);
    update_system_info_visuals();
}

} // namespace

// ============================================================================
// API Pública
// ============================================================================
void ui_init(
    UiVolumeCallback on_volume_change,
    UiMuteCallback on_mute_toggle,
    UiMediaCallback on_media_action,
    UiScreenCallback on_screen_change,
    UiMacroCallback on_macro_action,
    UiLaunchGameCallback on_launch_game,
    UiPcBluetoothCallback on_pc_bt_toggle,
    UiWifiScanCallback on_wifi_scan,
    UiWifiConnectCallback on_wifi_connect)
{
    g_vol_cb = on_volume_change;
    g_mute_cb = on_mute_toggle;
    g_media_cb = on_media_action;
    g_screen_cb = on_screen_change;
    g_macro_cb = on_macro_action;
    g_launch_game_cb = on_launch_game;
    g_pc_bt_cb = on_pc_bt_toggle;
    g_wifi_scan_cb = on_wifi_scan;
    g_wifi_connect_cb = on_wifi_connect;

    build_screen_home();
    build_screen_audio();
    build_screen_media();
    build_screen_hardware();
    build_screen_macros();
    build_screen_games();
    build_screen_game_hud();
    build_screen_settings();
    build_screen_wifi_manager();
    build_screen_brightness();
    build_screen_system();
    build_screen_screensaver();

    update_mic_visuals();
    for (SidebarWidgets *msb : g_master_sidebars)
    {
        update_sidebar_visuals(*msb, g_channels[0], PH_ICN_VOL);
    }
    update_sidebar_visuals(g_media_sidebar, g_channels[1], PH_ICN_BROWSER);
    update_sidebar_visuals(g_hud_sidebar, g_channels[3], PH_ICN_GAME);
    update_media_play_visuals();
    apply_home_conn_badge_style(UI_CONN_WAITING);

    g_active_screen = UI_SCREEN_HOME;
    lv_scr_load(g_screens[UI_SCREEN_HOME]);
}

void ui_show_screen(UiScreenId screen_id, bool animate)
{
    if (screen_id >= NUM_SCREENS) return;

    lv_obj_t *target = g_screens[screen_id];
    if (!target || lv_scr_act() == target) return;

    close_wifi_modal();
    g_active_screen = screen_id;

    if (animate)
    {
        lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_FADE_ON, 160, 0, false);
    }
    else
    {
        lv_scr_load(target);
    }

    if (g_screen_cb && screen_id != UI_SCREEN_SCREENSAVER)
    {
        const char *name = "home";
        if (screen_id == UI_SCREEN_AUDIO) name = "audio";
        else if (screen_id == UI_SCREEN_MEDIA) name = "media";
        else if (screen_id == UI_SCREEN_HARDWARE) name = "hardware";
        else if (screen_id == UI_SCREEN_MACROS) name = "macros";
        else if (screen_id == UI_SCREEN_GAMES) name = "games";
        else if (screen_id == UI_SCREEN_GAME_HUD) name = "game_hud";
        else if (screen_id == UI_SCREEN_SETTINGS) name = "settings";
        else if (screen_id == UI_SCREEN_WIFI) name = "wifi";
        else if (screen_id == UI_SCREEN_BRIGHTNESS) name = "brightness";
        else if (screen_id == UI_SCREEN_SYSTEM) name = "system";
        g_screen_cb(name);
    }
}

void ui_sync_volume(const char *target, int value)
{
    ChannelUi *ch = find_channel(target);
    if (!ch) return;

    const bool is_master  = (strcasecmp(ch->target, "master") == 0);
    const bool is_browser = (strcasecmp(ch->target, "browser") == 0);
    const bool is_game    = (strcasecmp(ch->target, "game") == 0);

    bool master_dragging = false;
    if (is_master)
    {
        for (SidebarWidgets *msb : g_master_sidebars)
        {
            if (msb->dragging)
            {
                master_dragging = true;
                break;
            }
        }
    }

    if (ch->dragging ||
        master_dragging ||
        (is_browser && g_media_sidebar.dragging) ||
        (is_game && g_hud_sidebar.dragging))
    {
        return;
    }

    const int clamped = clamp_vol(value);
    ch->value = clamped;
    ch->last_sent_value = clamped;

    g_syncing_from_pc = true;
    if (ch->slider)
    {
        lv_slider_set_value(ch->slider, clamped, LV_ANIM_OFF);
    }
    if (is_master)
    {
        for (SidebarWidgets *msb : g_master_sidebars)
        {
            if (msb->slider)
            {
                lv_slider_set_value(msb->slider, clamped, LV_ANIM_OFF);
            }
        }
    }
    if (is_browser && g_media_sidebar.slider)
    {
        lv_slider_set_value(g_media_sidebar.slider, clamped, LV_ANIM_OFF);
    }
    if (is_game && g_hud_sidebar.slider)
    {
        lv_slider_set_value(g_hud_sidebar.slider, clamped, LV_ANIM_OFF);
    }
    update_channel_visuals(*ch);
    g_syncing_from_pc = false;
}

void ui_sync_mute(const char *target, bool muted)
{
    if (!target) return;

    if (strcasecmp(target, "mic") == 0)
    {
        g_mic_muted = muted;
        update_mic_visuals();
        return;
    }

    ChannelUi *ch = find_channel(target);
    if (!ch) return;

    ch->muted = muted;
    update_channel_visuals(*ch);
}

void ui_sync_label(const char *target, const char *label)
{
    if (!target || !label || !label[0]) return;
    ChannelUi *ch = find_channel(target);
    if (!ch || !ch->name_label) return;

    lv_label_set_text(ch->name_label, label);
}

void ui_sync_media(const char *title, const char *artist, bool playing)
{
    if (g_media_title_lbl && title)
    {
        lv_label_set_text(g_media_title_lbl, title[0] ? title : "Nenhuma mídia");
    }
    if (g_media_artist_lbl && artist)
    {
        lv_label_set_text(g_media_artist_lbl, artist);
    }
    g_media_playing = playing;
    update_media_play_visuals();
}

void ui_sync_stats(
    int cpu_pct,
    int cpu_temp,
    int gpu_pct,
    int gpu_temp,
    int ram_pct,
    const char *ram_used,
    const char *ram_total,
    int disk_pct)
{
    const int cpu = clamp_vol(cpu_pct);
    const int gpu = clamp_vol(gpu_pct);
    const int ram = clamp_vol(ram_pct);

    auto update_temp_and_pct = [](lv_obj_t *pct_lbl, lv_obj_t *temp_lbl, int pct, uint32_t pct_color, int temp_c) {
        if (!pct_lbl) return;
        char pbuf[16];
        snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
        lv_label_set_text(pct_lbl, pbuf);
        lv_obj_set_style_text_color(pct_lbl, lv_color_hex(pct_color), 0);

        if (temp_lbl && temp_c > 0)
        {
            char tbuf[20];
            snprintf(tbuf, sizeof(tbuf), "| %d\xC2\xB0" "C", temp_c);
            lv_label_set_text(temp_lbl, tbuf);
            uint32_t temp_color = 0xCBD5E1;
            if (temp_c > 80)
            {
                temp_color = 0xEF4444; // Vermelho: alerta de aquecimento (> 80°C)
            }
            else if (temp_c >= 72)
            {
                temp_color = HEX_WARN; // Laranja/âmbar: temperatura elevada
            }
            lv_obj_set_style_text_color(temp_lbl, lv_color_hex(temp_color), 0);
            lv_obj_clear_flag(temp_lbl, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(temp_lbl, LV_ALIGN_TOP_RIGHT, -14, 12);
            lv_obj_align_to(pct_lbl, temp_lbl, LV_ALIGN_OUT_LEFT_MID, -6, 0);
        }
        else
        {
            if (temp_lbl)
            {
                lv_obj_add_flag(temp_lbl, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_align(pct_lbl, LV_ALIGN_TOP_RIGHT, -14, 8);
        }
    };

    // Cor dinâmica para CPU: verde/esmeralda (<65%), âmbar (65-84%), vermelho (>=85%)
    uint32_t cpu_color = 0x34D399;
    const char *cpu_state = "Uso normal";
    if (cpu >= 85)
    {
        cpu_color = 0xEF4444;
        cpu_state = "Carga alta!";
    }
    else if (cpu >= 65)
    {
        cpu_color = HEX_WARN;
        cpu_state = "Carga moderada";
    }
    if (cpu_temp > 80)
    {
        cpu_state = "Temperatura alta!";
    }

    if (g_hw_cpu_bar)
    {
        lv_bar_set_value(g_hw_cpu_bar, cpu, LV_ANIM_ON);
        lv_obj_set_style_bg_color(g_hw_cpu_bar, lv_color_hex(cpu_color), LV_PART_INDICATOR);
    }
    if (g_hw_cpu_accent)
    {
        lv_obj_set_style_bg_color(g_hw_cpu_accent, lv_color_hex(cpu_temp > 80 ? 0xEF4444 : cpu_color), 0);
    }
    update_temp_and_pct(g_hw_cpu_pct_lbl, g_hw_cpu_temp_lbl, cpu, cpu_color, cpu_temp);
    if (g_hw_cpu_state_lbl)
    {
        lv_label_set_text(g_hw_cpu_state_lbl, cpu_state);
        lv_obj_set_style_text_color(g_hw_cpu_state_lbl, lv_color_hex((cpu >= 85 || cpu_temp > 80) ? 0xF87171 : HEX_TEXT_DIM), 0);
    }
    if (g_hw_disk_lbl && disk_pct >= 0)
    {
        char dbuf[24];
        snprintf(dbuf, sizeof(dbuf), "Disco C: %d%%", clamp_vol(disk_pct));
        lv_label_set_text(g_hw_disk_lbl, dbuf);
    }

    // Cor dinâmica para GPU: violeta (<70%), âmbar (70-84%), vermelho (>=85%)
    uint32_t gpu_color = 0xA78BFA;
    const char *gpu_state = "Uso normal";
    if (gpu >= 85)
    {
        gpu_color = 0xEF4444;
        gpu_state = "Carga alta!";
    }
    else if (gpu >= 70)
    {
        gpu_color = HEX_WARN;
        gpu_state = "Carga moderada";
    }
    if (gpu_temp > 80)
    {
        gpu_state = "Temperatura alta!";
    }

    if (g_hw_gpu_bar)
    {
        lv_bar_set_value(g_hw_gpu_bar, gpu, LV_ANIM_ON);
        lv_obj_set_style_bg_color(g_hw_gpu_bar, lv_color_hex(gpu_color), LV_PART_INDICATOR);
    }
    if (g_hw_gpu_accent)
    {
        lv_obj_set_style_bg_color(g_hw_gpu_accent, lv_color_hex(gpu_temp > 80 ? 0xEF4444 : gpu_color), 0);
    }
    update_temp_and_pct(g_hw_gpu_pct_lbl, g_hw_gpu_temp_lbl, gpu, gpu_color, gpu_temp);
    if (g_hw_gpu_state_lbl)
    {
        lv_label_set_text(g_hw_gpu_state_lbl, gpu_state);
        lv_obj_set_style_text_color(g_hw_gpu_state_lbl, lv_color_hex((gpu >= 85 || gpu_temp > 80) ? 0xF87171 : HEX_TEXT_DIM), 0);
    }

    // Cor dinâmica para RAM: azul céu (<70%), âmbar (70-84%), vermelho (>=85%)
    uint32_t ram_color = 0x38BDF8;
    if (ram >= 85)
    {
        ram_color = 0xEF4444;
    }
    else if (ram >= 70)
    {
        ram_color = HEX_WARN;
    }

    if (g_hw_ram_bar)
    {
        lv_bar_set_value(g_hw_ram_bar, ram, LV_ANIM_ON);
        lv_obj_set_style_bg_color(g_hw_ram_bar, lv_color_hex(ram_color), LV_PART_INDICATOR);
    }
    if (g_hw_ram_accent)
    {
        lv_obj_set_style_bg_color(g_hw_ram_accent, lv_color_hex(ram_color), 0);
    }
    if (g_hw_ram_pct_lbl)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", ram);
        lv_label_set_text(g_hw_ram_pct_lbl, buf);
        lv_obj_set_style_text_color(g_hw_ram_pct_lbl, lv_color_hex(ram_color), 0);
    }
    if (g_hw_ram_gb_lbl && ram_used && ram_total)
    {
        char gbuf[40];
        snprintf(gbuf, sizeof(gbuf), "%s GB / %s GB", ram_used, ram_total);
        lv_label_set_text(g_hw_ram_gb_lbl, gbuf);
    }

    // ---- Atualiza também os 3 tiles + bloco genérico do Painel Game HUD ----
    auto temp_color_for = [](int t) -> uint32_t {
        if (t > 80) return 0xEF4444;
        if (t >= 72) return HEX_WARN;
        return 0xCBD5E1;
    };

    if (g_hud_cpu_pct_lbl)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", cpu);
        lv_label_set_text(g_hud_cpu_pct_lbl, buf);
        lv_obj_set_style_text_color(g_hud_cpu_pct_lbl, lv_color_hex(cpu_color), 0);
    }
    if (g_hud_cpu_temp_lbl)
    {
        char tbuf[16];
        if (cpu_temp > 0) snprintf(tbuf, sizeof(tbuf), "%d\xC2\xB0" "C", cpu_temp);
        else snprintf(tbuf, sizeof(tbuf), "--");
        lv_label_set_text(g_hud_cpu_temp_lbl, tbuf);
        lv_obj_set_style_text_color(g_hud_cpu_temp_lbl, lv_color_hex(temp_color_for(cpu_temp)), 0);
    }
    if (g_hud_cpu_bar)
    {
        lv_bar_set_value(g_hud_cpu_bar, cpu, LV_ANIM_ON);
        lv_obj_set_style_bg_color(g_hud_cpu_bar, lv_color_hex(cpu_color), LV_PART_INDICATOR);
    }
    if (g_hud_cpu_accent)
    {
        lv_obj_set_style_bg_color(g_hud_cpu_accent, lv_color_hex(cpu_temp > 80 ? 0xEF4444 : cpu_color), 0);
    }

    if (g_hud_gpu_pct_lbl)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", gpu);
        lv_label_set_text(g_hud_gpu_pct_lbl, buf);
        lv_obj_set_style_text_color(g_hud_gpu_pct_lbl, lv_color_hex(gpu_color), 0);
    }
    if (g_hud_gpu_temp_lbl)
    {
        char tbuf[16];
        if (gpu_temp > 0) snprintf(tbuf, sizeof(tbuf), "%d\xC2\xB0" "C", gpu_temp);
        else snprintf(tbuf, sizeof(tbuf), "--");
        lv_label_set_text(g_hud_gpu_temp_lbl, tbuf);
        lv_obj_set_style_text_color(g_hud_gpu_temp_lbl, lv_color_hex(temp_color_for(gpu_temp)), 0);
    }
    if (g_hud_gpu_bar)
    {
        lv_bar_set_value(g_hud_gpu_bar, gpu, LV_ANIM_ON);
        lv_obj_set_style_bg_color(g_hud_gpu_bar, lv_color_hex(gpu_color), LV_PART_INDICATOR);
    }
    if (g_hud_gpu_accent)
    {
        lv_obj_set_style_bg_color(g_hud_gpu_accent, lv_color_hex(gpu_temp > 80 ? 0xEF4444 : gpu_color), 0);
    }

    if (g_hud_ram_pct_lbl)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", ram);
        lv_label_set_text(g_hud_ram_pct_lbl, buf);
        lv_obj_set_style_text_color(g_hud_ram_pct_lbl, lv_color_hex(ram_color), 0);
    }
    if (g_hud_ram_gb_lbl && ram_used && ram_total)
    {
        char sbuf[28];
        snprintf(sbuf, sizeof(sbuf), "%s/%sG", ram_used, ram_total);
        lv_label_set_text(g_hud_ram_gb_lbl, sbuf);
    }
    if (g_hud_ram_bar)
    {
        lv_bar_set_value(g_hud_ram_bar, ram, LV_ANIM_ON);
        lv_obj_set_style_bg_color(g_hud_ram_bar, lv_color_hex(ram_color), LV_PART_INDICATOR);
    }
    if (g_hud_ram_accent)
    {
        lv_obj_set_style_bg_color(g_hud_ram_accent, lv_color_hex(ram_color), 0);
    }

    if (g_hud_gen_ram_lbl && ram_used && ram_total)
    {
        char gbuf[48];
        snprintf(gbuf, sizeof(gbuf), "RAM em uso: %s GB / %s GB (%d%%)", ram_used, ram_total, ram);
        lv_label_set_text(g_hud_gen_ram_lbl, gbuf);
    }
    if (disk_pct >= 0)
    {
        const int d = clamp_vol(disk_pct);
        if (g_hud_gen_disk_lbl)
        {
            char dbuf[28];
            snprintf(dbuf, sizeof(dbuf), "Disco C: %d%% utilizado", d);
            lv_label_set_text(g_hud_gen_disk_lbl, dbuf);
        }
        if (g_hud_gen_disk_bar)
        {
            const uint32_t dcol = (d >= 90) ? 0xEF4444 : (d >= 80 ? HEX_WARN : 0x34D399);
            lv_bar_set_value(g_hud_gen_disk_bar, d, LV_ANIM_ON);
            lv_obj_set_style_bg_color(g_hud_gen_disk_bar, lv_color_hex(dcol), LV_PART_INDICATOR);
        }
    }
}

void ui_sync_cs2_c4(const char *state, int time_left)
{
    if (!g_hud_cs2_panel || !g_hud_c4_icon_lbl || !g_hud_c4_time_lbl || !g_hud_c4_sub_lbl) return;

    const bool planted = (state != nullptr && strcasecmp(state, "planted") == 0);
    if (!planted)
    {
        lv_obj_set_style_bg_color(g_hud_cs2_panel, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_border_width(g_hud_cs2_panel, 1, 0);
        lv_obj_set_style_border_color(g_hud_cs2_panel, lv_color_hex(HEX_CARD_BORDER), 0);
        if (g_hud_c4_accent)
        {
            lv_obj_set_style_bg_color(g_hud_c4_accent, lv_color_hex(HEX_WARN), 0);
        }
        if (g_hud_c4_icon_box)
        {
            lv_obj_set_style_bg_color(g_hud_c4_icon_box, lv_color_hex(0x2A2012), 0);
            lv_obj_set_style_border_color(g_hud_c4_icon_box, lv_color_hex(HEX_WARN), 0);
        }
        lv_label_set_text(g_hud_c4_icon_lbl, PH_ICN_CROSSHAIR);
        lv_obj_set_style_text_color(g_hud_c4_icon_lbl, lv_color_hex(HEX_WARN), 0);

        if (g_hud_c4_title_lbl)
        {
            lv_label_set_text(g_hud_c4_title_lbl, "CS2 C4 TIMER (GSI)");
            lv_obj_set_style_text_color(g_hud_c4_title_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
        }
        lv_label_set_text(g_hud_c4_time_lbl, "--s");
        lv_obj_set_style_text_color(g_hud_c4_time_lbl, lv_color_hex(HEX_WARN), 0);

        lv_label_set_text(g_hud_c4_sub_lbl, "Aguardando plant na partida...");
        lv_obj_set_style_text_color(g_hud_c4_sub_lbl, lv_color_hex(HEX_TEXT_DIM), 0);

        if (g_hud_c4_bar)
        {
            lv_bar_set_value(g_hud_c4_bar, 0, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(g_hud_c4_bar, lv_color_hex(HEX_WARN), LV_PART_INDICATOR);
        }
        return;
    }

    const int secs = (time_left < 0) ? 0 : (time_left > 40 ? 40 : time_left);
    uint32_t bg_hex = 0x3B2308;
    uint32_t box_bg = 0x452A0A;
    uint32_t bdr_hex = HEX_WARN;
    uint32_t ic_hex = 0xFBBF24;
    uint32_t txt_hex = 0xFDE68A;
    const char *icon_utf8 = PH_ICN_FIRE;
    const char *title_txt = "BOMBA PLANTADA";
    const char *sub_txt = "Tempo restante para desarme";

    if (secs <= 5)
    {
        bg_hex = 0x991B1B;
        box_bg = 0x7F1D1D;
        bdr_hex = 0xFF4444;
        ic_hex = 0xFFFFFF;
        txt_hex = 0xFFFFFF;
        icon_utf8 = PH_ICN_WARNING;
        title_txt = "CORRE! VAI EXPLODIR!";
        sub_txt = "Menos de 5s (sem tempo com kit)";
    }
    else if (secs <= 10)
    {
        bg_hex = 0x450A0A;
        box_bg = 0x5C1111;
        bdr_hex = 0xEF4444;
        ic_hex = 0xF87171;
        txt_hex = 0xFCA5A5;
        icon_utf8 = PH_ICN_WARNING;
        title_txt = "ALERTA! SEM KIT!";
        sub_txt = "Apenas com Defuse Kit (< 10s)";
    }

    lv_obj_set_style_bg_color(g_hud_cs2_panel, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_border_width(g_hud_cs2_panel, 2, 0);
    lv_obj_set_style_border_color(g_hud_cs2_panel, lv_color_hex(bdr_hex), 0);
    if (g_hud_c4_accent)
    {
        lv_obj_set_style_bg_color(g_hud_c4_accent, lv_color_hex(bdr_hex), 0);
    }
    if (g_hud_c4_icon_box)
    {
        lv_obj_set_style_bg_color(g_hud_c4_icon_box, lv_color_hex(box_bg), 0);
        lv_obj_set_style_border_color(g_hud_c4_icon_box, lv_color_hex(bdr_hex), 0);
    }

    lv_label_set_text(g_hud_c4_icon_lbl, icon_utf8);
    lv_obj_set_style_text_color(g_hud_c4_icon_lbl, lv_color_hex(ic_hex), 0);

    if (g_hud_c4_title_lbl)
    {
        lv_label_set_text(g_hud_c4_title_lbl, title_txt);
        lv_obj_set_style_text_color(g_hud_c4_title_lbl, lv_color_hex(txt_hex), 0);
    }

    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%ds", secs);
    lv_label_set_text(g_hud_c4_time_lbl, tbuf);
    lv_obj_set_style_text_color(g_hud_c4_time_lbl, lv_color_hex(txt_hex), 0);

    lv_label_set_text(g_hud_c4_sub_lbl, sub_txt);
    lv_obj_set_style_text_color(g_hud_c4_sub_lbl, lv_color_hex(txt_hex), 0);

    if (g_hud_c4_bar)
    {
        lv_bar_set_value(g_hud_c4_bar, secs, LV_ANIM_ON);
        lv_obj_set_style_bg_color(g_hud_c4_bar, lv_color_hex(bdr_hex), LV_PART_INDICATOR);
    }
}

void ui_sync_pc_bluetooth(bool enabled)
{
    g_pc_bt_enabled = enabled;
    update_pc_bt_visuals();
}

void ui_set_conn_state(UiConnState state)
{
    if (g_conn_state == state) return;
    g_conn_state = state;
    apply_home_conn_badge_style(state);
}

void ui_set_pc_connected(bool connected)
{
    ui_set_conn_state(connected ? UI_CONN_SYNCED : UI_CONN_DISCONNECTED);
}

void ui_set_status_text(const char *text)
{
    (void)text;
}

void ui_set_wifi_status(bool connected, const char *ip_str, const char *ssid, int32_t rssi)
{
    g_wifi_connected = connected;
    g_wifi_rssi = rssi;

    if (ssid && ssid[0] != '\0')
    {
        strncpy(g_wifi_ssid_str, ssid, sizeof(g_wifi_ssid_str) - 1);
        g_wifi_ssid_str[sizeof(g_wifi_ssid_str) - 1] = '\0';
    }
    else if (!connected)
    {
        strncpy(g_wifi_ssid_str, "Desconectado", sizeof(g_wifi_ssid_str) - 1);
        g_wifi_ssid_str[sizeof(g_wifi_ssid_str) - 1] = '\0';
    }

    if (connected && ip_str && ip_str[0] != '\0')
    {
        strncpy(g_wifi_ip, ip_str, sizeof(g_wifi_ip) - 1);
        g_wifi_ip[sizeof(g_wifi_ip) - 1] = '\0';
    }
    else
    {
        g_wifi_ip[0] = '\0';
    }

    update_wifi_card_visuals();
}

void ui_set_wifi_scan_status(const char *status_text)
{
    if (!g_net_scan_status_lbl || !status_text) return;
    lv_label_set_text(g_net_scan_status_lbl, status_text);

    uint32_t col = HEX_TEXT_DIM;
    if (strncmp(status_text, "Falha", 5) == 0)
    {
        col = HEX_WARN;
    }
    else if (strncmp(status_text, "Conectado", 9) == 0 ||
             strncmp(status_text, "Rede anterior", 13) == 0)
    {
        col = 0x34D399;
    }
    else if (strncmp(status_text, "Conectando", 10) == 0 ||
             strncmp(status_text, "Escaneando", 10) == 0)
    {
        col = HEX_CYAN;
    }
    lv_obj_set_style_text_color(g_net_scan_status_lbl, lv_color_hex(col), 0);
}

void ui_set_wifi_scan_results(const UiWifiNetwork *networks, int count)
{
    if (!g_net_wifi_list) return;

    lv_obj_clean(g_net_wifi_list);
    g_wifi_scan_count = 0;

    if (!networks || count <= 0)
    {
        ui_set_wifi_scan_status("Nenhuma rede encontrada");
        return;
    }

    const int n = (count > MAX_WIFI_SCAN_RESULTS) ? MAX_WIFI_SCAN_RESULTS : count;
    for (int i = 0; i < n; ++i)
    {
        g_wifi_scan_cache[i] = networks[i];
        g_wifi_scan_count++;

        char line[72];
        snprintf(
            line,
            sizeof(line),
            "%s  (%ld dBm)%s",
            networks[i].ssid,
            static_cast<long>(networks[i].rssi),
            networks[i].secured ? "  *" : "");

        lv_obj_t *item = lv_list_add_btn(g_net_wifi_list, LV_SYMBOL_WIFI, line);
        lv_obj_set_height(item, 38);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x0A2A43), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(item, lv_color_hex(HEX_TEXT_MAIN), 0);
        lv_obj_set_style_text_font(item, &lv_font_montserrat_14, 0);
        lv_obj_set_style_radius(item, 9, 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_border_color(item, lv_color_hex(HEX_CARD_BORDER), 0);
        lv_obj_set_style_pad_ver(item, 6, 0);
        lv_obj_set_style_pad_hor(item, 10, 0);
        lv_obj_add_event_cb(
            item,
            wifi_list_item_cb,
            LV_EVENT_CLICKED,
            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }

    char sbuf[40];
    snprintf(sbuf, sizeof(sbuf), "%d redes (toque p/ conectar)", g_wifi_scan_count);
    ui_set_wifi_scan_status(sbuf);
}

void ui_set_clock_text(const char *hhmm)
{
    if (!hhmm || !hhmm[0]) return;
    if (strcmp(g_last_clock, hhmm) == 0) return;

    strncpy(g_last_clock, hhmm, sizeof(g_last_clock) - 1);
    g_last_clock[sizeof(g_last_clock) - 1] = '\0';

    for (int i = 0; i < NUM_SCREENS; ++i)
    {
        if (g_header_clock[i])
        {
            lv_label_set_text(g_header_clock[i], g_last_clock);
        }
    }
    if (g_ss_clock_lbl)
    {
        lv_label_set_text(g_ss_clock_lbl, g_last_clock);
    }
}

void ui_set_date_text(const char *date_str)
{
    if (!date_str || !date_str[0]) return;
    if (strcmp(g_last_date, date_str) == 0) return;

    strncpy(g_last_date, date_str, sizeof(g_last_date) - 1);
    g_last_date[sizeof(g_last_date) - 1] = '\0';

    if (g_ss_date_lbl)
    {
        lv_label_set_text(g_ss_date_lbl, g_last_date);
    }
}

void ui_sync_weather(int temp_c, const char *city)
{
    g_weather_temp = temp_c;
    g_has_weather = true;
    if (city && city[0] != '\0')
    {
        strncpy(g_weather_city, city, sizeof(g_weather_city) - 1);
        g_weather_city[sizeof(g_weather_city) - 1] = '\0';
    }
    update_screensaver_weather_visuals();
}

void ui_set_system_metrics(uint32_t free_heap_kb, uint32_t free_psram_kb, uint32_t free_flash_kb, const char *host_ip)
{
    if (host_ip)
    {
        strncpy(g_ws_host_ip, host_ip, sizeof(g_ws_host_ip) - 1);
        g_ws_host_ip[sizeof(g_ws_host_ip) - 1] = '\0';
    }

    if (g_sys_mem_lbl)
    {
        char mbuf[72];
        const unsigned psram_mb_int = free_psram_kb / 1024U;
        const unsigned psram_mb_dec = ((free_psram_kb % 1024U) * 10U) / 1024U;
        const unsigned flash_mb_int = free_flash_kb / 1024U;
        const unsigned flash_mb_dec = ((free_flash_kb % 1024U) * 10U) / 1024U;
        snprintf(
            mbuf,
            sizeof(mbuf),
            "Heap %lu KB  •  PSRAM %u.%u MB  •  Flash %u.%u MB",
            static_cast<unsigned long>(free_heap_kb),
            psram_mb_int,
            psram_mb_dec,
            flash_mb_int,
            flash_mb_dec);
        lv_label_set_text(g_sys_mem_lbl, mbuf);
    }

    update_system_info_visuals();
}

bool ui_is_screensaver_active()
{
    return (g_active_screen == UI_SCREEN_SCREENSAVER);
}

UiScreenId ui_get_active_screen()
{
    return g_active_screen;
}

bool ui_is_wifi_modal_open()
{
    return (g_wifi_modal != nullptr) && !lv_obj_has_flag(g_wifi_modal, LV_OBJ_FLAG_HIDDEN);
}

void ui_enter_screensaver()
{
    if (g_active_screen == UI_SCREEN_SCREENSAVER) return;
    if (g_active_screen == UI_SCREEN_GAME_HUD) return;
    if (ui_is_wifi_modal_open()) return;

    g_prev_screen_before_ss = g_active_screen;
    set_screensaver_dim(true);
    ui_show_screen(UI_SCREEN_SCREENSAVER, true);
}

void ui_wake_screensaver()
{
    if (g_active_screen != UI_SCREEN_SCREENSAVER) return;

    set_screensaver_dim(false);
    UiScreenId restore = g_prev_screen_before_ss;
    if (restore == UI_SCREEN_SCREENSAVER || restore >= NUM_SCREENS)
    {
        restore = UI_SCREEN_HOME;
    }
    ui_show_screen(restore, true);
}

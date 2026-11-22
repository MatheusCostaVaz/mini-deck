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
constexpr int      NUM_SCREENS        = 5;

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
    HubCardDef("media",    "Mídia",    PH_ICN_MUSIC, HEX_ROSE, 0x3B1024, UI_SCREEN_MEDIA),
    HubCardDef("mixer",    "Mixer",    PH_ICN_MIXER, HEX_CYAN, 0x083344, UI_SCREEN_AUDIO),
    HubCardDef("light",    "Brilho",   PH_ICN_SMART, 0xFBBF24, 0x3B2608, -1),
    HubCardDef("network",  "Rede",     PH_ICN_WIFI,  0x38BDF8, 0x0A2A43, -1),
    HubCardDef("macros",   "Atalhos",  PH_ICN_APP,   0xA78BFA, 0x22184A, UI_SCREEN_MACROS),
    HubCardDef("hardware", "Hardware", PH_ICN_CPU,   0x34D399, 0x064E3B, UI_SCREEN_HARDWARE),
};

// ============================================================================
// Telas Principais e Widgets Persistentes
// ============================================================================
lv_obj_t *g_screens[NUM_SCREENS]      = {nullptr, nullptr, nullptr, nullptr, nullptr};
lv_obj_t *g_header_clock[NUM_SCREENS] = {nullptr, nullptr, nullptr, nullptr, nullptr};
HeaderMicWidgets g_header_mic[NUM_SCREENS];

// Badge de conexão no centro do Header da Home
lv_obj_t *g_home_conn_badge = nullptr;
lv_obj_t *g_home_conn_label = nullptr;

// Sliders laterais esquerdos persistentes (Home/Hardware/Atalhos = Master, Mídia = Browser/Edge)
SidebarWidgets g_home_sidebar;
SidebarWidgets g_media_sidebar;
SidebarWidgets g_hw_sidebar;
SidebarWidgets g_macros_sidebar;

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
lv_obj_t *g_hw_cpu_bar       = nullptr;
lv_obj_t *g_hw_cpu_state_lbl = nullptr;
lv_obj_t *g_hw_disk_lbl      = nullptr;

lv_obj_t *g_hw_gpu_accent    = nullptr;
lv_obj_t *g_hw_gpu_pct_lbl   = nullptr;
lv_obj_t *g_hw_gpu_bar       = nullptr;
lv_obj_t *g_hw_gpu_state_lbl = nullptr;

lv_obj_t *g_hw_ram_accent    = nullptr;
lv_obj_t *g_hw_ram_pct_lbl   = nullptr;
lv_obj_t *g_hw_ram_bar       = nullptr;
lv_obj_t *g_hw_ram_gb_lbl    = nullptr;

// Widgets do Módulo CS2 C4 (Card 6 em screen_macros)
lv_obj_t *g_c4_btn      = nullptr;
lv_obj_t *g_c4_accent   = nullptr;
lv_obj_t *g_c4_icon_lbl = nullptr;
lv_obj_t *g_c4_main_lbl = nullptr;
lv_obj_t *g_c4_sub_lbl  = nullptr;

// Estado Global Compartilhado
UiConnState g_conn_state = UI_CONN_WAITING;
bool g_mic_muted         = false;
bool g_media_playing     = false;
bool g_wifi_connected    = false;
char g_wifi_ip[24]       = "";
char g_last_clock[16]    = "--:--";
bool g_syncing_from_pc   = false;

UiVolumeCallback g_vol_cb    = nullptr;
UiMuteCallback   g_mute_cb   = nullptr;
UiMediaCallback  g_media_cb  = nullptr;
UiScreenCallback g_screen_cb = nullptr;
UiMacroCallback  g_macro_cb  = nullptr;

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
        update_sidebar_visuals(g_home_sidebar, ch, PH_ICN_VOL);
        update_sidebar_visuals(g_hw_sidebar, ch, PH_ICN_VOL);
        update_sidebar_visuals(g_macros_sidebar, ch, PH_ICN_VOL);
    }
    else if (strcasecmp(ch.target, "browser") == 0)
    {
        update_sidebar_visuals(g_media_sidebar, ch, PH_ICN_BROWSER);
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
    ChannelUi &ch = is_browser ? g_channels[1] : g_channels[0];
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
        if (!is_browser)
        {
            if (sb != &g_home_sidebar && g_home_sidebar.slider)
            {
                lv_slider_set_value(g_home_sidebar.slider, ch.value, LV_ANIM_OFF);
            }
            if (sb != &g_hw_sidebar && g_hw_sidebar.slider)
            {
                lv_slider_set_value(g_hw_sidebar.slider, ch.value, LV_ANIM_OFF);
            }
            if (sb != &g_macros_sidebar && g_macros_sidebar.slider)
            {
                lv_slider_set_value(g_macros_sidebar.slider, ch.value, LV_ANIM_OFF);
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
    if (ch_idx < 0 || ch_idx > 1 || g_syncing_from_pc) return;

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
            if (g_home_sidebar.slider)
            {
                lv_slider_set_value(g_home_sidebar.slider, ch->value, LV_ANIM_OFF);
            }
            if (g_hw_sidebar.slider)
            {
                lv_slider_set_value(g_hw_sidebar.slider, ch->value, LV_ANIM_OFF);
            }
            if (g_macros_sidebar.slider)
            {
                lv_slider_set_value(g_macros_sidebar.slider, ch->value, LV_ANIM_OFF);
            }
            g_syncing_from_pc = false;
        }
        else if (strcasecmp(ch->target, "browser") == 0 && g_media_sidebar.slider)
        {
            g_syncing_from_pc = true;
            lv_slider_set_value(g_media_sidebar.slider, ch->value, LV_ANIM_OFF);
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
        if (center_icon_utf8 && center_icon_utf8[0] != '\0')
        {
            lv_obj_t *mod_ic = lv_label_create(center_box);
            lv_label_set_text(mod_ic, center_icon_utf8);
            lv_obj_set_style_text_font(mod_ic, &phosphor_20, 0);
            lv_obj_set_style_text_color(mod_ic, lv_color_hex(center_accent_hex), 0);
        }

        lv_obj_t *mod_title = lv_label_create(center_box);
        lv_label_set_text(mod_title, center_title ? center_title : "");
        lv_obj_set_style_text_font(mod_title, &inter_bold_16, 0);
        lv_obj_set_style_text_color(mod_title, lv_color_hex(HEX_TEXT_MAIN), 0);
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
    const char *icon_utf8)
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
    lv_obj_set_size(ic_btn, 58, 28);
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

    sb.slider = lv_slider_create(sidebar);
    lv_slider_set_range(sb.slider, 0, 100);
    lv_slider_set_value(sb.slider, ch.value, LV_ANIM_OFF);
    lv_obj_set_size(sb.slider, 18, 156);
    lv_obj_align(sb.slider, LV_ALIGN_TOP_MID, 0, 42);
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
    build_volume_sidebar(scr, g_home_sidebar, 0, PH_ICN_VOL);

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

    // Card 6 (col=2, row=1): Widget Especial CS2 C4 (GSI Timer Dinâmico)
    const int16_t c4_x = GRID_X + 2 * (CARD_W + GAP_X);
    const int16_t c4_y = GRID_Y + 1 * (CARD_H + GAP_Y);

    g_c4_btn = lv_btn_create(scr);
    lv_obj_remove_style_all(g_c4_btn);
    lv_obj_set_pos(g_c4_btn, c4_x, c4_y);
    lv_obj_set_size(g_c4_btn, CARD_W, CARD_H);
    lv_obj_clear_flag(g_c4_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(g_c4_btn, 16, 0);
    lv_obj_set_style_bg_opa(g_c4_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_c4_btn, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(g_c4_btn, 1, 0);
    lv_obj_set_style_border_color(g_c4_btn, lv_color_hex(HEX_CARD_BORDER), 0);
    lv_obj_set_style_bg_color(g_c4_btn, lv_color_hex(0x3B2308), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(g_c4_btn, 2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(g_c4_btn, lv_color_hex(HEX_WARN), LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_c4_btn, macro_btn_cb, LV_EVENT_CLICKED, const_cast<char *>("cs2_c4"));

    g_c4_accent = lv_obj_create(g_c4_btn);
    lv_obj_remove_style_all(g_c4_accent);
    lv_obj_set_size(g_c4_accent, 42, 3);
    lv_obj_set_style_radius(g_c4_accent, 2, 0);
    lv_obj_set_style_bg_opa(g_c4_accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_c4_accent, lv_color_hex(HEX_WARN), 0);
    lv_obj_align(g_c4_accent, LV_ALIGN_TOP_MID, 0, 0);

    g_c4_icon_lbl = lv_label_create(g_c4_btn);
    lv_label_set_text(g_c4_icon_lbl, PH_ICN_CROSSHAIR);
    lv_obj_set_style_text_font(g_c4_icon_lbl, &phosphor_32, 0);
    lv_obj_set_style_text_color(g_c4_icon_lbl, lv_color_hex(HEX_WARN), 0);
    lv_obj_align(g_c4_icon_lbl, LV_ALIGN_TOP_MID, 0, 14);

    g_c4_main_lbl = lv_label_create(g_c4_btn);
    lv_label_set_text(g_c4_main_lbl, "CS2 C4");
    lv_obj_set_style_text_font(g_c4_main_lbl, &inter_bold_16, 0);
    lv_obj_set_style_text_color(g_c4_main_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_align(g_c4_main_lbl, LV_ALIGN_CENTER, 0, 16);

    g_c4_sub_lbl = lv_label_create(g_c4_btn);
    lv_label_set_text(g_c4_sub_lbl, "AGUARDANDO");
    lv_obj_set_style_text_font(g_c4_sub_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_c4_sub_lbl, lv_color_hex(HEX_TEXT_DIM), 0);
    lv_obj_align(g_c4_sub_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);
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
    UiMacroCallback on_macro_action)
{
    g_vol_cb = on_volume_change;
    g_mute_cb = on_mute_toggle;
    g_media_cb = on_media_action;
    g_screen_cb = on_screen_change;
    g_macro_cb = on_macro_action;

    build_screen_home();
    build_screen_audio();
    build_screen_media();
    build_screen_hardware();
    build_screen_macros();

    update_mic_visuals();
    update_sidebar_visuals(g_home_sidebar, g_channels[0], PH_ICN_VOL);
    update_sidebar_visuals(g_media_sidebar, g_channels[1], PH_ICN_BROWSER);
    update_sidebar_visuals(g_hw_sidebar, g_channels[0], PH_ICN_VOL);
    update_sidebar_visuals(g_macros_sidebar, g_channels[0], PH_ICN_VOL);
    update_media_play_visuals();
    apply_home_conn_badge_style(UI_CONN_WAITING);

    lv_scr_load(g_screens[UI_SCREEN_HOME]);
}

void ui_show_screen(UiScreenId screen_id, bool animate)
{
    if (screen_id >= NUM_SCREENS) return;

    lv_obj_t *target = g_screens[screen_id];
    if (!target || lv_scr_act() == target) return;

    if (animate)
    {
        lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_FADE_ON, 160, 0, false);
    }
    else
    {
        lv_scr_load(target);
    }

    if (g_screen_cb)
    {
        const char *name = "home";
        if (screen_id == UI_SCREEN_AUDIO) name = "audio";
        else if (screen_id == UI_SCREEN_MEDIA) name = "media";
        else if (screen_id == UI_SCREEN_HARDWARE) name = "hardware";
        else if (screen_id == UI_SCREEN_MACROS) name = "macros";
        g_screen_cb(name);
    }
}

void ui_sync_volume(const char *target, int value)
{
    ChannelUi *ch = find_channel(target);
    if (!ch) return;

    const bool is_master  = (strcasecmp(ch->target, "master") == 0);
    const bool is_browser = (strcasecmp(ch->target, "browser") == 0);
    if (ch->dragging ||
        (is_master && (g_home_sidebar.dragging || g_hw_sidebar.dragging || g_macros_sidebar.dragging)) ||
        (is_browser && g_media_sidebar.dragging))
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
        if (g_home_sidebar.slider)
        {
            lv_slider_set_value(g_home_sidebar.slider, clamped, LV_ANIM_OFF);
        }
        if (g_hw_sidebar.slider)
        {
            lv_slider_set_value(g_hw_sidebar.slider, clamped, LV_ANIM_OFF);
        }
        if (g_macros_sidebar.slider)
        {
            lv_slider_set_value(g_macros_sidebar.slider, clamped, LV_ANIM_OFF);
        }
    }
    if (is_browser && g_media_sidebar.slider)
    {
        lv_slider_set_value(g_media_sidebar.slider, clamped, LV_ANIM_OFF);
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

void ui_sync_stats(int cpu_pct, int gpu_pct, int ram_pct, const char *ram_used, const char *ram_total, int disk_pct)
{
    const int cpu = clamp_vol(cpu_pct);
    const int gpu = clamp_vol(gpu_pct);
    const int ram = clamp_vol(ram_pct);

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

    if (g_hw_cpu_bar)
    {
        lv_bar_set_value(g_hw_cpu_bar, cpu, LV_ANIM_ON);
        lv_obj_set_style_bg_color(g_hw_cpu_bar, lv_color_hex(cpu_color), LV_PART_INDICATOR);
    }
    if (g_hw_cpu_accent)
    {
        lv_obj_set_style_bg_color(g_hw_cpu_accent, lv_color_hex(cpu_color), 0);
    }
    if (g_hw_cpu_pct_lbl)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", cpu);
        lv_label_set_text(g_hw_cpu_pct_lbl, buf);
        lv_obj_set_style_text_color(g_hw_cpu_pct_lbl, lv_color_hex(cpu_color), 0);
    }
    if (g_hw_cpu_state_lbl)
    {
        lv_label_set_text(g_hw_cpu_state_lbl, cpu_state);
        lv_obj_set_style_text_color(g_hw_cpu_state_lbl, lv_color_hex(cpu >= 85 ? 0xF87171 : HEX_TEXT_DIM), 0);
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

    if (g_hw_gpu_bar)
    {
        lv_bar_set_value(g_hw_gpu_bar, gpu, LV_ANIM_ON);
        lv_obj_set_style_bg_color(g_hw_gpu_bar, lv_color_hex(gpu_color), LV_PART_INDICATOR);
    }
    if (g_hw_gpu_accent)
    {
        lv_obj_set_style_bg_color(g_hw_gpu_accent, lv_color_hex(gpu_color), 0);
    }
    if (g_hw_gpu_pct_lbl)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", gpu);
        lv_label_set_text(g_hw_gpu_pct_lbl, buf);
        lv_obj_set_style_text_color(g_hw_gpu_pct_lbl, lv_color_hex(gpu_color), 0);
    }
    if (g_hw_gpu_state_lbl)
    {
        lv_label_set_text(g_hw_gpu_state_lbl, gpu_state);
        lv_obj_set_style_text_color(g_hw_gpu_state_lbl, lv_color_hex(gpu >= 85 ? 0xF87171 : HEX_TEXT_DIM), 0);
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
}

void ui_sync_cs2_c4(const char *state, int time_left)
{
    if (!g_c4_btn || !g_c4_icon_lbl || !g_c4_main_lbl || !g_c4_sub_lbl) return;

    const bool planted = (state != nullptr && strcasecmp(state, "planted") == 0);
    if (!planted)
    {
        lv_obj_set_style_bg_color(g_c4_btn, lv_color_hex(HEX_CARD), 0);
        lv_obj_set_style_border_width(g_c4_btn, 1, 0);
        lv_obj_set_style_border_color(g_c4_btn, lv_color_hex(HEX_CARD_BORDER), 0);
        if (g_c4_accent)
        {
            lv_obj_set_style_bg_color(g_c4_accent, lv_color_hex(HEX_WARN), 0);
        }
        lv_label_set_text(g_c4_icon_lbl, PH_ICN_CROSSHAIR);
        lv_obj_set_style_text_color(g_c4_icon_lbl, lv_color_hex(HEX_WARN), 0);

        lv_label_set_text(g_c4_main_lbl, "CS2 C4");
        lv_obj_set_style_text_font(g_c4_main_lbl, &inter_bold_16, 0);
        lv_obj_set_style_text_color(g_c4_main_lbl, lv_color_hex(HEX_TEXT_MAIN), 0);
        lv_obj_align(g_c4_main_lbl, LV_ALIGN_CENTER, 0, 16);

        lv_label_set_text(g_c4_sub_lbl, "AGUARDANDO");
        lv_obj_set_style_text_color(g_c4_sub_lbl, lv_color_hex(HEX_TEXT_DIM), 0);
        return;
    }

    const int secs = (time_left < 0) ? 0 : time_left;
    uint32_t bg_hex = 0x3B2308;
    uint32_t bdr_hex = HEX_WARN;
    uint32_t ic_hex = 0xFBBF24;
    uint32_t txt_hex = 0xFDE68A;
    const char *icon_utf8 = PH_ICN_FIRE;
    const char *sub_txt = "C4 PLANTADA";

    if (secs <= 5)
    {
        // Crítico (< 5s): impossível desarmar mesmo com kit
        bg_hex = 0x991B1B;
        bdr_hex = 0xFF4444;
        ic_hex = 0xFFFFFF;
        txt_hex = 0xFFFFFF;
        icon_utf8 = PH_ICN_WARNING;
        sub_txt = "CORRE!";
    }
    else if (secs <= 10)
    {
        // Alerta máximo (<= 10s): sem kit não dá mais tempo
        bg_hex = 0x450A0A;
        bdr_hex = 0xEF4444;
        ic_hex = 0xF87171;
        txt_hex = 0xFCA5A5;
        icon_utf8 = PH_ICN_WARNING;
        sub_txt = "SEM KIT!";
    }

    lv_obj_set_style_bg_color(g_c4_btn, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_border_width(g_c4_btn, 2, 0);
    lv_obj_set_style_border_color(g_c4_btn, lv_color_hex(bdr_hex), 0);
    if (g_c4_accent)
    {
        lv_obj_set_style_bg_color(g_c4_accent, lv_color_hex(bdr_hex), 0);
    }

    lv_label_set_text(g_c4_icon_lbl, icon_utf8);
    lv_obj_set_style_text_color(g_c4_icon_lbl, lv_color_hex(ic_hex), 0);

    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%ds", secs);
    lv_label_set_text(g_c4_main_lbl, tbuf);
    lv_obj_set_style_text_font(g_c4_main_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_c4_main_lbl, lv_color_hex(txt_hex), 0);
    lv_obj_align(g_c4_main_lbl, LV_ALIGN_CENTER, 0, 15);

    lv_label_set_text(g_c4_sub_lbl, sub_txt);
    lv_obj_set_style_text_color(g_c4_sub_lbl, lv_color_hex(txt_hex), 0);
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

void ui_set_wifi_status(bool connected, const char *ip_str)
{
    g_wifi_connected = connected;
    if (connected && ip_str && ip_str[0] != '\0')
    {
        strncpy(g_wifi_ip, ip_str, sizeof(g_wifi_ip) - 1);
        g_wifi_ip[sizeof(g_wifi_ip) - 1] = '\0';
    }
    else
    {
        g_wifi_ip[0] = '\0';
    }
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
}

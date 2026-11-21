#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

// ============================================================================
// Paleta Dark Theme (480x320)
// ============================================================================
constexpr uint32_t HEX_BG          = 0x0B1120;
constexpr uint32_t HEX_CARD        = 0x131C2E;
constexpr uint32_t HEX_CARD_BORDER = 0x24324A;
constexpr uint32_t HEX_TRACK       = 0x1E293B;
constexpr uint32_t HEX_BTN_IDLE    = 0x1E293B;
constexpr uint32_t HEX_TEXT_MAIN   = 0xF8FAFC;
constexpr uint32_t HEX_TEXT_DIM    = 0x94A3B8;
constexpr uint32_t HEX_MUTE_BG     = 0xDC2626;
constexpr uint32_t HEX_MUTE_BORDER = 0xF87171;
constexpr uint32_t HEX_MIC_OK_BG   = 0x064E3B;
constexpr uint32_t HEX_MIC_OK_BDR  = 0x10B981;
constexpr uint32_t HEX_WARN        = 0xF59E0B;
constexpr uint32_t HEX_OK          = 0x10B981;

constexpr uint32_t SLIDER_THROTTLE_MS = 40;

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

ChannelUi g_channels[4] = {
    {"master",  "MASTER",  0x00D8FF},
    {"browser", "EDGE",    0x38BDF8},
    {"discord", "DISCORD", 0x818CF8},
    {"game",    "JOGO",    0x34D399},
};

lv_obj_t *g_conn_badge = nullptr;
lv_obj_t *g_conn_label = nullptr;
UiConnState g_conn_state = UI_CONN_WAITING;
lv_obj_t *g_wifi_badge = nullptr;
lv_obj_t *g_wifi_label = nullptr;
lv_obj_t *g_clock_badge = nullptr;
lv_obj_t *g_clock_label = nullptr;
char g_last_clock_text[16] = "--:--";
lv_obj_t *g_mic_btn = nullptr;
lv_obj_t *g_mic_dot = nullptr;
lv_obj_t *g_mic_label = nullptr;
lv_obj_t *g_status_label = nullptr;

bool g_mic_muted = false;
bool g_syncing_from_pc = false;

UiVolumeCallback g_vol_cb = nullptr;
UiMuteCallback g_mute_cb = nullptr;

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

void update_channel_visuals(ChannelUi &ch)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", ch.value);
    lv_label_set_text(ch.pct_label, buf);

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

void update_mic_visuals()
{
    if (!g_mic_btn || !g_mic_label || !g_mic_dot)
    {
        return;
    }

    if (g_mic_muted)
    {
        lv_obj_set_style_bg_color(g_mic_btn, lv_color_hex(HEX_MUTE_BG), 0);
        lv_obj_set_style_border_color(g_mic_btn, lv_color_hex(HEX_MUTE_BORDER), 0);
        lv_obj_set_style_bg_color(g_mic_dot, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(g_mic_label, "MICROFONE: MUTADO");
        lv_obj_set_style_text_color(g_mic_label, lv_color_hex(0xFFFFFF), 0);
    }
    else
    {
        lv_obj_set_style_bg_color(g_mic_btn, lv_color_hex(HEX_MIC_OK_BG), 0);
        lv_obj_set_style_border_color(g_mic_btn, lv_color_hex(HEX_MIC_OK_BDR), 0);
        lv_obj_set_style_bg_color(g_mic_dot, lv_color_hex(0x34D399), 0);
        lv_label_set_text(g_mic_label, "MICROFONE: ATIVO");
        lv_obj_set_style_text_color(g_mic_label, lv_color_hex(0xECFDF5), 0);
    }
}

void emit_volume_if_needed(ChannelUi &ch, bool force)
{
    const uint32_t now = millis();
    if (!force)
    {
        if (ch.value == ch.last_sent_value) return;
        if ((now - ch.last_send_ms) < SLIDER_THROTTLE_MS) return;
    }
    else
    {
        if (ch.value == ch.last_sent_value) return;
    }

    ch.last_sent_value = ch.value;
    ch.last_send_ms = now;

    if (g_vol_cb)
    {
        g_vol_cb(ch.target, ch.value);
    }
}

void slider_event_cb(lv_event_t *e)
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
        update_channel_visuals(*ch);

        char statusBuf[32];
        snprintf(statusBuf, sizeof(statusBuf), "%s: %d%%", lv_label_get_text(ch->name_label), ch->value);
        ui_set_status_text(statusBuf);

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

    char statusBuf[32];
    snprintf(statusBuf, sizeof(statusBuf), "%s: %s",
             lv_label_get_text(ch->name_label),
             ch->muted ? "Mute" : "Ativo");
    ui_set_status_text(statusBuf);

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
    ui_set_status_text(g_mic_muted ? "Mic: Mutado" : "Mic: Ativo");

    if (g_mute_cb)
    {
        g_mute_cb("mic");
    }
}

void build_header(lv_obj_t *scr)
{
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 8, 4);
    lv_obj_set_size(header, 464, 28);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Ponto de destaque ciano + Título (esquerda: 0..90)
    lv_obj_t *dot = lv_obj_create(header);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x00D8FF), 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 1, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "MINI DECK");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    // 2. Indicador de Wi-Fi + IP (centro-esquerda: 94..228)
    g_wifi_badge = lv_obj_create(header);
    lv_obj_remove_style_all(g_wifi_badge);
    lv_obj_set_pos(g_wifi_badge, 94, 3);
    lv_obj_set_size(g_wifi_badge, 134, 22);
    lv_obj_clear_flag(g_wifi_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(g_wifi_badge, 11, 0);
    lv_obj_set_style_bg_opa(g_wifi_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_wifi_badge, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(g_wifi_badge, 1, 0);
    lv_obj_set_style_border_color(g_wifi_badge, lv_color_hex(0x475569), 0);

    g_wifi_label = lv_label_create(g_wifi_badge);
    lv_label_set_text(g_wifi_label, LV_SYMBOL_WIFI " WiFi...");
    lv_label_set_long_mode(g_wifi_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(g_wifi_label, 126);
    lv_obj_set_style_text_align(g_wifi_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_wifi_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_wifi_label, lv_color_hex(HEX_WARN), 0);
    lv_obj_center(g_wifi_label);

    // 3. Relógio Digital NTP HH:MM (centro-direita: 232..294)
    g_clock_badge = lv_obj_create(header);
    lv_obj_remove_style_all(g_clock_badge);
    lv_obj_set_pos(g_clock_badge, 232, 3);
    lv_obj_set_size(g_clock_badge, 62, 22);
    lv_obj_clear_flag(g_clock_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(g_clock_badge, 11, 0);
    lv_obj_set_style_bg_opa(g_clock_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_clock_badge, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(g_clock_badge, 1, 0);
    lv_obj_set_style_border_color(g_clock_badge, lv_color_hex(HEX_CARD_BORDER), 0);

    g_clock_label = lv_label_create(g_clock_badge);
    lv_label_set_text(g_clock_label, "--:--");
    lv_obj_set_style_text_font(g_clock_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_clock_label, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_center(g_clock_label);

    // 4. Badge de conexão com o PC (direita: 298..464, comporta "PC CONECTADO (WIFI)")
    g_conn_badge = lv_obj_create(header);
    lv_obj_remove_style_all(g_conn_badge);
    lv_obj_set_size(g_conn_badge, 166, 22);
    lv_obj_set_style_radius(g_conn_badge, 11, 0);
    lv_obj_set_style_bg_opa(g_conn_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x2A2312), 0);
    lv_obj_set_style_border_width(g_conn_badge, 1, 0);
    lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(HEX_WARN), 0);
    lv_obj_align(g_conn_badge, LV_ALIGN_RIGHT_MID, 0, 0);

    g_conn_label = lv_label_create(g_conn_badge);
    lv_label_set_text(g_conn_label, "AGUARDANDO PC");
    lv_obj_set_style_text_font(g_conn_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0xFCD34D), 0);
    lv_obj_center(g_conn_label);
    g_conn_state = UI_CONN_WAITING;
}

void build_channel_card(lv_obj_t *scr, ChannelUi &ch, int16_t x)
{
    constexpr int16_t CARD_W = 110;
    constexpr int16_t CARD_H = 218;
    constexpr int16_t CARD_Y = 36;

    ch.card = lv_obj_create(scr);
    lv_obj_remove_style_all(ch.card);
    lv_obj_set_pos(ch.card, x, CARD_Y);
    lv_obj_set_size(ch.card, CARD_W, CARD_H);
    lv_obj_clear_flag(ch.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ch.card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ch.card, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(ch.card, 1, 0);
    lv_obj_set_style_border_color(ch.card, lv_color_hex(HEX_CARD_BORDER), 0);
    lv_obj_set_style_radius(ch.card, 12, 0);

    // Barra de cor superior
    lv_obj_t *accent = lv_obj_create(ch.card);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 48, 3);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(ch.accent_hex), 0);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);

    // Nome do canal
    ch.name_label = lv_label_create(ch.card);
    lv_label_set_text(ch.name_label, ch.default_name);
    lv_label_set_long_mode(ch.name_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(ch.name_label, CARD_W - 10);
    lv_obj_set_style_text_align(ch.name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ch.name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ch.name_label, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_align(ch.name_label, LV_ALIGN_TOP_MID, 0, 9);

    // Porcentagem
    ch.pct_label = lv_label_create(ch.card);
    lv_label_set_text(ch.pct_label, "100%");
    lv_obj_set_style_text_font(ch.pct_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ch.pct_label, lv_color_hex(ch.accent_hex), 0);
    lv_obj_align(ch.pct_label, LV_ALIGN_TOP_MID, 0, 27);

    // Slider vertical (0..100) com área de toque ampliada
    ch.slider = lv_slider_create(ch.card);
    lv_slider_set_range(ch.slider, 0, 100);
    lv_slider_set_value(ch.slider, ch.value, LV_ANIM_OFF);
    lv_obj_set_size(ch.slider, 16, 110);
    lv_obj_align(ch.slider, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_ext_click_area(ch.slider, 22);

    // Track (Main)
    lv_obj_set_style_bg_opa(ch.slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ch.slider, lv_color_hex(HEX_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(ch.slider, 8, LV_PART_MAIN);

    // Fill (Indicator)
    lv_obj_set_style_bg_opa(ch.slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ch.slider, lv_color_hex(ch.accent_hex), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ch.slider, 8, LV_PART_INDICATOR);

    // Knob grande para toque confortável
    lv_obj_set_style_bg_opa(ch.slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(ch.slider, lv_color_hex(HEX_TEXT_MAIN), LV_PART_KNOB);
    lv_obj_set_style_border_width(ch.slider, 3, LV_PART_KNOB);
    lv_obj_set_style_border_color(ch.slider, lv_color_hex(ch.accent_hex), LV_PART_KNOB);
    lv_obj_set_style_radius(ch.slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(ch.slider, 6, LV_PART_KNOB);

    lv_obj_add_event_cb(ch.slider, slider_event_cb, LV_EVENT_PRESSED, &ch);
    lv_obj_add_event_cb(ch.slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, &ch);
    lv_obj_add_event_cb(ch.slider, slider_event_cb, LV_EVENT_RELEASED, &ch);
    lv_obj_add_event_cb(ch.slider, slider_event_cb, LV_EVENT_PRESS_LOST, &ch);

    // Botão de Mute na base do cartão
    ch.mute_btn = lv_btn_create(ch.card);
    lv_obj_remove_style_all(ch.mute_btn);
    lv_obj_set_size(ch.mute_btn, 92, 30);
    lv_obj_align(ch.mute_btn, LV_ALIGN_BOTTOM_MID, 0, -9);
    lv_obj_set_style_radius(ch.mute_btn, 8, 0);
    lv_obj_set_style_bg_opa(ch.mute_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ch.mute_btn, 1, 0);
    lv_obj_set_style_bg_opa(ch.mute_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_add_event_cb(ch.mute_btn, channel_mute_event_cb, LV_EVENT_CLICKED, &ch);

    ch.mute_label = lv_label_create(ch.mute_btn);
    lv_obj_set_style_text_font(ch.mute_label, &lv_font_montserrat_12, 0);
    lv_obj_center(ch.mute_label);

    update_channel_visuals(ch);
}

void build_bottom_dock(lv_obj_t *scr)
{
    constexpr int16_t DOCK_Y = 262;
    constexpr int16_t DOCK_H = 50;

    // Botão dedicado do Microfone
    g_mic_btn = lv_btn_create(scr);
    lv_obj_remove_style_all(g_mic_btn);
    lv_obj_set_pos(g_mic_btn, 8, DOCK_Y);
    lv_obj_set_size(g_mic_btn, 296, DOCK_H);
    lv_obj_set_style_radius(g_mic_btn, 12, 0);
    lv_obj_set_style_bg_opa(g_mic_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_mic_btn, 2, 0);
    lv_obj_set_style_bg_opa(g_mic_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_mic_btn, mic_btn_event_cb, LV_EVENT_CLICKED, nullptr);

    g_mic_dot = lv_obj_create(g_mic_btn);
    lv_obj_remove_style_all(g_mic_dot);
    lv_obj_set_size(g_mic_dot, 12, 12);
    lv_obj_set_style_radius(g_mic_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(g_mic_dot, LV_OPA_COVER, 0);
    lv_obj_align(g_mic_dot, LV_ALIGN_LEFT_MID, 20, 0);

    g_mic_label = lv_label_create(g_mic_btn);
    lv_obj_set_style_text_font(g_mic_label, &lv_font_montserrat_16, 0);
    lv_obj_align(g_mic_label, LV_ALIGN_CENTER, 10, 0);

    update_mic_visuals();

    // Cartão de status ao vivo no canto inferior direito
    lv_obj_t *status_card = lv_obj_create(scr);
    lv_obj_remove_style_all(status_card);
    lv_obj_set_pos(status_card, 312, DOCK_Y);
    lv_obj_set_size(status_card, 160, DOCK_H);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(status_card, 12, 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(HEX_CARD), 0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_set_style_border_color(status_card, lv_color_hex(HEX_CARD_BORDER), 0);

    lv_obj_t *caption = lv_label_create(status_card);
    lv_label_set_text(caption, "ULTIMA ACAO");
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(0x64748B), 0);
    lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 6);

    g_status_label = lv_label_create(status_card);
    lv_label_set_text(g_status_label, "Pronto");
    lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(g_status_label, 146);
    lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(HEX_TEXT_MAIN), 0);
    lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_MID, 0, -7);
}

} // namespace

void ui_init(UiVolumeCallback on_volume_change, UiMuteCallback on_mute_toggle)
{
    g_vol_cb = on_volume_change;
    g_mute_cb = on_mute_toggle;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(HEX_BG), 0);

    build_header(scr);

    // 4 colunas simétricas (110px de largura, 8px de espaçamento)
    const int16_t colX[4] = {8, 126, 244, 362};
    for (int i = 0; i < 4; ++i)
    {
        build_channel_card(scr, g_channels[i], colX[i]);
    }

    build_bottom_dock(scr);
}

void ui_sync_volume(const char *target, int value)
{
    ChannelUi *ch = find_channel(target);
    if (!ch || !ch->slider) return;

    // Nunca sobrescreve o slider enquanto o usuário está arrastando com o dedo
    if (ch->dragging) return;

    const int clamped = clamp_vol(value);
    ch->value = clamped;
    ch->last_sent_value = clamped;

    g_syncing_from_pc = true;
    lv_slider_set_value(ch->slider, clamped, LV_ANIM_OFF);
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

void ui_set_conn_state(UiConnState state)
{
    if (!g_conn_badge || !g_conn_label) return;
    if (g_conn_state == state) return;
    g_conn_state = state;

    switch (state)
    {
    case UI_CONN_WIFI:
        lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x064E3B), 0);
        lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(HEX_OK), 0);
        lv_label_set_text(g_conn_label, "PC CONECTADO (WIFI)");
        lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0x34D399), 0);
        ui_set_status_text("Wi-Fi Online");
        break;

    case UI_CONN_SYNCED:
        lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x064E3B), 0);
        lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(HEX_OK), 0);
        lv_label_set_text(g_conn_label, "PC SINCRONIZADO");
        lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0x34D399), 0);
        ui_set_status_text("Sincronizado");
        break;

    case UI_CONN_DISCONNECTED:
        lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x450A0A), 0);
        lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(0xEF4444), 0);
        lv_label_set_text(g_conn_label, "DESCONECTADO");
        lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0xF87171), 0);
        ui_set_status_text("Sem sinal do PC");
        break;

    case UI_CONN_WAITING:
    default:
        lv_obj_set_style_bg_color(g_conn_badge, lv_color_hex(0x2A2312), 0);
        lv_obj_set_style_border_color(g_conn_badge, lv_color_hex(HEX_WARN), 0);
        lv_label_set_text(g_conn_label, "AGUARDANDO PC");
        lv_obj_set_style_text_color(g_conn_label, lv_color_hex(0xFCD34D), 0);
        ui_set_status_text("Aguardando PC");
        break;
    }
}

void ui_set_pc_connected(bool connected)
{
    ui_set_conn_state(connected ? UI_CONN_SYNCED : UI_CONN_DISCONNECTED);
}

void ui_set_status_text(const char *text)
{
    if (!g_status_label || !text) return;
    lv_label_set_text(g_status_label, text);
}

void ui_set_wifi_status(bool connected, const char *ip_str)
{
    if (!g_wifi_badge || !g_wifi_label) return;

    if (connected)
    {
        lv_obj_set_style_bg_color(g_wifi_badge, lv_color_hex(0x0B2942), 0);
        lv_obj_set_style_border_color(g_wifi_badge, lv_color_hex(0x38BDF8), 0);
        lv_obj_set_style_text_color(g_wifi_label, lv_color_hex(0x7DD3FC), 0);

        char buf[40];
        if (ip_str && ip_str[0] != '\0')
        {
            snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s", ip_str);
        }
        else
        {
            snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " WiFi: OK");
        }
        lv_label_set_text(g_wifi_label, buf);
    }
    else
    {
        lv_obj_set_style_bg_color(g_wifi_badge, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_border_color(g_wifi_badge, lv_color_hex(0x475569), 0);
        lv_obj_set_style_text_color(g_wifi_label, lv_color_hex(HEX_WARN), 0);
        lv_label_set_text(g_wifi_label, LV_SYMBOL_WIFI " WiFi...");
    }
}

void ui_set_clock_text(const char *hhmm)
{
    if (!g_clock_label || !hhmm || !hhmm[0]) return;
    if (strcmp(g_last_clock_text, hhmm) == 0) return;

    strncpy(g_last_clock_text, hhmm, sizeof(g_last_clock_text) - 1);
    g_last_clock_text[sizeof(g_last_clock_text) - 1] = '\0';
    lv_label_set_text(g_clock_label, g_last_clock_text);
}

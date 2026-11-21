#include <Arduino.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <time.h>

#include "pins_config.h"
#include "ui.h"

// ============================================================================
// Configuração Wi-Fi, WebSocket, Auto-Discovery UDP e NTP (Brasil GMT-3)
// ============================================================================
static constexpr const char *WIFI_SSID = "WIFI 2.4";
static constexpr const char *WIFI_PASS = "Pass718293";

static constexpr uint16_t WS_PORT = 81;
static constexpr uint16_t UDP_DISCOVERY_PORT = 4210;
static constexpr uint32_t UDP_BROADCAST_INTERVAL_MS = 2000;
static constexpr const char *UDP_DISCOVERY_PAYLOAD = "{\"device\":\"minideck\",\"port\":81}";

static constexpr long NTP_GMT_OFFSET_SEC = -3L * 3600L;
static constexpr int NTP_DAYLIGHT_OFFSET_SEC = 0;
static constexpr const char *NTP_SERVER_1 = "a.st1.ntp.br";
static constexpr const char *NTP_SERVER_2 = "pool.ntp.org";
static constexpr const char *NTP_SERVER_3 = "time.nist.gov";

// ============================================================================
// Display QSPI (AXS15231B) + Canvas em PSRAM (rotação paisagem 480x320)
// ============================================================================
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    QSPI_CS, QSPI_SCLK, QSPI_D0, QSPI_D1, QSPI_D2, QSPI_D3);

Arduino_GFX *panel = new Arduino_AXS15231B(
    bus,
    GFX_NOT_DEFINED,
    0,
    false,
    PANEL_NATIVE_WIDTH,
    PANEL_NATIVE_HEIGHT);

Arduino_Canvas *gfx = new Arduino_Canvas(
    PANEL_NATIVE_WIDTH,
    PANEL_NATIVE_HEIGHT,
    panel,
    0,
    0,
    DISPLAY_ROTATION);

// ============================================================================
// Servidores de Rede (WebSocket porta 81 + UDP Discovery porta 4210)
// ============================================================================
WebSocketsServer webSocket = WebSocketsServer(WS_PORT);
WiFiUDP g_udp_discovery;

// ============================================================================
// LVGL Buffers e Drivers
// ============================================================================
static constexpr uint16_t DISP_HOR_RES = 480;
static constexpr uint16_t DISP_VER_RES = 320;
static constexpr uint32_t DRAW_BUF_LINES = 160; // Meia tela em PSRAM (~153 KB)

static lv_disp_draw_buf_t g_draw_buf;
static lv_color_t *g_buf1 = nullptr;
static lv_disp_drv_t g_disp_drv;
static lv_indev_drv_t g_indev_drv;

// ============================================================================
// Estado de Touch, Serial, WebSocket, Wi-Fi e NTP
// ============================================================================
static constexpr uint32_t SERIAL_HEARTBEAT_TIMEOUT_MS = 3000;
static constexpr uint32_t WIFI_POLL_INTERVAL_MS = 500;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
static constexpr uint32_t CLOCK_POLL_UNSYNCED_MS = 1000;
static constexpr uint32_t CLOCK_POLL_SYNCED_MS = 10000;

static bool g_touch_ready = false;
static int16_t g_last_touch_x = 0;
static int16_t g_last_touch_y = 0;
static uint8_t g_touch_miss_count = 0;
static bool g_touch_pressed = false;

static String g_serial_rx;
static uint32_t g_last_serial_rx_ms = 0;
static bool g_serial_pc_connected = false;

static bool g_wifi_connected = false;
static bool g_ws_started = false;
static bool g_ws_client_connected = false;
static bool g_ntp_synced = false;

static uint32_t g_last_wifi_poll_ms = 0;
static uint32_t g_last_wifi_retry_ms = 0;
static uint32_t g_last_udp_bcast_ms = 0;
static uint32_t g_last_clock_poll_ms = 0;
static char g_current_time_str[16] = "--:--";

// ============================================================================
// Touch Capacitivo AXS15231B via I2C (0x3B)
// ============================================================================
static bool initTouch()
{
    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
    pinMode(TOUCH_INT, INPUT_PULLUP);

    Wire.beginTransmission(TOUCH_ADDR);
    return (Wire.endTransmission() == 0);
}

static bool readTouchRaw(int16_t &outX, int16_t &outY, bool &explicitRelease)
{
    explicitRelease = false;
    if (!g_touch_ready)
    {
        return false;
    }

    static const uint8_t readCmd[11] = {
        0xB5, 0xAB, 0xA5, 0x5A,
        0x00, 0x00,
        0x00, 0x08,
        0x00, 0x00, 0x00
    };

    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(readCmd, sizeof(readCmd));
    if (Wire.endTransmission() != 0)
    {
        return false;
    }

    const uint8_t received = Wire.requestFrom(static_cast<uint8_t>(TOUCH_ADDR), static_cast<uint8_t>(8));
    if (received < 8)
    {
        return false;
    }

    uint8_t data[8] = {0};
    for (uint8_t i = 0; i < 8; ++i)
    {
        data[i] = Wire.read();
    }

    const uint8_t gesture = data[0];
    const uint8_t numPoints = data[1];
    const uint8_t eventFlag = (data[2] >> 6) & 0x03; // 0=Down, 1=Up, 2=Contact

    if (eventFlag == 1)
    {
        explicitRelease = true;
        return false;
    }

    if (gesture != 0 || numPoints == 0 || numPoints > 5)
    {
        return false;
    }

    const int16_t rawX = ((data[2] & 0x0F) << 8) | data[3];
    const int16_t rawY = ((data[4] & 0x0F) << 8) | data[5];

    if (rawX < 0 || rawX >= PANEL_NATIVE_WIDTH || rawY < 0 || rawY >= PANEL_NATIVE_HEIGHT)
    {
        return false;
    }

    switch (DISPLAY_ROTATION)
    {
    case 1:
        outX = rawY;
        outY = (PANEL_NATIVE_WIDTH - 1) - rawX;
        break;
    case 2:
        outX = (PANEL_NATIVE_WIDTH - 1) - rawX;
        outY = (PANEL_NATIVE_HEIGHT - 1) - rawY;
        break;
    case 3:
        outX = (PANEL_NATIVE_HEIGHT - 1) - rawY;
        outY = rawX;
        break;
    default:
        outX = rawX;
        outY = rawY;
        break;
    }

    return true;
}

// ============================================================================
// Callbacks do LVGL (Display Flush & Touch Read)
// ============================================================================
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    const uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
    const uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);

    gfx->draw16bitRGBBitmap(
        area->x1,
        area->y1,
        reinterpret_cast<uint16_t *>(&color_p->full),
        w,
        h);

    if (lv_disp_flush_is_last(disp))
    {
        gfx->flush();
    }

    lv_disp_flush_ready(disp);
}

static void my_touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;

    int16_t tx = 0;
    int16_t ty = 0;
    bool explicitRelease = false;

    if (readTouchRaw(tx, ty, explicitRelease))
    {
        g_last_touch_x = tx;
        g_last_touch_y = ty;
        g_touch_pressed = true;
        g_touch_miss_count = 0;
    }
    else if (explicitRelease)
    {
        g_touch_pressed = false;
        g_touch_miss_count = 0;
    }
    else if (g_touch_pressed)
    {
        if (++g_touch_miss_count >= 2)
        {
            g_touch_pressed = false;
            g_touch_miss_count = 0;
        }
    }

    data->point.x = g_last_touch_x;
    data->point.y = g_last_touch_y;
    data->state = g_touch_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

// ============================================================================
// Estado Unificado de Conexão (Wi-Fi WebSocket tem prioridade visual sobre USB)
// ============================================================================
static void refreshConnectionBadge()
{
    if (g_ws_client_connected)
    {
        ui_set_conn_state(UI_CONN_WIFI);
    }
    else if (g_serial_pc_connected)
    {
        ui_set_conn_state(UI_CONN_SYNCED);
    }
    else
    {
        ui_set_conn_state(UI_CONN_WAITING);
    }
}

// ============================================================================
// Envio de Eventos JSON (ESP32 -> Serial + Todos os clientes WebSocket)
// ============================================================================
static void broadcastOutgoingEvent(const char *jsonPayload)
{
    Serial.println(jsonPayload);
    if (g_ws_started && webSocket.connectedClients() > 0)
    {
        webSocket.broadcastTXT(jsonPayload);
    }
}

static void sendVolumeEvent(const char *target, int value)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"event\":\"set_vol\",\"target\":\"%s\",\"val\":%d}", target, value);
    broadcastOutgoingEvent(buf);
}

static void sendMuteToggleEvent(const char *target)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"event\":\"toggle_mute\",\"target\":\"%s\"}", target);
    broadcastOutgoingEvent(buf);
}

static void sendMediaAction(const char *action)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"media\",\"action\":\"%s\"}", action);
    broadcastOutgoingEvent(buf);
}

static void sendScreenChange(const char *screenName)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"event\":\"screen\",\"screen\":\"%s\"}", screenName);
    broadcastOutgoingEvent(buf);
}

// ============================================================================
// Tratamento Compartilhado de Comandos JSON (WebSocket e Serial)
// ============================================================================
static void replyPong(bool fromWifi, uint8_t wsClientNum)
{
    static const char *PONG_JSON = "{\"event\":\"pong\"}";
    if (fromWifi)
    {
        webSocket.sendTXT(wsClientNum, PONG_JSON);
    }
    else
    {
        Serial.println(PONG_JSON);
    }
}

static void handleIncomingMessage(String line, bool fromWifi, uint8_t wsClientNum = 0)
{
    line.trim();
    if (line.isEmpty())
    {
        return;
    }

    if (line.equalsIgnoreCase("PING"))
    {
        if (!fromWifi)
        {
            g_last_serial_rx_ms = millis();
            g_serial_pc_connected = true;
            refreshConnectionBadge();
        }
        replyPong(fromWifi, wsClientNum);
        return;
    }

    if (line.equalsIgnoreCase("STATUS"))
    {
        const String ipStr = g_wifi_connected ? WiFi.localIP().toString() : String("");
        char statusBuf[256];
        snprintf(
            statusBuf,
            sizeof(statusBuf),
            "{\"event\":\"status\",\"display\":\"%dx%d\",\"touch\":%s,\"wifi\":%s,\"ip\":\"%s\","
            "\"ws_port\":%u,\"ws_clients\":%u,\"serial_pc\":%s,\"ntp\":%s,\"time\":\"%s\",\"psram_free\":%u}",
            gfx->width(),
            gfx->height(),
            g_touch_ready ? "true" : "false",
            g_wifi_connected ? "true" : "false",
            ipStr.c_str(),
            static_cast<unsigned>(WS_PORT),
            static_cast<unsigned>(g_ws_started ? webSocket.connectedClients() : 0),
            g_serial_pc_connected ? "true" : "false",
            g_ntp_synced ? "true" : "false",
            g_current_time_str,
            static_cast<unsigned>(ESP.getFreePsram()));

        if (fromWifi)
        {
            webSocket.sendTXT(wsClientNum, statusBuf);
        }
        else
        {
            Serial.println(statusBuf);
        }
        return;
    }

    if (line[0] != '{')
    {
        return;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, line);
    if (err)
    {
        const char *errJson = "{\"event\":\"error\",\"msg\":\"json_parse_failed\"}";
        if (fromWifi)
        {
            webSocket.sendTXT(wsClientNum, errJson);
        }
        else
        {
            Serial.println(errJson);
        }
        return;
    }

    const char *cmd = doc["cmd"] | "";
    const char *syncType = doc["sync"] | "";
    const char *target = doc["target"] | "";

    bool validMessage = false;

    if (strcmp(cmd, "ping") == 0 || strcmp(syncType, "ping") == 0)
    {
        validMessage = true;
    }
    else if (strcmp(syncType, "vol") == 0 && target[0] != '\0')
    {
        const int val = doc["val"] | 0;
        ui_sync_volume(target, val);
        validMessage = true;
    }
    else if (strcmp(syncType, "mute") == 0 && target[0] != '\0')
    {
        bool muted = false;
        if (doc["val"].is<bool>())
        {
            muted = doc["val"].as<bool>();
        }
        else
        {
            muted = (doc["val"] | 0) != 0;
        }
        ui_sync_mute(target, muted);
        validMessage = true;
    }
    else if (strcmp(syncType, "label") == 0 && target[0] != '\0')
    {
        const char *label = doc["val"] | "";
        if (label[0] != '\0')
        {
            ui_sync_label(target, label);
        }
        validMessage = true;
    }
    else if (strcmp(syncType, "media") == 0)
    {
        const char *title = doc["title"] | "Nenhuma mídia";
        const char *artist = doc["artist"] | "";
        bool playing = false;
        if (doc["playing"].is<bool>())
        {
            playing = doc["playing"].as<bool>();
        }
        else
        {
            playing = (doc["playing"] | 0) != 0;
        }
        ui_sync_media(title, artist, playing);
        validMessage = true;
    }
    else if (strcmp(syncType, "stats") == 0)
    {
        const int cpu = doc["cpu"] | 0;
        const int gpu = doc["gpu"] | 0;
        const int ramPct = doc["ram_pct"] | 0;
        const char *ramUsed = doc["ram_used"] | "0.0";
        const char *ramTotal = doc["ram_total"] | "0.0";
        const int diskPct = doc["disk_pct"] | -1;
        ui_sync_stats(cpu, gpu, ramPct, ramUsed, ramTotal, diskPct);
        validMessage = true;
    }

    if (validMessage)
    {
        if (fromWifi)
        {
            g_ws_client_connected = true;
        }
        else
        {
            g_last_serial_rx_ms = millis();
            g_serial_pc_connected = true;
        }
        refreshConnectionBadge();
        replyPong(fromWifi, wsClientNum);
    }
}

static void pollSerial()
{
    while (Serial.available() > 0)
    {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r')
        {
            continue;
        }
        if (c == '\n')
        {
            handleIncomingMessage(g_serial_rx, false, 0);
            g_serial_rx = "";
        }
        else if (g_serial_rx.length() < 384)
        {
            g_serial_rx += c;
        }
    }
}

static void checkSerialHeartbeatTimeout()
{
    if (g_serial_pc_connected && (millis() - g_last_serial_rx_ms) > SERIAL_HEARTBEAT_TIMEOUT_MS)
    {
        g_serial_pc_connected = false;
        refreshConnectionBadge();
    }
}

// ============================================================================
// Eventos do Servidor WebSocket (porta 81)
// ============================================================================
static void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_CONNECTED:
    {
        IPAddress clientIp = webSocket.remoteIP(num);
        g_ws_client_connected = true;
        refreshConnectionBadge();

        Serial.printf(
            "{\"event\":\"ws_connected\",\"client\":%u,\"ip\":\"%s\"}\n",
            static_cast<unsigned>(num),
            clientIp.toString().c_str());

        // Notifica cliente recém-conectado para disparar sincronização completa imediata
        char readyBuf[128];
        snprintf(
            readyBuf,
            sizeof(readyBuf),
            "{\"event\":\"ready\",\"transport\":\"wifi\",\"ip\":\"%s\",\"port\":%u}",
            WiFi.localIP().toString().c_str(),
            static_cast<unsigned>(WS_PORT));
        webSocket.sendTXT(num, readyBuf);
        break;
    }

    case WStype_DISCONNECTED:
        Serial.printf("{\"event\":\"ws_disconnected\",\"client\":%u}\n", static_cast<unsigned>(num));
        g_ws_client_connected = (webSocket.connectedClients() > 0);
        refreshConnectionBadge();
        break;

    case WStype_TEXT:
    {
        if (payload && length > 0)
        {
            String msg;
            msg.reserve(length + 1);
            for (size_t i = 0; i < length; ++i)
            {
                msg += static_cast<char>(payload[i]);
            }
            handleIncomingMessage(msg, true, num);
        }
        break;
    }

    default:
        break;
    }
}

static void startWebSocketServer()
{
    if (g_ws_started)
    {
        return;
    }
    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);
    // Heartbeat WebSocket nativo: ping a cada 3s, timeout 2s, 2 tentativas -> detecta queda limpa
    webSocket.enableHeartbeat(3000, 2000, 2);
    g_ws_started = true;
    Serial.printf("{\"event\":\"ws_server\",\"status\":\"listening\",\"port\":%u}\n", static_cast<unsigned>(WS_PORT));
}

// ============================================================================
// Broadcast UDP de Auto-Descoberta (porta 4210 a cada 2s)
// ============================================================================
static void pollUdpDiscovery()
{
    if (!g_wifi_connected)
    {
        return;
    }

    const uint32_t now = millis();
    if ((now - g_last_udp_bcast_ms) < UDP_BROADCAST_INTERVAL_MS)
    {
        return;
    }
    g_last_udp_bcast_ms = now;

    // Envia para o broadcast da sub-rede local (ex.: 192.168.100.255) e broadcast global
    const uint32_t ipRaw = static_cast<uint32_t>(WiFi.localIP());
    const uint32_t maskRaw = static_cast<uint32_t>(WiFi.subnetMask());
    const IPAddress subnetBroadcast(ipRaw | ~maskRaw);

    g_udp_discovery.beginPacket(subnetBroadcast, UDP_DISCOVERY_PORT);
    g_udp_discovery.write(
        reinterpret_cast<const uint8_t *>(UDP_DISCOVERY_PAYLOAD),
        strlen(UDP_DISCOVERY_PAYLOAD));
    g_udp_discovery.endPacket();

    g_udp_discovery.beginPacket(IPAddress(255, 255, 255, 255), UDP_DISCOVERY_PORT);
    g_udp_discovery.write(
        reinterpret_cast<const uint8_t *>(UDP_DISCOVERY_PAYLOAD),
        strlen(UDP_DISCOVERY_PAYLOAD));
    g_udp_discovery.endPacket();
}

// ============================================================================
// Wi-Fi em Segundo Plano (Não-Bloqueante) e Relógio NTP (GMT-3)
// ============================================================================
static void initWifiAsync()
{
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    g_last_wifi_retry_ms = millis();

    Serial.printf("{\"event\":\"wifi\",\"connected\":false,\"ssid\":\"%s\",\"msg\":\"connecting\"}\n", WIFI_SSID);
}

static void pollWifiAndClock()
{
    const uint32_t now = millis();

    if ((now - g_last_wifi_poll_ms) >= WIFI_POLL_INTERVAL_MS)
    {
        g_last_wifi_poll_ms = now;
        const bool isConnected = (WiFi.status() == WL_CONNECTED);

        if (isConnected && !g_wifi_connected)
        {
            g_wifi_connected = true;
            const String ipStr = WiFi.localIP().toString();
            ui_set_wifi_status(true, ipStr.c_str());

            configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
            startWebSocketServer();

            // Dispara o primeiro broadcast UDP imediatamente ao conectar no Wi-Fi
            g_last_udp_bcast_ms = now - UDP_BROADCAST_INTERVAL_MS;

            Serial.printf(
                "{\"event\":\"wifi\",\"connected\":true,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d}\n",
                WIFI_SSID,
                ipStr.c_str(),
                WiFi.RSSI());
        }
        else if (!isConnected && g_wifi_connected)
        {
            g_wifi_connected = false;
            g_ws_client_connected = false;
            ui_set_wifi_status(false, nullptr);
            refreshConnectionBadge();
            Serial.println("{\"event\":\"wifi\",\"connected\":false,\"msg\":\"disconnected\"}");
        }
        else if (!isConnected && (now - g_last_wifi_retry_ms) >= WIFI_RETRY_INTERVAL_MS)
        {
            g_last_wifi_retry_ms = now;
            WiFi.reconnect();
        }
    }

    const uint32_t clockInterval = g_ntp_synced ? CLOCK_POLL_SYNCED_MS : CLOCK_POLL_UNSYNCED_MS;
    if ((now - g_last_clock_poll_ms) >= clockInterval)
    {
        g_last_clock_poll_ms = now;
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0))
        {
            char hhmm[8];
            snprintf(hhmm, sizeof(hhmm), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            strncpy(g_current_time_str, hhmm, sizeof(g_current_time_str) - 1);
            g_current_time_str[sizeof(g_current_time_str) - 1] = '\0';

            ui_set_clock_text(g_current_time_str);

            if (!g_ntp_synced)
            {
                g_ntp_synced = true;
                Serial.printf(
                    "{\"event\":\"ntp\",\"synced\":true,\"time\":\"%02d:%02d:%02d\",\"date\":\"%04d-%02d-%02d\"}\n",
                    timeinfo.tm_hour,
                    timeinfo.tm_min,
                    timeinfo.tm_sec,
                    timeinfo.tm_year + 1900,
                    timeinfo.tm_mon + 1,
                    timeinfo.tm_mday);
            }
        }
    }
}

// ============================================================================
// Setup e Loop
// ============================================================================
void setup()
{
    Serial.begin(115200);

    const uint32_t waitStart = millis();
    while (!Serial && (millis() - waitStart) < 1200)
    {
        delay(10);
    }

    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    if (!gfx->begin())
    {
        Serial.println("{\"event\":\"error\",\"msg\":\"display_init_failed\"}");
        while (true)
        {
            delay(1000);
        }
    }

    g_touch_ready = initTouch();

    lv_init();

    const size_t bufPixels = static_cast<size_t>(DISP_HOR_RES) * DRAW_BUF_LINES;
    g_buf1 = static_cast<lv_color_t *>(
        heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_buf1)
    {
        g_buf1 = static_cast<lv_color_t *>(malloc(DISP_HOR_RES * 40 * sizeof(lv_color_t)));
        lv_disp_draw_buf_init(&g_draw_buf, g_buf1, nullptr, DISP_HOR_RES * 40);
    }
    else
    {
        lv_disp_draw_buf_init(&g_draw_buf, g_buf1, nullptr, bufPixels);
    }

    lv_disp_drv_init(&g_disp_drv);
    g_disp_drv.hor_res = DISP_HOR_RES;
    g_disp_drv.ver_res = DISP_VER_RES;
    g_disp_drv.flush_cb = my_disp_flush;
    g_disp_drv.draw_buf = &g_draw_buf;
    lv_disp_drv_register(&g_disp_drv);

    lv_indev_drv_init(&g_indev_drv);
    g_indev_drv.type = LV_INDEV_TYPE_POINTER;
    g_indev_drv.read_cb = my_touch_read;
    lv_indev_drv_register(&g_indev_drv);

    ui_init(sendVolumeEvent, sendMuteToggleEvent, sendMediaAction, sendScreenChange);
    lv_timer_handler();

    initWifiAsync();

    Serial.printf(
        "{\"event\":\"ready\",\"display\":\"%dx%d\",\"touch\":%s,\"ws_port\":%u,\"udp_port\":%u,\"psram_free\":%u}\n",
        gfx->width(),
        gfx->height(),
        g_touch_ready ? "true" : "false",
        static_cast<unsigned>(WS_PORT),
        static_cast<unsigned>(UDP_DISCOVERY_PORT),
        static_cast<unsigned>(ESP.getFreePsram()));
}

void loop()
{
    pollSerial();
    if (g_ws_started)
    {
        webSocket.loop();
    }
    checkSerialHeartbeatTimeout();
    pollWifiAndClock();
    pollUdpDiscovery();
    lv_timer_handler();
    delay(2);
}

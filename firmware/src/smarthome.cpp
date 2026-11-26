#include "smarthome.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef PIERRE_API_BASE_URL
#define PIERRE_API_BASE_URL ""
#endif

#ifndef PIERRE_API_TOKEN
#define PIERRE_API_TOKEN ""
#endif

namespace {

constexpr uint32_t PIERRE_POLL_INTERVAL_MS        = 30000; // Polling suave a cada 30s (com tela visível)
constexpr uint32_t PIERRE_ENTER_THROTTLE_MS       = 1500;
constexpr uint32_t PIERRE_POST_CMD_REFRESH_MS     = 2500;
constexpr uint32_t PIERRE_HTTP_CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t PIERRE_HTTP_READ_TIMEOUT_MS    = 8000;
constexpr size_t   PIERRE_TASK_STACK_BYTES        = 8192;
constexpr int      PIERRE_QUEUE_DEPTH             = 8;

enum PierreJobKind : uint8_t {
    JOB_GET_STATUS = 0,
    JOB_POST_CMD   = 1,
};

struct PierreJob {
    PierreJobKind kind;
    char endpoint[64];
    char payload[160];
    char feedback_ok[48];
};

struct PierreSharedSnapshot {
    bool dirty = false;
    bool online = false;
    bool busy = false;
    bool charging = false;
    int battery_pct = -1; // -1 = desconhecido
    UiPierreState state = UI_PIERRE_UNKNOWN;
    UiPierreSuction suction = UI_SUCTION_UNKNOWN;
    UiPierreCistern cistern = UI_CISTERN_UNKNOWN;
    char status_text[72] = "Status: Aguardando Wi-Fi...";
    char feedback_text[48] = "";
    bool feedback_is_error = false;
    bool feedback_changed = false;
    bool schedule_post_refresh = false;
};

QueueHandle_t     g_job_queue = nullptr;
SemaphoreHandle_t g_state_mtx = nullptr;
TaskHandle_t      g_task_hdl  = nullptr;

PierreSharedSnapshot g_shared;

volatile bool g_wifi_is_up = false;
bool g_screen_visible = false;
uint32_t g_last_poll_enqueued_ms = 0;
bool g_post_cmd_refresh_pending = false;
uint32_t g_post_cmd_refresh_at_ms = 0;

void copyStr(char *dst, size_t dstSize, const char *src)
{
    if (!dst || dstSize == 0) return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

UiPierreSuction parseSuctionLevel(const char *raw)
{
    if (!raw || raw[0] == '\0') return UI_SUCTION_UNKNOWN;
    if (strcasecmp(raw, "gentle") == 0 || strcasecmp(raw, "quiet") == 0) return UI_SUCTION_GENTLE;
    if (strcasecmp(raw, "normal") == 0) return UI_SUCTION_NORMAL;
    if (strcasecmp(raw, "strong") == 0 || strcasecmp(raw, "max") == 0) return UI_SUCTION_STRONG;
    return UI_SUCTION_UNKNOWN;
}

UiPierreCistern parseCisternLevel(const char *raw)
{
    if (!raw || raw[0] == '\0') return UI_CISTERN_UNKNOWN;
    if (strcasecmp(raw, "closed") == 0 || strcasecmp(raw, "off") == 0) return UI_CISTERN_CLOSED;
    if (strcasecmp(raw, "low") == 0) return UI_CISTERN_LOW;
    if (strcasecmp(raw, "middle") == 0 || strcasecmp(raw, "medium") == 0) return UI_CISTERN_MIDDLE;
    if (strcasecmp(raw, "high") == 0) return UI_CISTERN_HIGH;
    return UI_CISTERN_UNKNOWN;
}

void resolveEndpoint(const char *action, char *outPath, size_t outSize, PierreJobKind &outKind)
{
    if (!action || action[0] == '\0' || strcmp(action, "status") == 0)
    {
        copyStr(outPath, outSize, "/api/v1/vacuum/status");
        outKind = JOB_GET_STATUS;
        return;
    }
    if (action[0] == '/')
    {
        copyStr(outPath, outSize, action);
        outKind = (strstr(action, "/status") != nullptr) ? JOB_GET_STATUS : JOB_POST_CMD;
        return;
    }
    if (strcmp(action, "start") == 0)
    {
        copyStr(outPath, outSize, "/api/v1/vacuum/start");
        outKind = JOB_POST_CMD;
        return;
    }
    if (strcmp(action, "pause") == 0)
    {
        copyStr(outPath, outSize, "/api/v1/vacuum/pause");
        outKind = JOB_POST_CMD;
        return;
    }
    if (strcmp(action, "dock") == 0)
    {
        copyStr(outPath, outSize, "/api/v1/vacuum/dock");
        outKind = JOB_POST_CMD;
        return;
    }
    if (strcmp(action, "command") == 0)
    {
        copyStr(outPath, outSize, "/api/v1/vacuum/command");
        outKind = JOB_POST_CMD;
        return;
    }

    snprintf(outPath, outSize, "/api/v1/vacuum/%s", action);
    outKind = JOB_POST_CMD;
}

void classifyPierreStatus(
    const char *rawStatus,
    bool switchCharge,
    bool powerGo,
    bool paused,
    int faultCode,
    UiPierreState &outState,
    bool &outCharging,
    char *outText,
    size_t outTextSize)
{
    outCharging = false;
    outState = UI_PIERRE_IDLE;

    if (faultCode > 0 || (rawStatus && strcasecmp(rawStatus, "fault") == 0))
    {
        outState = UI_PIERRE_ERROR;
        copyStr(outText, outTextSize, "Status: Alerta - Verificar Pierre");
        return;
    }

    if (rawStatus && rawStatus[0] != '\0')
    {
        if (strcasecmp(rawStatus, "charging") == 0)
        {
            outState = UI_PIERRE_CHARGING;
            outCharging = true;
            copyStr(outText, outTextSize, "Status: Carregando na Base");
            return;
        }
        if (strcasecmp(rawStatus, "charge_done") == 0 ||
            strcasecmp(rawStatus, "chargecompleted") == 0 ||
            strcasecmp(rawStatus, "full") == 0)
        {
            outState = UI_PIERRE_CHARGING;
            outCharging = true;
            copyStr(outText, outTextSize, "Status: Carregado na Base");
            return;
        }
        if (strcasecmp(rawStatus, "goto_charge") == 0 ||
            strcasecmp(rawStatus, "back_charge") == 0 ||
            strcasecmp(rawStatus, "docking") == 0 ||
            strcasecmp(rawStatus, "returning") == 0)
        {
            outState = UI_PIERRE_DOCKING;
            outCharging = true;
            copyStr(outText, outTextSize, "Status: Voltando para a Base");
            return;
        }
        if (strcasecmp(rawStatus, "cleaning") == 0 ||
            strcasecmp(rawStatus, "smart") == 0 ||
            strcasecmp(rawStatus, "smart_clean") == 0 ||
            strcasecmp(rawStatus, "part") == 0 ||
            strcasecmp(rawStatus, "zone") == 0 ||
            strcasecmp(rawStatus, "pose") == 0 ||
            strcasecmp(rawStatus, "wall_follow") == 0 ||
            strcasecmp(rawStatus, "spiral") == 0 ||
            strcasecmp(rawStatus, "mop") == 0)
        {
            outState = UI_PIERRE_CLEANING;
            outCharging = false;
            copyStr(outText, outTextSize, "Status: Limpando...");
            return;
        }
        if (strcasecmp(rawStatus, "paused") == 0 || strcasecmp(rawStatus, "pause") == 0)
        {
            outState = UI_PIERRE_PAUSED;
            outCharging = false;
            copyStr(outText, outTextSize, "Status: Pausado");
            return;
        }
        if (strcasecmp(rawStatus, "standby") == 0 ||
            strcasecmp(rawStatus, "idle") == 0 ||
            strcasecmp(rawStatus, "sleep") == 0)
        {
            outState = UI_PIERRE_IDLE;
            outCharging = switchCharge;
            copyStr(outText, outTextSize, "Status: Em espera (Pronto)");
            return;
        }
    }

    if (paused)
    {
        outState = UI_PIERRE_PAUSED;
        copyStr(outText, outTextSize, "Status: Pausado");
    }
    else if (powerGo)
    {
        outState = UI_PIERRE_CLEANING;
        copyStr(outText, outTextSize, "Status: Limpando...");
    }
    else if (switchCharge)
    {
        outState = UI_PIERRE_CHARGING;
        outCharging = true;
        copyStr(outText, outTextSize, "Status: Carregando na Base");
    }
    else if (rawStatus && rawStatus[0] != '\0')
    {
        snprintf(outText, outTextSize, "Status: %s", rawStatus);
    }
    else
    {
        copyStr(outText, outTextSize, "Status: Conectado");
    }
}

void setBusyState(bool busy, const char *feedbackMsg, bool isError)
{
    if (!g_state_mtx) return;
    if (xSemaphoreTake(g_state_mtx, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        g_shared.busy = busy;
        if (feedbackMsg)
        {
            copyStr(g_shared.feedback_text, sizeof(g_shared.feedback_text), feedbackMsg);
            g_shared.feedback_is_error = isError;
            g_shared.feedback_changed = true;
        }
        g_shared.dirty = true;
        xSemaphoreGive(g_state_mtx);
    }
}

bool parseStatusJson(
    const String &body,
    int &outBattery,
    UiPierreState &outState,
    bool &outCharging,
    UiPierreSuction &outSuction,
    UiPierreCistern &outCistern,
    char *outStatusText,
    size_t outStatusTextSize,
    char *outRawStatus,
    size_t outRawStatusSize,
    char *outRawSuction,
    size_t outRawSuctionSize,
    char *outRawCistern,
    size_t outRawCisternSize)
{
    JsonDocument filter;
    filter["success"] = true;
    filter["electricity_left"] = true;
    filter["battery"] = true;
    filter["status"] = true;
    filter["state"] = true;
    filter["suction"] = true;
    filter["cistern"] = true;
    filter["result"][0]["code"] = true;
    filter["result"][0]["value"] = true;

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err)
    {
        Serial.printf("{\"event\":\"pierre_parse_error\",\"err\":\"%s\"}\n", err.c_str());
        return false;
    }

    int battery = -1;
    char rawStatus[32] = "";
    char rawSuction[24] = "";
    char rawCistern[24] = "";
    bool switchCharge = false;
    bool powerGo = false;
    bool paused = false;
    int faultCode = 0;

    // 1. Formato Tuya DP array: {"result":[{"code":"electricity_left","value":92}, ...], "success":true}
    JsonArray resultArr = doc["result"].as<JsonArray>();
    if (!resultArr.isNull())
    {
        for (JsonObject item : resultArr)
        {
            const char *code = item["code"] | "";
            if (strcmp(code, "electricity_left") == 0)
            {
                battery = item["value"] | -1;
            }
            else if (strcmp(code, "status") == 0)
            {
                copyStr(rawStatus, sizeof(rawStatus), item["value"] | "");
            }
            else if (strcmp(code, "mode") == 0 && rawStatus[0] == '\0')
            {
                copyStr(rawStatus, sizeof(rawStatus), item["value"] | "");
            }
            else if (strcmp(code, "suction") == 0)
            {
                copyStr(rawSuction, sizeof(rawSuction), item["value"] | "");
            }
            else if (strcmp(code, "cistern") == 0)
            {
                copyStr(rawCistern, sizeof(rawCistern), item["value"] | "");
            }
            else if (strcmp(code, "switch_charge") == 0)
            {
                switchCharge = item["value"] | false;
            }
            else if (strcmp(code, "power_go") == 0)
            {
                powerGo = item["value"] | false;
            }
            else if (strcmp(code, "pause") == 0)
            {
                paused = item["value"] | false;
            }
            else if (strcmp(code, "fault") == 0)
            {
                faultCode = item["value"] | 0;
            }
        }
    }

    // 2. Fallback formato plano
    if (battery < 0)
    {
        if (!doc["electricity_left"].isNull()) battery = doc["electricity_left"] | -1;
        else if (!doc["battery"].isNull()) battery = doc["battery"] | -1;
    }
    if (rawStatus[0] == '\0')
    {
        copyStr(rawStatus, sizeof(rawStatus), doc["status"] | (doc["state"] | ""));
    }
    if (rawSuction[0] == '\0')
    {
        copyStr(rawSuction, sizeof(rawSuction), doc["suction"] | "");
    }
    if (rawCistern[0] == '\0')
    {
        copyStr(rawCistern, sizeof(rawCistern), doc["cistern"] | "");
    }

    if (battery < 0 && rawStatus[0] == '\0' && rawSuction[0] == '\0' && rawCistern[0] == '\0')
    {
        return false;
    }

    outBattery = battery;
    outSuction = parseSuctionLevel(rawSuction);
    outCistern = parseCisternLevel(rawCistern);
    copyStr(outRawStatus, outRawStatusSize, rawStatus);
    copyStr(outRawSuction, outRawSuctionSize, rawSuction);
    copyStr(outRawCistern, outRawCisternSize, rawCistern);

    classifyPierreStatus(
        rawStatus,
        switchCharge,
        powerGo,
        paused,
        faultCode,
        outState,
        outCharging,
        outStatusText,
        outStatusTextSize);
    return true;
}

void executeHttpJob(const PierreJob &job)
{
    if (PIERRE_API_BASE_URL[0] == '\0' || PIERRE_API_TOKEN[0] == '\0')
    {
        Serial.println("{\"event\":\"pierre_error\",\"msg\":\"api_credentials_missing_in_secrets_h\"}");
        setBusyState(false, "API n\xC3\xA3o configurada", true);
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.printf("{\"event\":\"pierre_skip\",\"endpoint\":\"%s\",\"reason\":\"wifi_offline\"}\n", job.endpoint);
        if (xSemaphoreTake(g_state_mtx, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            g_shared.online = false;
            g_shared.busy = false;
            copyStr(g_shared.status_text, sizeof(g_shared.status_text), "Status: Sem conex\xC3\xA3o Wi-Fi");
            if (job.kind == JOB_POST_CMD)
            {
                copyStr(g_shared.feedback_text, sizeof(g_shared.feedback_text), "Sem Wi-Fi");
                g_shared.feedback_is_error = true;
                g_shared.feedback_changed = true;
            }
            g_shared.dirty = true;
            xSemaphoreGive(g_state_mtx);
        }
        return;
    }

    if (job.kind == JOB_POST_CMD)
    {
        setBusyState(true, "Enviando comando...", false);
    }

    char fullUrl[192];
    snprintf(fullUrl, sizeof(fullUrl), "%s%s", PIERRE_API_BASE_URL, job.endpoint);

    char authHeader[128];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", PIERRE_API_TOKEN);

    const uint32_t t0 = millis();
    Serial.printf(
        "{\"event\":\"pierre_req\",\"method\":\"%s\",\"endpoint\":\"%s\",\"payload\":%s}\n",
        (job.kind == JOB_GET_STATUS) ? "GET" : "POST",
        job.endpoint,
        (job.payload[0] != '\0') ? job.payload : "\"\"");

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(PIERRE_HTTP_READ_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(PIERRE_HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(PIERRE_HTTP_READ_TIMEOUT_MS);

    int httpCode = -1;
    String respBody;

    if (http.begin(client, fullUrl))
    {
        http.addHeader("Authorization", authHeader);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "application/json");

        if (job.kind == JOB_GET_STATUS)
        {
            httpCode = http.GET();
        }
        else
        {
            const char *bodyToSend = (job.payload[0] != '\0') ? job.payload : "{}";
            httpCode = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(bodyToSend)), strlen(bodyToSend));
        }

        if (httpCode > 0)
        {
            respBody = http.getString();
        }
        http.end();
    }

    const uint32_t elapsedMs = millis() - t0;

    if (job.kind == JOB_GET_STATUS)
    {
        if (httpCode >= 200 && httpCode < 300)
        {
            int battery = -1;
            UiPierreState pState = UI_PIERRE_IDLE;
            bool charging = false;
            UiPierreSuction suction = UI_SUCTION_UNKNOWN;
            UiPierreCistern cistern = UI_CISTERN_UNKNOWN;
            char statusText[72] = "";
            char rawStatus[32] = "";
            char rawSuction[24] = "";
            char rawCistern[24] = "";

            const bool parsed = parseStatusJson(
                respBody,
                battery,
                pState,
                charging,
                suction,
                cistern,
                statusText,
                sizeof(statusText),
                rawStatus,
                sizeof(rawStatus),
                rawSuction,
                sizeof(rawSuction),
                rawCistern,
                sizeof(rawCistern));

            Serial.printf(
                "{\"event\":\"pierre_status\",\"http\":%d,\"ms\":%u,\"parsed\":%s,\"battery\":%d,"
                "\"raw_status\":\"%s\",\"charging\":%s,\"suction\":\"%s\",\"cistern\":\"%s\"}\n",
                httpCode,
                static_cast<unsigned>(elapsedMs),
                parsed ? "true" : "false",
                battery,
                rawStatus,
                charging ? "true" : "false",
                rawSuction,
                rawCistern);

            if (parsed && xSemaphoreTake(g_state_mtx, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                g_shared.online = true;
                g_shared.busy = false;
                g_shared.battery_pct = battery;
                g_shared.charging = charging;
                g_shared.state = pState;
                if (suction != UI_SUCTION_UNKNOWN) g_shared.suction = suction;
                if (cistern != UI_CISTERN_UNKNOWN) g_shared.cistern = cistern;
                copyStr(g_shared.status_text, sizeof(g_shared.status_text), statusText);
                g_shared.dirty = true;
                xSemaphoreGive(g_state_mtx);
            }
            else if (!parsed)
            {
                setBusyState(false, "Resposta inv\xC3\xA1lida", true);
            }
        }
        else
        {
            Serial.printf(
                "{\"event\":\"pierre_status_err\",\"http\":%d,\"ms\":%u,\"err\":\"%s\"}\n",
                httpCode,
                static_cast<unsigned>(elapsedMs),
                HTTPClient::errorToString(httpCode).c_str());

            if (xSemaphoreTake(g_state_mtx, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                g_shared.online = false;
                g_shared.busy = false;
                copyStr(g_shared.status_text, sizeof(g_shared.status_text), "Status: API indispon\xC3\xADvel");
                g_shared.dirty = true;
                xSemaphoreGive(g_state_mtx);
            }
        }
        return;
    }

    // Resposta de comando POST (/start, /pause, /dock, /command)
    String preview = respBody;
    if (preview.length() > 160) preview = preview.substring(0, 160);
    preview.replace("\"", "'");
    preview.replace("\n", " ");
    preview.replace("\r", " ");

    Serial.printf(
        "{\"event\":\"pierre_resp\",\"endpoint\":\"%s\",\"http\":%d,\"ms\":%u,\"body\":\"%s\"}\n",
        job.endpoint,
        httpCode,
        static_cast<unsigned>(elapsedMs),
        preview.c_str());

    if (httpCode >= 200 && httpCode < 300)
    {
        if (xSemaphoreTake(g_state_mtx, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            g_shared.online = true;
            g_shared.busy = false;
            copyStr(
                g_shared.feedback_text,
                sizeof(g_shared.feedback_text),
                (job.feedback_ok[0] != '\0') ? job.feedback_ok : "Comando enviado!");
            g_shared.feedback_is_error = false;
            g_shared.feedback_changed = true;
            g_shared.schedule_post_refresh = true;
            g_shared.dirty = true;
            xSemaphoreGive(g_state_mtx);
        }
    }
    else
    {
        char errBuf[48];
        if (httpCode > 0) snprintf(errBuf, sizeof(errBuf), "Erro HTTP %d", httpCode);
        else snprintf(errBuf, sizeof(errBuf), "Falha SSL / Rede");
        setBusyState(false, errBuf, true);
    }
}

void smarthomeWorkerTask(void *param)
{
    (void)param;
    PierreJob job;
    for (;;)
    {
        if (xQueueReceive(g_job_queue, &job, pdMS_TO_TICKS(250)) == pdTRUE)
        {
            executeHttpJob(job);
        }
    }
}

void enqueueJob(const PierreJob &job)
{
    if (!g_job_queue) return;
    if (xQueueSend(g_job_queue, &job, 0) != pdTRUE)
    {
        Serial.printf("{\"event\":\"pierre_queue_full\",\"endpoint\":\"%s\"}\n", job.endpoint);
    }
}

} // namespace

// ============================================================================
// API Pública do Módulo Casa Inteligente (Robô Pierre)
// ============================================================================
void smarthome_init()
{
    if (g_task_hdl != nullptr) return;

    g_state_mtx = xSemaphoreCreateMutex();
    g_job_queue = xQueueCreate(PIERRE_QUEUE_DEPTH, sizeof(PierreJob));

    if (g_state_mtx && g_job_queue)
    {
        xTaskCreatePinnedToCore(
            smarthomeWorkerTask,
            "pierre_https",
            PIERRE_TASK_STACK_BYTES,
            nullptr,
            1,
            &g_task_hdl,
            0);
    }
}

void smarthome_on_wifi_changed(bool connected)
{
    g_wifi_is_up = connected;
    if (connected)
    {
        if (g_screen_visible)
        {
            g_last_poll_enqueued_ms = millis();
            smarthome_refresh_status();
        }
    }
    else
    {
        g_post_cmd_refresh_pending = false;
        if (g_state_mtx && xSemaphoreTake(g_state_mtx, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            g_shared.online = false;
            g_shared.busy = false;
            copyStr(g_shared.status_text, sizeof(g_shared.status_text), "Status: Aguardando Wi-Fi...");
            g_shared.dirty = true;
            xSemaphoreGive(g_state_mtx);
        }
    }
}

void smarthome_set_screen_visible(bool visible)
{
    const bool was_visible = g_screen_visible;
    g_screen_visible = visible;

    if (visible && !was_visible && g_wifi_is_up)
    {
        const uint32_t now = millis();
        if (g_last_poll_enqueued_ms == 0 || (now - g_last_poll_enqueued_ms) >= PIERRE_ENTER_THROTTLE_MS)
        {
            g_last_poll_enqueued_ms = now;
            ui_show_pierre_feedback("Atualizando...", false);
            smarthome_refresh_status();
        }
    }
    else if (!visible)
    {
        g_post_cmd_refresh_pending = false;
    }
}

void smarthome_poll()
{
    const uint32_t now = millis();

    // 1. Re-consulta suave após enviar comando (apenas se a tela continuar visível)
    if (g_wifi_is_up && g_screen_visible && g_post_cmd_refresh_pending && now >= g_post_cmd_refresh_at_ms)
    {
        g_post_cmd_refresh_pending = false;
        g_last_poll_enqueued_ms = now;
        smarthome_refresh_status();
    }

    // 2. Polling periódico suave a cada 30s SOMENTE enquanto screen_smarthome estiver visível
    if (g_wifi_is_up && g_screen_visible &&
        (g_last_poll_enqueued_ms == 0 || (now - g_last_poll_enqueued_ms) >= PIERRE_POLL_INTERVAL_MS))
    {
        g_last_poll_enqueued_ms = now;
        smarthome_refresh_status();
    }

    // 3. Aplica snapshot do Core 0 -> LVGL no Core 1 (100% thread-safe)
    if (!g_state_mtx) return;
    if (xSemaphoreTake(g_state_mtx, 0) == pdTRUE)
    {
        if (g_shared.dirty)
        {
            PierreSharedSnapshot snap = g_shared;
            g_shared.dirty = false;
            g_shared.feedback_changed = false;
            g_shared.schedule_post_refresh = false;
            xSemaphoreGive(g_state_mtx);

            if (snap.schedule_post_refresh && g_screen_visible)
            {
                g_post_cmd_refresh_pending = true;
                g_post_cmd_refresh_at_ms = now + PIERRE_POST_CMD_REFRESH_MS;
            }

            ui_sync_pierre_status(
                snap.online,
                snap.battery_pct,
                snap.charging,
                snap.state,
                snap.suction,
                snap.cistern,
                snap.status_text,
                snap.busy);

            if (snap.feedback_changed && snap.feedback_text[0] != '\0')
            {
                ui_show_pierre_feedback(snap.feedback_text, snap.feedback_is_error);
            }
            return;
        }
        xSemaphoreGive(g_state_mtx);
    }
}

void sendPierreCommand(const char *action, const char *jsonPayload)
{
    PierreJob job;
    memset(&job, 0, sizeof(job));
    resolveEndpoint(action, job.endpoint, sizeof(job.endpoint), job.kind);
    if (jsonPayload && jsonPayload[0] != '\0')
    {
        copyStr(job.payload, sizeof(job.payload), jsonPayload);
    }
    copyStr(job.feedback_ok, sizeof(job.feedback_ok), "Comando enviado!");
    enqueueJob(job);
}

void smarthome_refresh_status()
{
    PierreJob job;
    memset(&job, 0, sizeof(job));
    job.kind = JOB_GET_STATUS;
    copyStr(job.endpoint, sizeof(job.endpoint), "/api/v1/vacuum/status");
    enqueueJob(job);
}

void smarthome_start()
{
    PierreJob job;
    memset(&job, 0, sizeof(job));
    job.kind = JOB_POST_CMD;
    copyStr(job.endpoint, sizeof(job.endpoint), "/api/v1/vacuum/start");
    copyStr(job.payload, sizeof(job.payload), "{}");
    copyStr(job.feedback_ok, sizeof(job.feedback_ok), "Limpeza iniciada!");
    enqueueJob(job);
}

void smarthome_pause()
{
    PierreJob job;
    memset(&job, 0, sizeof(job));
    job.kind = JOB_POST_CMD;
    copyStr(job.endpoint, sizeof(job.endpoint), "/api/v1/vacuum/pause");
    copyStr(job.payload, sizeof(job.payload), "{}");
    copyStr(job.feedback_ok, sizeof(job.feedback_ok), "Pausa alternada!");
    enqueueJob(job);
}

void smarthome_dock()
{
    PierreJob job;
    memset(&job, 0, sizeof(job));
    job.kind = JOB_POST_CMD;
    copyStr(job.endpoint, sizeof(job.endpoint), "/api/v1/vacuum/dock");
    copyStr(job.payload, sizeof(job.payload), "{}");
    copyStr(job.feedback_ok, sizeof(job.feedback_ok), "Voltando p/ base!");
    enqueueJob(job);
}

void smarthome_set_suction(const char *level)
{
    if (!level || level[0] == '\0') return;
    PierreJob job;
    memset(&job, 0, sizeof(job));
    job.kind = JOB_POST_CMD;
    copyStr(job.endpoint, sizeof(job.endpoint), "/api/v1/vacuum/command");
    snprintf(job.payload, sizeof(job.payload), "{\"code\":\"suction\",\"value\":\"%s\"}", level);
    copyStr(job.feedback_ok, sizeof(job.feedback_ok), "Suc\xC3\xA7\xC3\xA3o atualizada!");
    enqueueJob(job);
}

void smarthome_set_cistern(const char *level)
{
    if (!level || level[0] == '\0') return;
    PierreJob job;
    memset(&job, 0, sizeof(job));
    job.kind = JOB_POST_CMD;
    copyStr(job.endpoint, sizeof(job.endpoint), "/api/v1/vacuum/command");
    snprintf(job.payload, sizeof(job.payload), "{\"code\":\"cistern\",\"value\":\"%s\"}", level);
    copyStr(job.feedback_ok, sizeof(job.feedback_ok), "Fluxo de \xC3\xA1gua OK!");
    enqueueJob(job);
}

void smarthome_seek()
{
    PierreJob job;
    memset(&job, 0, sizeof(job));
    job.kind = JOB_POST_CMD;
    copyStr(job.endpoint, sizeof(job.endpoint), "/api/v1/vacuum/command");
    copyStr(job.payload, sizeof(job.payload), "{\"code\":\"seek\",\"value\":true}");
    copyStr(job.feedback_ok, sizeof(job.feedback_ok), "Localizando Pierre!");
    enqueueJob(job);
}

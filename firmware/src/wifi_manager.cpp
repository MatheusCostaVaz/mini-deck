#include "wifi_manager.h"

#include <Preferences.h>
#include <WiFi.h>
#include <string.h>

#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif

#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS ""
#endif

namespace {

constexpr const char *NVS_NAMESPACE = "minideck";
constexpr uint32_t WIFI_POLL_INTERVAL_MS      = 500;
constexpr uint32_t WIFI_RSSI_POLL_INTERVAL_MS = 5000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS    = 12000;
constexpr uint32_t WIFI_RESCAN_INTERVAL_MS    = 25000;

Preferences g_prefs;
WifiStateChangeCb g_state_cb = nullptr;

SavedNetwork g_saved[WIFI_MGR_MAX_SAVED];
int g_saved_count = 0;

String g_target_ssid;
String g_target_pass;
String g_last_good_ssid;
String g_last_good_pass;
String g_pending_ssid;
String g_pending_pass;

bool g_connected = false;
bool g_trying_new = false;
bool g_restoring = false;
bool g_manual_scanning = false;
bool g_auto_scanning = false;

uint32_t g_last_poll_ms = 0;
uint32_t g_last_rssi_ms = 0;
uint32_t g_last_rescan_ms = 0;
uint32_t g_attempt_start_ms = 0;
int g_last_countdown_sec = -1;

int find_saved_index(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') return -1;
    for (int i = 0; i < g_saved_count; ++i)
    {
        if (strcmp(g_saved[i].ssid, ssid) == 0)
        {
            return i;
        }
    }
    return -1;
}

void persist_saved_networks()
{
    g_prefs.begin(NVS_NAMESPACE, false);
    g_prefs.putUChar("w_cnt", static_cast<uint8_t>(g_saved_count));
    for (int i = 0; i < WIFI_MGR_MAX_SAVED; ++i)
    {
        char ks[8];
        char kp[8];
        snprintf(ks, sizeof(ks), "w_s%d", i);
        snprintf(kp, sizeof(kp), "w_p%d", i);
        if (i < g_saved_count)
        {
            g_prefs.putString(ks, g_saved[i].ssid);
            g_prefs.putString(kp, g_saved[i].pass);
        }
        else
        {
            g_prefs.remove(ks);
            g_prefs.remove(kp);
        }
    }
    g_prefs.end();
}

void add_network_in_memory(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') return;
    const char *safePass = pass ? pass : "";

    const int existing = find_saved_index(ssid);
    if (existing >= 0)
    {
        // Atualiza senha e move para o final (mais recente)
        SavedNetwork updated;
        strncpy(updated.ssid, ssid, sizeof(updated.ssid) - 1);
        updated.ssid[sizeof(updated.ssid) - 1] = '\0';
        strncpy(updated.pass, safePass, sizeof(updated.pass) - 1);
        updated.pass[sizeof(updated.pass) - 1] = '\0';

        for (int i = existing; i < g_saved_count - 1; ++i)
        {
            g_saved[i] = g_saved[i + 1];
        }
        g_saved[g_saved_count - 1] = updated;
        return;
    }

    if (g_saved_count < WIFI_MGR_MAX_SAVED)
    {
        strncpy(g_saved[g_saved_count].ssid, ssid, sizeof(g_saved[g_saved_count].ssid) - 1);
        g_saved[g_saved_count].ssid[sizeof(g_saved[g_saved_count].ssid) - 1] = '\0';
        strncpy(g_saved[g_saved_count].pass, safePass, sizeof(g_saved[g_saved_count].pass) - 1);
        g_saved[g_saved_count].pass[sizeof(g_saved[g_saved_count].pass) - 1] = '\0';
        g_saved_count++;
    }
    else
    {
        // Remove a mais antiga (índice 0) e grava a nova no final
        for (int i = 0; i < WIFI_MGR_MAX_SAVED - 1; ++i)
        {
            g_saved[i] = g_saved[i + 1];
        }
        const int last = WIFI_MGR_MAX_SAVED - 1;
        strncpy(g_saved[last].ssid, ssid, sizeof(g_saved[last].ssid) - 1);
        g_saved[last].ssid[sizeof(g_saved[last].ssid) - 1] = '\0';
        strncpy(g_saved[last].pass, safePass, sizeof(g_saved[last].pass) - 1);
        g_saved[last].pass[sizeof(g_saved[last].pass) - 1] = '\0';
    }
}

void upsert_saved_network(const char *ssid, const char *pass)
{
    add_network_in_memory(ssid, pass);
    persist_saved_networks();
}

void load_saved_networks()
{
    g_saved_count = 0;
    memset(g_saved, 0, sizeof(g_saved));

    g_prefs.begin(NVS_NAMESPACE, true);
    const uint8_t storedCount = g_prefs.getUChar("w_cnt", 0);
    const int limit = (storedCount > WIFI_MGR_MAX_SAVED) ? WIFI_MGR_MAX_SAVED : storedCount;
    for (int i = 0; i < limit; ++i)
    {
        char ks[8];
        char kp[8];
        snprintf(ks, sizeof(ks), "w_s%d", i);
        snprintf(kp, sizeof(kp), "w_p%d", i);
        String s = g_prefs.getString(ks, "");
        String p = g_prefs.getString(kp, "");
        if (!s.isEmpty())
        {
            add_network_in_memory(s.c_str(), p.c_str());
        }
    }

    // Migração transparente da chave única antiga ("ssid"/"pass") para a lista Multi-Wi-Fi
    String legacySsid;
    String legacyPass;
    if (g_saved_count == 0)
    {
        legacySsid = g_prefs.getString("ssid", "");
        legacyPass = g_prefs.getString("pass", "");
    }
    g_prefs.end();

    bool needsPersist = false;
    if (!legacySsid.isEmpty())
    {
        add_network_in_memory(legacySsid.c_str(), legacyPass.c_str());
        needsPersist = true;
    }

    // Fallback opcional definido localmente em secrets.h (ignorado no Git)
    if (DEFAULT_WIFI_SSID[0] != '\0' && find_saved_index(DEFAULT_WIFI_SSID) < 0)
    {
        add_network_in_memory(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
        needsPersist = true;
    }

    if (needsPersist && g_saved_count > 0)
    {
        persist_saved_networks();
    }

    Serial.printf("{\"event\":\"wifi_nvs\",\"saved_networks\":%d}\n", g_saved_count);
}

bool select_best_from_scan_results(int16_t found, int &outSavedIdx, int32_t &outBestRssi)
{
    outSavedIdx = -1;
    outBestRssi = -999;

    for (int16_t i = 0; i < found; ++i)
    {
        String ssid = WiFi.SSID(i);
        ssid.trim();
        if (ssid.isEmpty()) continue;

        const int idx = find_saved_index(ssid.c_str());
        if (idx >= 0)
        {
            const int32_t rssi = WiFi.RSSI(i);
            if (rssi > outBestRssi)
            {
                outBestRssi = rssi;
                outSavedIdx = idx;
            }
        }
    }
    return (outSavedIdx >= 0);
}

void begin_station_connect(const char *ssid, const char *pass)
{
    g_target_ssid = ssid ? ssid : "";
    g_target_pass = pass ? pass : "";

    WiFi.disconnect(false);
    delay(40);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_target_ssid.c_str(), g_target_pass.c_str());
    WiFi.setSleep(false);

    const uint32_t now = millis();
    g_last_poll_ms = now;
    g_last_rescan_ms = now;
}

void handle_scan_results_for_manual_ui(int16_t found)
{
    if (found == WIFI_SCAN_FAILED || found < 0)
    {
        WiFi.scanDelete();
        ui_set_wifi_scan_status("Falha ao escanear");
        Serial.println("{\"event\":\"wifi_scan\",\"status\":\"failed\"}");
        return;
    }

    static constexpr int MAX_NETS = 12;
    UiWifiNetwork nets[MAX_NETS];
    int count = 0;

    for (int16_t i = 0; i < found; ++i)
    {
        String ssid = WiFi.SSID(i);
        ssid.trim();
        if (ssid.isEmpty()) continue;

        const int32_t rssi = WiFi.RSSI(i);
        const bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

        int existingIdx = -1;
        for (int j = 0; j < count; ++j)
        {
            if (strcmp(nets[j].ssid, ssid.c_str()) == 0)
            {
                existingIdx = j;
                break;
            }
        }

        if (existingIdx >= 0)
        {
            if (rssi > nets[existingIdx].rssi)
            {
                nets[existingIdx].rssi = rssi;
                nets[existingIdx].secured = secured;
            }
            continue;
        }

        if (count < MAX_NETS)
        {
            strncpy(nets[count].ssid, ssid.c_str(), sizeof(nets[count].ssid) - 1);
            nets[count].ssid[sizeof(nets[count].ssid) - 1] = '\0';
            nets[count].rssi = rssi;
            nets[count].secured = secured;
            ++count;
        }
    }

    for (int i = 0; i < count; ++i)
    {
        for (int j = i + 1; j < count; ++j)
        {
            if (nets[j].rssi > nets[i].rssi)
            {
                UiWifiNetwork tmp = nets[i];
                nets[i] = nets[j];
                nets[j] = tmp;
            }
        }
    }

    WiFi.scanDelete();
    ui_set_wifi_scan_results(nets, count);
    Serial.printf("{\"event\":\"wifi_scan\",\"status\":\"done\",\"count\":%d}\n", count);
}

void restore_previous_or_rescan(const char *reason)
{
    g_trying_new = false;
    g_pending_ssid = "";
    g_pending_pass = "";
    g_last_countdown_sec = -1;

    g_connected = false;
    if (g_state_cb)
    {
        g_state_cb(false, nullptr, nullptr, 0);
    }

    if (!g_last_good_ssid.isEmpty())
    {
        g_restoring = true;
        ui_set_wifi_status(false, nullptr, g_last_good_ssid.c_str(), 0);
        ui_set_wifi_scan_status("Falha ao conectar. Restaurando rede anterior...");
        begin_station_connect(g_last_good_ssid.c_str(), g_last_good_pass.c_str());

        Serial.printf(
            "{\"event\":\"wifi\",\"connected\":false,\"ssid\":\"%s\",\"msg\":\"fallback_restore\",\"reason\":\"%s\"}\n",
            g_last_good_ssid.c_str(),
            reason ? reason : "timeout");
    }
    else
    {
        g_restoring = false;
        ui_set_wifi_status(false, nullptr, "Desconectado", 0);
        ui_set_wifi_scan_status("Falha ao conectar. Verifique a senha.");
        WiFi.disconnect(false);
        g_last_rescan_ms = millis();

        Serial.printf(
            "{\"event\":\"wifi\",\"connected\":false,\"msg\":\"connect_failed\",\"reason\":\"%s\"}\n",
            reason ? reason : "timeout");
    }
}

void poll_active_scans()
{
    if (!g_manual_scanning && !g_auto_scanning)
    {
        return;
    }

    const int16_t found = WiFi.scanComplete();
    if (found == WIFI_SCAN_RUNNING)
    {
        return;
    }

    if (g_manual_scanning)
    {
        g_manual_scanning = false;
        g_auto_scanning = false;
        handle_scan_results_for_manual_ui(found);
        return;
    }

    // Resultado de auto-scan em segundo plano (busca a rede salva com melhor RSSI)
    g_auto_scanning = false;
    if (found > 0 && !g_connected && !g_trying_new)
    {
        int bestIdx = -1;
        int32_t bestRssi = -999;
        if (select_best_from_scan_results(found, bestIdx, bestRssi))
        {
            const SavedNetwork &net = g_saved[bestIdx];
            WiFi.scanDelete();
            ui_set_wifi_status(false, nullptr, net.ssid, 0);
            begin_station_connect(net.ssid, net.pass);
            Serial.printf(
                "{\"event\":\"wifi\",\"connected\":false,\"ssid\":\"%s\",\"rssi\":%ld,\"msg\":\"auto_select\"}\n",
                net.ssid,
                static_cast<long>(bestRssi));
            return;
        }
    }
    WiFi.scanDelete();
}

} // namespace

void wifi_mgr_init(WifiStateChangeCb on_state_change)
{
    g_state_cb = on_state_change;
    load_saved_networks();

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);

    if (g_saved_count == 0)
    {
        ui_set_wifi_status(false, nullptr, "Desconectado", 0);
        ui_set_wifi_scan_status("Nenhuma rede salva. Escaneie abaixo.");
        Serial.println("{\"event\":\"wifi\",\"connected\":false,\"msg\":\"no_saved_networks\"}");
        return;
    }

    // Scan rápido inicial das redes 2.4 GHz para escolher automaticamente a salva com melhor RSSI
    ui_set_wifi_status(false, nullptr, "Buscando redes...", 0);
    const int16_t found = WiFi.scanNetworks(false, false, false, 150);

    int bestIdx = -1;
    int32_t bestRssi = -999;
    if (found > 0 && select_best_from_scan_results(found, bestIdx, bestRssi))
    {
        const SavedNetwork &best = g_saved[bestIdx];
        g_last_good_ssid = best.ssid;
        g_last_good_pass = best.pass;
        WiFi.scanDelete();

        ui_set_wifi_status(false, nullptr, best.ssid, 0);
        begin_station_connect(best.ssid, best.pass);

        Serial.printf(
            "{\"event\":\"wifi\",\"connected\":false,\"ssid\":\"%s\",\"rssi\":%ld,\"saved_count\":%d,\"msg\":\"auto_select_boot\"}\n",
            best.ssid,
            static_cast<long>(bestRssi),
            g_saved_count);
    }
    else
    {
        WiFi.scanDelete();
        ui_set_wifi_status(false, nullptr, "Desconectado", 0);
        ui_set_wifi_scan_status("Rede salva fora de alcance. Escaneie abaixo.");
        g_last_rescan_ms = millis();
        Serial.printf(
            "{\"event\":\"wifi\",\"connected\":false,\"saved_count\":%d,\"msg\":\"no_matching_saved_visible\"}\n",
            g_saved_count);
    }
}

void wifi_mgr_start_manual_scan()
{
    WiFi.scanDelete();
    g_auto_scanning = false;
    g_manual_scanning = true;
    ui_set_wifi_scan_status("Escaneando redes...");

    const int16_t res = WiFi.scanNetworks(true, false);
    if (res == WIFI_SCAN_FAILED)
    {
        g_manual_scanning = false;
        ui_set_wifi_scan_status("Falha ao iniciar scan");
        Serial.println("{\"event\":\"wifi_scan\",\"status\":\"failed\"}");
        return;
    }
    Serial.println("{\"event\":\"wifi_scan\",\"status\":\"started\"}");
}

void wifi_mgr_connect_new(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') return;
    const char *safePass = pass ? pass : "";

    if (g_manual_scanning || g_auto_scanning)
    {
        WiFi.scanDelete();
        g_manual_scanning = false;
        g_auto_scanning = false;
    }

    g_pending_ssid = ssid;
    g_pending_pass = safePass;
    g_trying_new = true;
    g_restoring = false;
    g_attempt_start_ms = millis();
    g_last_countdown_sec = static_cast<int>(WIFI_CONNECT_TIMEOUT_MS / 1000);

    g_connected = false;
    if (g_state_cb)
    {
        g_state_cb(false, nullptr, nullptr, 0);
    }

    ui_set_wifi_status(false, nullptr, g_pending_ssid.c_str(), 0);
    char statusBuf[96];
    snprintf(
        statusBuf,
        sizeof(statusBuf),
        "Conectando a %s... (Tentativa de %ds)",
        g_pending_ssid.c_str(),
        g_last_countdown_sec);
    ui_set_wifi_scan_status(statusBuf);

    begin_station_connect(g_pending_ssid.c_str(), g_pending_pass.c_str());

    Serial.printf(
        "{\"event\":\"wifi\",\"connected\":false,\"ssid\":\"%s\",\"msg\":\"trying_new\",\"timeout_s\":%d}\n",
        g_pending_ssid.c_str(),
        g_last_countdown_sec);
}

void wifi_mgr_poll()
{
    poll_active_scans();

    const uint32_t now = millis();
    if ((now - g_last_poll_ms) < WIFI_POLL_INTERVAL_MS)
    {
        return;
    }
    g_last_poll_ms = now;

    const wl_status_t status = WiFi.status();
    const bool isConnected = (status == WL_CONNECTED);

    if (g_trying_new)
    {
        const uint32_t elapsed = now - g_attempt_start_ms;
        if (isConnected)
        {
            upsert_saved_network(g_pending_ssid.c_str(), g_pending_pass.c_str());
            g_last_good_ssid = g_pending_ssid;
            g_last_good_pass = g_pending_pass;

            g_trying_new = false;
            g_restoring = false;
            g_pending_ssid = "";
            g_pending_pass = "";
            g_last_countdown_sec = -1;

            g_connected = true;
            WiFi.setSleep(false);
            const String ipStr = WiFi.localIP().toString();
            const String activeSsid = WiFi.SSID().length() > 0 ? WiFi.SSID() : g_last_good_ssid;
            const int32_t rssi = WiFi.RSSI();

            ui_set_wifi_status(true, ipStr.c_str(), activeSsid.c_str(), rssi);
            ui_set_wifi_scan_status("Conectado! Rede salva na memoria.");
            g_last_rssi_ms = now;

            if (g_state_cb)
            {
                g_state_cb(true, activeSsid.c_str(), ipStr.c_str(), rssi);
            }

            Serial.printf(
                "{\"event\":\"wifi\",\"connected\":true,\"saved\":true,\"saved_total\":%d,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%ld}\n",
                g_saved_count,
                activeSsid.c_str(),
                ipStr.c_str(),
                static_cast<long>(rssi));
        }
        else if (elapsed >= WIFI_CONNECT_TIMEOUT_MS ||
                 (status == WL_CONNECT_FAILED && elapsed >= 3000))
        {
            restore_previous_or_rescan(elapsed >= WIFI_CONNECT_TIMEOUT_MS ? "timeout_12s" : "auth_failed");
        }
        else
        {
            const int remSec = static_cast<int>((WIFI_CONNECT_TIMEOUT_MS - elapsed + 999) / 1000);
            if (remSec != g_last_countdown_sec)
            {
                g_last_countdown_sec = remSec;
                char statusBuf[96];
                snprintf(
                    statusBuf,
                    sizeof(statusBuf),
                    "Conectando a %s... (%ds)",
                    g_pending_ssid.c_str(),
                    remSec);
                ui_set_wifi_scan_status(statusBuf);
            }
        }
        return;
    }

    if (isConnected && !g_connected)
    {
        g_connected = true;
        WiFi.setSleep(false);
        const String ipStr = WiFi.localIP().toString();
        const String activeSsid = WiFi.SSID().length() > 0 ? WiFi.SSID() : g_target_ssid;
        const int32_t rssi = WiFi.RSSI();
        g_last_good_ssid = activeSsid;
        g_last_good_pass = g_target_pass;
        g_last_rssi_ms = now;

        ui_set_wifi_status(true, ipStr.c_str(), activeSsid.c_str(), rssi);
        if (g_restoring)
        {
            g_restoring = false;
            char restoredBuf[64];
            snprintf(restoredBuf, sizeof(restoredBuf), "Rede anterior restaurada: %s", activeSsid.c_str());
            ui_set_wifi_scan_status(restoredBuf);
        }
        else
        {
            ui_set_wifi_scan_status("Conectado!");
        }

        if (g_state_cb)
        {
            g_state_cb(true, activeSsid.c_str(), ipStr.c_str(), rssi);
        }

        Serial.printf(
            "{\"event\":\"wifi\",\"connected\":true,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%ld}\n",
            activeSsid.c_str(),
            ipStr.c_str(),
            static_cast<long>(rssi));
    }
    else if (isConnected && g_connected && (now - g_last_rssi_ms) >= WIFI_RSSI_POLL_INTERVAL_MS)
    {
        g_last_rssi_ms = now;
        const String ipStr = WiFi.localIP().toString();
        const String activeSsid = WiFi.SSID().length() > 0 ? WiFi.SSID() : g_target_ssid;
        ui_set_wifi_status(true, ipStr.c_str(), activeSsid.c_str(), WiFi.RSSI());
    }
    else if (!isConnected && g_connected)
    {
        g_connected = false;
        g_last_rescan_ms = now;
        ui_set_wifi_status(false, nullptr, "Desconectado", 0);
        if (g_state_cb)
        {
            g_state_cb(false, nullptr, nullptr, 0);
        }
        Serial.println("{\"event\":\"wifi\",\"connected\":false,\"msg\":\"disconnected\"}");
    }
    else if (!isConnected && !g_manual_scanning && !g_auto_scanning && g_saved_count > 0 &&
             (now - g_last_rescan_ms) >= WIFI_RESCAN_INTERVAL_MS)
    {
        // Re-escaneia assincronamente em segundo plano para conectar se alguma rede salva aparecer
        g_last_rescan_ms = now;
        g_auto_scanning = true;
        if (WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED)
        {
            g_auto_scanning = false;
        }
    }
}

bool wifi_mgr_is_connected()
{
    return g_connected;
}

const char *wifi_mgr_active_ssid()
{
    return g_last_good_ssid.c_str();
}

int wifi_mgr_saved_count()
{
    return g_saved_count;
}

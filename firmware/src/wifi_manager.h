#pragma once

#include <Arduino.h>
#include <stdint.h>

constexpr int WIFI_MGR_MAX_SAVED = 4;

struct SavedNetwork {
    char ssid[33];
    char pass[65];
};

using WifiStateChangeCb = void (*)(bool connected, const char *ssid, const char *ip_str, int32_t rssi);

// Inicializa NVS Multi-Wi-Fi (até 4 redes), escaneia redes 2.4 GHz e conecta na salva com melhor RSSI
void wifi_mgr_init(WifiStateChangeCb on_state_change);

// Loop não-bloqueante: monitora conexão, timeout/fallback 12s, RSSI periódico e scans
void wifi_mgr_poll();

// Dispara scan assíncrono manual solicitado pela tela screen_wifi_manager
void wifi_mgr_start_manual_scan();

// Tenta conectar em nova rede com timeout seguro de 12s e salva/atualiza na NVS se conectar
void wifi_mgr_connect_new(const char *ssid, const char *pass);

bool wifi_mgr_is_connected();
const char *wifi_mgr_active_ssid();
int wifi_mgr_saved_count();

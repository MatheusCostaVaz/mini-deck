#pragma once

#include <stdint.h>

// ============================================================================
// Módulo Casa Inteligente — Robô Aspirador Pierre (KaBuM! Smart 800 Pro)
// Task FreeRTOS dedicada no Core 0 (HTTPS + JSON) sincronizada por fila +
// mutex com a UI LVGL no Core 1 (zero microtravamentos no toque).
// Polling automático de 30s ativo apenas enquanto screen_smarthome estiver visível.
// ============================================================================

void smarthome_init();
void smarthome_poll(); // Chamar no loop() (Core 1) para atualizar LVGL com thread-safety
void smarthome_on_wifi_changed(bool connected);
void smarthome_set_screen_visible(bool visible);

// Despacho assíncrono genérico para a API REST da VPS Coolify
// action: "status", "start", "pause", "dock", "command" ou rota "/api/v1/vacuum/..."
void sendPierreCommand(const char *action, const char *jsonPayload = "");

// Atalhos operacionais chamados pelos callbacks da UI
void smarthome_refresh_status();
void smarthome_start();
void smarthome_pause();
void smarthome_dock();
void smarthome_set_suction(const char *level); // "gentle" | "normal" | "strong"
void smarthome_set_cistern(const char *level); // "closed" | "low" | "middle" | "high"
void smarthome_seek();                         // {"code":"seek","value":true}

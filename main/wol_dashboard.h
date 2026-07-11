/*
 * wol_dashboard.h — HTTP server for WoL dashboard
 *
 * Serves:
 *   GET  /              → Dashboard SPA (HTML/CSS/JS)
 *   GET  /api/hosts     → List all hosts with online state
 *   POST /api/hosts     → Add new host {name, mac, ip}
 *   DELETE /api/hosts   → Remove host ?id=N
 *   POST /api/wake      → Send WoL to host ?id=N
 *   GET  /api/status    → ESP32 health info
 */
#pragma once

#include "wol_storage.h"
#include "esp_err.h"

/**
 * Start the HTTP dashboard server.
 * Binds to port 80 on all interfaces.
 *
 * @param list            Shared host list (must outlive the server)
 * @param get_vpn_ip_fn   Callback to read current Tailscale IP (can be NULL)
 * @param wifi_rssi       Current WiFi RSSI value
 * @return ESP_OK on success
 */
esp_err_t wol_dashboard_start(wol_host_list_t *list,
                              uint32_t (*get_vpn_ip_fn)(void),
                              int wifi_rssi);

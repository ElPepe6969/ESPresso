/*
 * wol_sender.h — Wake-on-LAN magic packet sender
 */
#pragma once

#include "esp_err.h"

/**
 * Send WoL magic packet to target MAC via UDP broadcast.
 * Packets are broadcast to 255.255.255.255:9 on the WiFi/LAN interface.
 *
 * @param mac_str  MAC address string, e.g. "AA:BB:CC:DD:EE:FF"
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if MAC is malformed
 */
esp_err_t wol_send_magic_packet(const char *mac_str);

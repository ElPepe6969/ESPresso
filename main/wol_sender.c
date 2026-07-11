/*
 * wol_sender.c — Wake-on-LAN magic packet sender
 *
 * Magic packet format (102 bytes):
 *   6 bytes  of 0xFF (sync stream)
 *   16 times target MAC (6 × 16 = 96 bytes)
 *
 * Sent as UDP broadcast to 255.255.255.255:9
 */
#include "wol_sender.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "lwip/sockets.h"
#include "esp_log.h"

static const char *TAG = "wol_sender";

/* ---------------------------------------------------------------------------
 * MAC parsing: "AA:BB:CC:DD:EE:FF" → 6 bytes
 * --------------------------------------------------------------------------- */

static esp_err_t parse_mac(const char *str, uint8_t out[6])
{
    if (!str || strlen(str) != 17) return ESP_ERR_INVALID_ARG;

    int values[6];
    if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        /* Try with hyphens */
        if (sscanf(str, "%2x-%2x-%2x-%2x-%2x-%2x",
                   &values[0], &values[1], &values[2],
                   &values[3], &values[4], &values[5]) != 6) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    for (int i = 0; i < 6; i++) out[i] = (uint8_t)values[i];
    return ESP_OK;
}

/* ---------------------------------------------------------------------------
 * Build and send magic packet
 * --------------------------------------------------------------------------- */

esp_err_t wol_send_magic_packet(const char *mac_str)
{
    uint8_t target_mac[6];
    esp_err_t ret = parse_mac(mac_str, target_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid MAC: '%s'", mac_str);
        return ret;
    }

    /* Build packet: 6 × FF + 16 × MAC */
    uint8_t packet[102];
    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(packet + 6 + (i * 6), target_mac, 6);
    }

    /* Create UDP socket */
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
        return ESP_FAIL;
    }

    /* Enable broadcast */
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                   &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(TAG, "setsockopt(SO_BROADCAST) failed: errno=%d", errno);
        close(sock);
        return ESP_FAIL;
    }

    /* Destination: 255.255.255.255:9 (standard WoL port) */
    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port   = htons(9),
        .sin_addr   = { .s_addr = 0xFFFFFFFF }, /* 255.255.255.255 */
    };

    ssize_t sent = sendto(sock, packet, sizeof(packet), 0,
                          (struct sockaddr *)&dest, sizeof(dest));
    close(sock);

    if (sent < 0) {
        ESP_LOGE(TAG, "sendto() failed: errno=%d", errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Magic packet sent to %s (%d bytes)", mac_str, (int)sent);
    return ESP_OK;
}

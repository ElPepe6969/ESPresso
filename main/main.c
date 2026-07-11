/*
 * main.c — ESPresso: Wake-on-LAN Dashboard via Tailscale
 *
 * ESP32-S3 with MicroLink v2 (Tailscale) serving a web dashboard
 * for managing and waking devices on the local network.
 *
 * Hardware: ESP32-S3 with 8MB PSRAM
 * Build:    pio run          (PlatformIO)
 *           idf.py build     (ESP-IDF directly)
 * Flash:    pio run -t upload
 * Monitor:  pio run -t monitor
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "microlink.h"

#include "wol_storage.h"
#include "wol_dashboard.h"
#include "wol_monitor.h"

static const char *TAG = "espresso";

/* ---------------------------------------------------------------------------
 * WiFi
 * --------------------------------------------------------------------------- */

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

/* Credentials — Kconfig defaults, optionally overridden by MicroLink NVS */
static char wifi_ssid[33]     = CONFIG_ML_WIFI_SSID;
static char wifi_password[65] = CONFIG_ML_WIFI_PASSWORD;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t any_id;
    esp_event_handler_instance_t got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid,     wifi_ssid,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, wifi_password,
            sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Disable power save for low-latency WireGuard traffic */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WiFi init complete, connecting to %s...", wifi_ssid);
}

/* ---------------------------------------------------------------------------
 * Tailscale callbacks
 * --------------------------------------------------------------------------- */

static microlink_t *ml = NULL;

static void on_state_change(microlink_t *handle, microlink_state_t state,
                            void *user_data)
{
    const char *names[] = {
        "IDLE", "WIFI_WAIT", "CONNECTING", "REGISTERING",
        "CONNECTED", "RECONNECTING", "ERROR"
    };
    const char *name = (state < 7) ? names[state] : "UNKNOWN";
    ESP_LOGI(TAG, "Tailscale state: %s", name);

    if (state == ML_STATE_CONNECTED) {
        uint32_t ip = microlink_get_vpn_ip(handle);
        char ip_str[16];
        microlink_ip_to_str(ip, ip_str);
        ESP_LOGI(TAG, "Tailscale IP: %s", ip_str);
    }
}

static void on_peer_update(microlink_t *handle, const microlink_peer_info_t *peer,
                           void *user_data)
{
    char ip_str[16];
    microlink_ip_to_str(peer->vpn_ip, ip_str);
    ESP_LOGI(TAG, "Peer: %s (%s) online=%d direct=%d",
             peer->hostname, ip_str, peer->online, peer->direct_path);
}

/* ---------------------------------------------------------------------------
 * Main
 * --------------------------------------------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ESPresso — WoL Dashboard v1.0     ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");

    /* --- NVS init (required by WiFi + storage) --- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* --- Storage --- */
    ESP_ERROR_CHECK(wol_storage_init());

    wol_host_list_t host_list;
    ESP_ERROR_CHECK(wol_storage_load(&host_list));

    /* --- WiFi --- */
    ESP_LOGI(TAG, "Free heap: %lu B (PSRAM: %lu B)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    wifi_init();
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(30000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi connect timeout — restarting");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    /* --- Tailscale (MicroLink v2) --- */
    microlink_config_t config = {
        .auth_key    = CONFIG_ML_TAILSCALE_AUTH_KEY,
        .device_name = CONFIG_ML_DEVICE_NAME,
        .enable_derp = true,
        .enable_stun = true,
        .enable_disco = true,
        .max_peers   = CONFIG_ML_MAX_PEERS,
    };

    ml = microlink_init(&config);
    if (!ml) {
        ESP_LOGE(TAG, "MicroLink init failed — check PSRAM config");
        return;
    }

    microlink_set_state_callback(ml, on_state_change, NULL);
    microlink_set_peer_callback(ml, on_peer_update, NULL);

    ESP_ERROR_CHECK(microlink_start(ml));

    /* Wait for Tailscale connection (120s timeout, then restart) */
    ESP_LOGI(TAG, "Connecting to Tailscale...");
    int ts_retries = 0;
    while (!microlink_is_connected(ml)) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (++ts_retries > 120) {
            ESP_LOGE(TAG, "Tailscale connect timeout — restarting");
            esp_restart();
        }
    }

    uint32_t vpn_ip = microlink_get_vpn_ip(ml);
    char vpn_ip_str[16];
    microlink_ip_to_str(vpn_ip, vpn_ip_str);
    ESP_LOGI(TAG, "Tailscale connected! VPN IP: %s", vpn_ip_str);

    /* --- HTTP Dashboard Server --- */
    /* Provide VPN IP getter callback for /api/status */
    uint32_t get_vpn_ip_cb(void) {
        return ml ? microlink_get_vpn_ip(ml) : 0;
    }

    ESP_ERROR_CHECK(wol_dashboard_start(&host_list, get_vpn_ip_cb, -40));

    /* --- Host Monitor Task --- */
    wol_monitor_start(&host_list, 60);  /* Ping every 60 seconds */

    /* --- Idle loop --- */
    ESP_LOGI(TAG, "Ready! Dashboard at http://%s", vpn_ip_str);
    ESP_LOGI(TAG, "Free heap: %lu B",
             (unsigned long)esp_get_free_heap_size());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        /* Periodic health log */
        if (microlink_is_connected(ml)) {
            ESP_LOGI(TAG, "Health: heap=%lu psram=%lu peers=%d",
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     microlink_get_peer_count(ml));
        }
    }
}

/*
 * main.c — ESPresso: Wake-on-LAN Dashboard via Tailscale
 *
 * ESP32-S3 with ts.h v3.0 (Tailscale) serving a web dashboard
 * for managing and waking devices on the local network.
 *
 * Hardware: ESP32-S3 with 8MB PSRAM
 * Build:    idf.py set-target esp32s3 && idf.py build
 * Flash:    idf.py -p PORT flash
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

#include "ts.h"

#include "mdns.h"
#include "wol_storage.h"
#include "wol_dashboard.h"
#include "wol_monitor.h"
#include "led_indicator.h"

static const char *TAG = "espresso";

/* ---------------------------------------------------------------------------
 * WiFi
 * --------------------------------------------------------------------------- */

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static char wifi_ssid[33]     = CONFIG_ESPRESSO_WIFI_SSID;
static char wifi_password[65] = CONFIG_ESPRESSO_WIFI_PASSWORD;

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

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WiFi init complete, connecting to %s...", wifi_ssid);
}

/* ---------------------------------------------------------------------------
 * Tailscale callbacks
 * --------------------------------------------------------------------------- */

static ts_t *ml = NULL;
static bool ts_ever_connected = false;

static void on_connected(void)
{
    ts_ever_connected = true;
    uint32_t ip = ts_get_vpn_ip(ml);
    char ip_str[16];
    ts_vpn_ip_to_str(ip, ip_str);
    ESP_LOGI(TAG, "Tailscale connected! VPN IP: %s", ip_str);
    ESP_LOGI(TAG, "Dashboard: http://%s", ip_str);
}

static void on_disconnected(void)
{
    ESP_LOGW(TAG, "Tailscale disconnected");
}

static void on_state_change(ts_state_t old_s, ts_state_t new_s)
{
    const char *names[] = {
        "IDLE", "CONNECTING", "REGISTERING", "CONNECTED",
        "RECONNECTING", "ERROR"
    };
    const char *old_n = (old_s < 6) ? names[old_s] : "?";
    const char *new_n = (new_s < 6) ? names[new_s] : "?";
    ESP_LOGI(TAG, "Tailscale state: %s → %s", old_n, new_n);
}

static void on_peer_added(const ts_peer_t *peer)
{
    char ip_str[16];
    ts_vpn_ip_to_str(peer->vpn_ip, ip_str);
    ESP_LOGI(TAG, "Peer added: %s (%s)", peer->hostname, ip_str);
}

static void on_peer_removed(uint32_t node_id)
{
    ESP_LOGI(TAG, "Peer removed: %lu", (unsigned long)node_id);
}

/* VPN IP getter for dashboard status endpoint */
static uint32_t get_vpn_ip_cb(void)
{
    return ml ? ts_get_vpn_ip(ml) : 0;
}

/* ---------------------------------------------------------------------------
 * Main
 * --------------------------------------------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ESPresso — WoL Dashboard v1.0     ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");

    /* NVS init */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Storage */
    ESP_ERROR_CHECK(wol_storage_init());
    wol_host_list_t host_list;
    ESP_ERROR_CHECK(wol_storage_load(&host_list));

    /* WiFi */
    ESP_LOGI(TAG, "Free heap: %lu B (PSRAM: %lu B)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    led_init();
    led_wifi_connecting();

    wifi_init();
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(30000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi connect timeout — restarting");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    /* mDNS hostname (espresso.local) */
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("espresso"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESPresso WoL Dashboard"));
    ESP_LOGI(TAG, "mDNS: espressso.local");

    /* Tailscale (ts.h v3.0) */
    ts_config_t config = {
        .auth_key     = CONFIG_ESPRESSO_TAILSCALE_AUTH_KEY,
        .device_name  = CONFIG_ESPRESSO_DEVICE_NAME,
        .enable_derp  = true,
        .enable_stun  = true,
        .enable_disco = false,
        .max_peers    = 32,
        .on_connected     = on_connected,
        .on_disconnected  = on_disconnected,
        .on_state_change  = on_state_change,
        .on_peer_added    = on_peer_added,
        .on_peer_removed  = on_peer_removed,
    };

    ml = ts_init(&config);
    if (!ml) {
        ESP_LOGE(TAG, "ts_init failed — restarting");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        return;
    }

    ESP_ERROR_CHECK(ts_connect(ml));

    /* Get actual LAN IP */
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "Dashboard on LAN: http://" IPSTR, IP2STR(&ip_info.ip));
        }
    }
    ESP_ERROR_CHECK(wol_dashboard_start(&host_list, get_vpn_ip_cb, -40));
    wol_monitor_start(&host_list, 60);
    ESP_LOGI(TAG, "Tailscale connecting in background...");

    /* Idle loop — drive Tailscale state machine + health checks */
    while (1) {
        ts_update(ml);
        vTaskDelay(pdMS_TO_TICKS(50));

        static int tick = 0;
        if (++tick >= 1200) {
            tick = 0;

            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                wol_dashboard_set_rssi(ap_info.rssi);
            }

            if (ts_is_connected(ml)) {
                ESP_LOGI(TAG, "Health: heap=%lu psram=%lu peers=%d",
                         (unsigned long)esp_get_free_heap_size(),
                         (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                         ts_get_peer_count(ml));
            } else {
                ESP_LOGD(TAG, "Waiting for Tailscale... heap=%lu",
                         (unsigned long)esp_get_free_heap_size());
            }
        }
    }
}

/*
 * wol_monitor.c — Periodic ICMP ping task for host online detection
 *
 * Uses ESP-IDF's built-in ICMP echo (ping) API.
 * Pings hosts sequentially with a semaphore for reliable completion sync.
 * One ping session reused for all hosts to avoid heap fragmentation.
 */
#include "wol_monitor.h"
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "ping/ping_sock.h"
#include "esp_log.h"
#include "lwip/inet.h"

static const char *TAG = "wol_monitor";

/* Semaphore signaled by ping callback on completion */
static SemaphoreHandle_t s_ping_done = NULL;

/* Forward declaration */
static bool ping_host(esp_ping_handle_t ping, const char *ip_str,
                      uint32_t timeout_ms);

/* ---------------------------------------------------------------------------
 * Ping callbacks — signal semaphore on completion
 * --------------------------------------------------------------------------- */

static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    bool *result = (bool *)args;
    *result = true;
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    bool *result = (bool *)args;
    *result = false;
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    /* Signal that ping is complete */
    if (s_ping_done) {
        xSemaphoreGive(s_ping_done);
    }
}

/* ---------------------------------------------------------------------------
 * Ping a single host. Returns true if reachable within timeout_ms.
 * Uses a reusable ping session (created once).
 * --------------------------------------------------------------------------- */

static bool ping_host(esp_ping_handle_t ping, const char *ip_str,
                      uint32_t timeout_ms)
{
    /* Parse IP */
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count        = 1;
    config.interval_ms  = 0;
    config.timeout_ms   = timeout_ms;
    config.task_stack_size = 2048;

    ip_addr_t target_addr;
    if (!ipaddr_aton(ip_str, &target_addr)) {
        ESP_LOGW(TAG, "Invalid IP: %s", ip_str);
        return false;
    }
    config.target_addr = target_addr;

    bool result = false;
    esp_ping_callbacks_t cbs = {
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end     = on_ping_end,
        .cb_args         = &result,
    };

    /* Reconfigure ping session with new target */
    esp_err_t ret = esp_ping_new_session(&config, &cbs, &ping);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Ping session create failed for %s: %d", ip_str, ret);
        return false;
    }

    esp_ping_start(ping);

    /* Wait for ping to complete (signaled by on_ping_end) */
    if (xSemaphoreTake(s_ping_done, pdMS_TO_TICKS(timeout_ms + 1000))
        != pdTRUE) {
        ESP_LOGW(TAG, "Ping timed out waiting for completion: %s", ip_str);
        esp_ping_stop(ping);
    }

    esp_ping_delete_session(ping);
    return result;
}

/* ---------------------------------------------------------------------------
 * Monitor task
 * --------------------------------------------------------------------------- */

typedef struct {
    wol_host_list_t *list;
    uint32_t         interval_seconds;
} monitor_task_args_t;

static void wol_monitor_task(void *pvParameters)
{
    monitor_task_args_t *args = (monitor_task_args_t *)pvParameters;
    wol_host_list_t *list     = args->list;
    uint32_t interval_sec     = args->interval_seconds;

    /* Create semaphore for ping completion sync */
    s_ping_done = xSemaphoreCreateBinary();
    if (!s_ping_done) {
        ESP_LOGE(TAG, "Failed to create ping semaphore");
        free(args);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Monitor task started, interval=%lu s",
             (unsigned long)interval_sec);

    while (1) {
        if (list->count == 0) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        uint32_t now = (uint32_t)time(NULL);

        wol_storage_lock();
        for (int i = 0; i < list->count; i++) {
            wol_host_t *h = &list->entries[i];
            if (h->ip[0] == '\0') continue;

            ESP_LOGD(TAG, "Pinging %s (%s)...", h->name, h->ip);

            /* Release lock while pinging (network I/O, can take 1s+) */
            char ip_copy[WOL_HOST_IP_MAX];
            uint32_t host_id = h->id;
            snprintf(ip_copy, sizeof(ip_copy), "%s", h->ip);
            wol_storage_unlock();

            esp_ping_handle_t ping = NULL;
            bool online = ping_host(ping, ip_copy, 1000);

            wol_storage_lock();
            wol_storage_update_online(list, host_id, online, now);

            if (online) {
                ESP_LOGD(TAG, "  %s: ONLINE", h->name);
            } else {
                ESP_LOGD(TAG, "  %s: offline", h->name);
            }

            /* Small gap between pings */
            wol_storage_unlock();
            vTaskDelay(pdMS_TO_TICKS(200));
            wol_storage_lock();
        }
        wol_storage_unlock();

        ESP_LOGI(TAG, "Cycle complete. Next in %lu s",
                 (unsigned long)interval_sec);
        vTaskDelay(pdMS_TO_TICKS(interval_sec * 1000));
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

void wol_monitor_start(wol_host_list_t *list, uint32_t interval_seconds)
{
    if (interval_seconds < 10) interval_seconds = 10;

    monitor_task_args_t *args = malloc(sizeof(monitor_task_args_t));
    if (!args) {
        ESP_LOGE(TAG, "Failed to allocate monitor task args");
        return;
    }
    args->list             = list;
    args->interval_seconds = interval_seconds;

    BaseType_t ret = xTaskCreate(
        wol_monitor_task,
        "wol_monitor",
        6144,        /* Increased stack — ping config + ESP_LOG */
        args,
        3,           /* Priority — low, network I/O only */
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitor task");
        free(args);
    }
}

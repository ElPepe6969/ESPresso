/*
 * wol_monitor.c — Periodic ICMP ping task for host online detection
 *
 * Uses ESP-IDF's built-in ICMP echo (ping) API.
 * Pings hosts sequentially with a small delay between to avoid flooding.
 */
#include "wol_monitor.h"
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_ping.h"
#include "esp_log.h"
#include "lwip/inet.h"

static const char *TAG = "wol_monitor";

/* Per-ping result passed via queue from callback to task */
typedef struct {
    uint32_t host_id;
    bool     success;
} ping_result_t;

/* ---------------------------------------------------------------------------
 * Ping callback — runs in ping thread context, must be minimal
 * --------------------------------------------------------------------------- */

static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    ping_result_t *result = (ping_result_t *)args;
    result->success = true;
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    ping_result_t *result = (ping_result_t *)args;
    result->success = false;
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    /* Nothing needed — result already set */
}

/* ---------------------------------------------------------------------------
 * Ping a single host. Returns true if reachable within timeout_ms.
 * --------------------------------------------------------------------------- */

static bool ping_host(const char *ip_str, uint32_t timeout_ms)
{
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count        = 1;
    config.interval_ms  = 0;
    config.timeout_ms   = timeout_ms;
    config.task_stack_size = 2048;

    /* Parse IP */
    ip_addr_t target_addr;
    if (!ipaddr_aton(ip_str, &target_addr)) {
        ESP_LOGW(TAG, "Invalid IP: %s", ip_str);
        return false;
    }
    config.target_addr = target_addr;

    /* We need to set callbacks AND get the result. The ping API is
     * callback-based but esp_ping_start() blocks until done when
     * task_priority > 0. However, the callbacks also fire.
     * We use a simple volatile flag approach. */
    ping_result_t result = { .host_id = 0, .success = false };

    esp_ping_callbacks_t cbs = {
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end     = on_ping_end,
        .cb_args         = &result,
    };

    esp_ping_handle_t ping = NULL;
    esp_err_t ret = esp_ping_new_session(&config, &cbs, &ping);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Ping session create failed for %s: %d", ip_str, ret);
        return false;
    }

    esp_ping_start(ping);
    /* esp_ping_start blocks until ping completes when task_priority is set
     * in config. The default config uses a dedicated task internally.
     * We poll/wait instead. */
    vTaskDelay(pdMS_TO_TICKS(timeout_ms + 500));

    /* Get the result from our callback flag */
    bool success = result.success;

    esp_ping_stop(ping);
    esp_ping_delete_session(ping);

    return success;
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

    ESP_LOGI(TAG, "Monitor task started, interval=%lu s",
             (unsigned long)interval_sec);

    while (1) {
        if (list->count == 0) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        uint32_t now = (uint32_t)time(NULL);

        for (int i = 0; i < list->count; i++) {
            wol_host_t *h = &list->entries[i];
            if (h->ip[0] == '\0') continue;

            ESP_LOGD(TAG, "Pinging %s (%s)...", h->name, h->ip);
            bool online = ping_host(h->ip, 1000);

            wol_storage_update_online(list, h->id, online, now);

            if (online) {
                ESP_LOGD(TAG, "  %s: ONLINE", h->name);
            } else {
                ESP_LOGD(TAG, "  %s: offline", h->name);
            }

            /* Small gap between pings to avoid flooding */
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        ESP_LOGI(TAG, "Cycle complete. Next in %lu s",
                 (unsigned long)interval_sec);

        /* Sleep until next cycle */
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
        4096,        /* Stack size — ping needs stack */
        args,
        3,           /* Priority — low, network I/O only */
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitor task");
        free(args);
    }
}

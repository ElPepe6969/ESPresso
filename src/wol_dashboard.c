/*
 * wol_dashboard.c — HTTP server for WoL dashboard
 *
 * REST API:
 *   GET  /api/hosts   → {"hosts":[...]}
 *   POST /api/hosts   → {"ok":true, "id":N}  (body: {name,mac,ip})
 *   DELETE /api/hosts → {"ok":true}          (?id=N)
 *   POST /api/wake    → {"ok":true, "host":"name"}  (?id=N)
 *   GET  /api/status  → {"uptime":N, "free_heap":N, ...}
 *   GET  /            → Dashboard SPA (from wol_webui.h)
 */
#include "wol_dashboard.h"
#include "wol_sender.h"
#include "wol_webui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "wol_dashboard";

/* Shared state — set once at startup */
static wol_host_list_t *g_host_list = NULL;
static uint32_t (*g_get_vpn_ip)(void) = NULL;
static volatile int g_wifi_rssi = 0;
static uint32_t g_start_time = 0;
static httpd_handle_t g_server = NULL;

/* ---------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------- */

static void set_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

/**
 * Send a cJSON object as JSON response. Handles NULL (OOM) safely.
 * Takes ownership of `obj` (cJSON_Delete is called).
 */
static esp_err_t send_json(httpd_req_t *req, cJSON *obj)
{
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Out of memory");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, strlen(json));
    free(json);
    return ret;
}

/* ---------------------------------------------------------------------------
 * GET /api/hosts — list all hosts with online state
 * --------------------------------------------------------------------------- */

static esp_err_t api_hosts_get(httpd_req_t *req)
{
    set_cors_headers(req);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();

    wol_storage_lock();
    for (int i = 0; i < g_host_list->count; i++) {
        wol_host_t *h = &g_host_list->entries[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id",        h->id);
        cJSON_AddStringToObject(item, "name",      h->name);
        cJSON_AddStringToObject(item, "mac",       h->mac);
        cJSON_AddStringToObject(item, "ip",        h->ip);
        cJSON_AddBoolToObject(item,   "online",    h->online);
        cJSON_AddNumberToObject(item, "last_seen", h->last_seen);
        cJSON_AddItemToArray(arr, item);
    }
    wol_storage_unlock();

    cJSON_AddItemToObject(root, "hosts", arr);
    return send_json(req, root);
}

/* ---------------------------------------------------------------------------
 * POST /api/hosts — add new host
 * Body: {"name":"...", "mac":"...", "ip":"..."}
 * --------------------------------------------------------------------------- */

static esp_err_t api_hosts_post(httpd_req_t *req)
{
    set_cors_headers(req);

    /* Read body */
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    cJSON *body = cJSON_Parse(buf);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const cJSON *jn = cJSON_GetObjectItem(body, "name");
    const cJSON *jm = cJSON_GetObjectItem(body, "mac");
    const cJSON *ji = cJSON_GetObjectItem(body, "ip");

    if (!jn || !jm || !ji ||
        !cJSON_IsString(jn) || !cJSON_IsString(jm) || !cJSON_IsString(ji)) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Missing name, mac, or ip");
        return ESP_FAIL;
    }

    /* Basic MAC validation */
    const char *mac = jm->valuestring;
    if (strlen(mac) != 17) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MAC format");
        return ESP_FAIL;
    }

    /* Validate name length */
    const char *name = jn->valuestring;
    if (strlen(name) == 0 || strlen(name) >= WOL_HOST_NAME_MAX) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid name length");
        return ESP_FAIL;
    }

    /* Validate IP format (basic: must contain dots, not empty) */
    const char *ip = ji->valuestring;
    if (strlen(ip) < 7 || strlen(ip) >= WOL_HOST_IP_MAX ||
        strchr(ip, '.') == NULL) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP format");
        return ESP_FAIL;
    }

    uint32_t new_id = 0;
    wol_storage_lock();
    esp_err_t ret = wol_storage_add(g_host_list,
                                    name, mac, ip, &new_id);
    wol_storage_unlock();

    /* Persist to NVS outside the lock (NVS writes are slow) */
    if (ret == ESP_OK) {
        wol_storage_lock();
        esp_err_t save_ret = wol_storage_save(g_host_list);
        wol_storage_unlock();
        if (save_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist new host to NVS: %d", save_ret);
        }
    }
    cJSON_Delete(body);

    /* Build response */
    cJSON *resp = cJSON_CreateObject();
    if (ret == ESP_OK) {
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddNumberToObject(resp, "id", new_id);
    } else if (ret == ESP_ERR_NO_MEM) {
        cJSON_AddBoolToObject(resp, "ok", false);
        cJSON_AddStringToObject(resp, "error", "Max hosts reached");
    } else {
        cJSON_AddBoolToObject(resp, "ok", false);
        cJSON_AddStringToObject(resp, "error", "Storage error");
    }

    return send_json(req, resp);
}

/* ---------------------------------------------------------------------------
 * DELETE /api/hosts?id=N — remove host
 * --------------------------------------------------------------------------- */

static esp_err_t api_hosts_delete(httpd_req_t *req)
{
    set_cors_headers(req);

    /* Get ?id= parameter */
    char id_str[64];
    if (httpd_req_get_url_query_str(req, id_str, sizeof(id_str)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ?id=");
        return ESP_FAIL;
    }

    char param[16];
    if (httpd_query_key_value(id_str, "id", param, sizeof(param)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ?id=");
        return ESP_FAIL;
    }

    uint32_t id = (uint32_t)atoi(param);

    wol_storage_lock();
    esp_err_t ret = wol_storage_remove(g_host_list, id);
    wol_storage_unlock();

    /* Persist to NVS outside the lock */
    if (ret == ESP_OK) {
        wol_storage_lock();
        esp_err_t save_ret = wol_storage_save(g_host_list);
        wol_storage_unlock();
        if (save_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist removal to NVS: %d", save_ret);
        }
    }

    cJSON *resp = cJSON_CreateObject();
    if (ret == ESP_OK) {
        cJSON_AddBoolToObject(resp, "ok", true);
    } else {
        cJSON_AddBoolToObject(resp, "ok", false);
        cJSON_AddStringToObject(resp, "error", "Not found");
    }

    return send_json(req, resp);
}

/* ---------------------------------------------------------------------------
 * POST /api/wake?id=N — send WoL magic packet
 * --------------------------------------------------------------------------- */

static esp_err_t api_wake_post(httpd_req_t *req)
{
    set_cors_headers(req);

    /* Get ?id= parameter */
    char id_str[64];
    if (httpd_req_get_url_query_str(req, id_str, sizeof(id_str)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ?id=");
        return ESP_FAIL;
    }

    char param[16];
    if (httpd_query_key_value(id_str, "id", param, sizeof(param)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ?id=");
        return ESP_FAIL;
    }

    uint32_t id = (uint32_t)atoi(param);

    wol_storage_lock();
    wol_host_t *h = wol_storage_find(g_host_list, id);
    cJSON *resp = cJSON_CreateObject();

    if (h) {
        char name_copy[WOL_HOST_NAME_MAX];
        char mac_copy[WOL_HOST_MAC_MAX];
        snprintf(name_copy, sizeof(name_copy), "%s", h->name);
        snprintf(mac_copy,  sizeof(mac_copy),  "%s", h->mac);
        wol_storage_unlock();

        esp_err_t send_ret = wol_send_magic_packet(mac_copy);
        if (send_ret == ESP_OK) {
            cJSON_AddBoolToObject(resp, "ok", true);
            cJSON_AddStringToObject(resp, "host", name_copy);
            ESP_LOGI(TAG, "Wake sent to %s (%s)", name_copy, mac_copy);
        } else {
            cJSON_AddBoolToObject(resp, "ok", false);
            cJSON_AddStringToObject(resp, "error", "Send failed");
        }
    } else {
        wol_storage_unlock();
        cJSON_AddBoolToObject(resp, "ok", false);
        cJSON_AddStringToObject(resp, "error", "Host not found");
    }

    return send_json(req, resp);
}

/* ---------------------------------------------------------------------------
 * GET /api/status — ESP32 health
 * --------------------------------------------------------------------------- */

static esp_err_t api_status_get(httpd_req_t *req)
{
    set_cors_headers(req);

    uint32_t vpn_ip = g_get_vpn_ip ? g_get_vpn_ip() : 0;
    char ip_str[16] = "unknown";
    if (vpn_ip != 0) {
        snprintf(ip_str, sizeof(ip_str), "%lu.%lu.%lu.%lu",
                 (unsigned long)(vpn_ip & 0xFF),
                 (unsigned long)((vpn_ip >> 8) & 0xFF),
                 (unsigned long)((vpn_ip >> 16) & 0xFF),
                 (unsigned long)((vpn_ip >> 24) & 0xFF));
    }

    uint32_t uptime = (g_start_time > 0)
                      ? (uint32_t)(time(NULL)) - g_start_time
                      : 0;

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "uptime",              uptime);
    cJSON_AddNumberToObject(resp, "free_heap",           esp_get_free_heap_size());
    cJSON_AddStringToObject(resp, "tailscale_ip",        ip_str);
    cJSON_AddBoolToObject(resp,   "tailscale_connected", (vpn_ip != 0));
    cJSON_AddNumberToObject(resp, "wifi_rssi",           g_wifi_rssi);
    wol_storage_lock();
    cJSON_AddNumberToObject(resp, "host_count", g_host_list ? g_host_list->count : 0);
    wol_storage_unlock();

    return send_json(req, resp);
}

void wol_dashboard_set_rssi(int rssi)
{
    g_wifi_rssi = rssi;
}

/* ---------------------------------------------------------------------------
 * GET / — serve dashboard SPA
 * --------------------------------------------------------------------------- */

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, WOL_WEBUI_HTML, strlen(WOL_WEBUI_HTML));
    return ESP_OK;
}

/* ---------------------------------------------------------------------------
 * OPTIONS handler for CORS preflight
 * --------------------------------------------------------------------------- */

static esp_err_t cors_options(httpd_req_t *req)
{
    set_cors_headers(req);
    httpd_resp_send(req, "", 0);
    return ESP_OK;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

esp_err_t wol_dashboard_start(wol_host_list_t *list,
                              uint32_t (*get_vpn_ip_fn)(void),
                              int wifi_rssi)
{
    g_host_list  = list;
    g_get_vpn_ip = get_vpn_ip_fn;
    g_wifi_rssi  = wifi_rssi;
    g_start_time = (uint32_t)time(NULL);

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 12;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %d", ret);
        return ret;
    }
    g_server = server;

    /* Register URI handlers */
    httpd_uri_t uri_root = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = root_get,
    };
    httpd_register_uri_handler(server, &uri_root);

    httpd_uri_t uri_hosts_get = {
        .uri      = "/api/hosts",
        .method   = HTTP_GET,
        .handler  = api_hosts_get,
    };
    httpd_register_uri_handler(server, &uri_hosts_get);

    httpd_uri_t uri_hosts_post = {
        .uri      = "/api/hosts",
        .method   = HTTP_POST,
        .handler  = api_hosts_post,
    };
    httpd_register_uri_handler(server, &uri_hosts_post);

    httpd_uri_t uri_hosts_delete = {
        .uri      = "/api/hosts",
        .method   = HTTP_DELETE,
        .handler  = api_hosts_delete,
    };
    httpd_register_uri_handler(server, &uri_hosts_delete);

    httpd_uri_t uri_wake = {
        .uri      = "/api/wake",
        .method   = HTTP_POST,
        .handler  = api_wake_post,
    };
    httpd_register_uri_handler(server, &uri_wake);

    httpd_uri_t uri_status = {
        .uri      = "/api/status",
        .method   = HTTP_GET,
        .handler  = api_status_get,
    };
    httpd_register_uri_handler(server, &uri_status);

    /* CORS preflight for all /api/* routes */
    httpd_uri_t uri_cors = {
        .uri      = "/api/*",
        .method   = HTTP_OPTIONS,
        .handler  = cors_options,
    };
    httpd_register_uri_handler(server, &uri_cors);

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return ESP_OK;
}

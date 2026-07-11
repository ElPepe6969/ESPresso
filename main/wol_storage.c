/*
 * wol_storage.c — Persistent host configuration via NVS
 */
#include "wol_storage.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "wol_storage";
static SemaphoreHandle_t s_mutex = NULL;

/* ---------------------------------------------------------------------------
 * Init
 * --------------------------------------------------------------------------- */

esp_err_t wol_storage_init(void)
{
    /* NVS must already be initialized by main.c before calling this.
     * We just create the mutex here. */
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Storage ready");
    return ESP_OK;
}

/* ---------------------------------------------------------------------------
 * Load / Save
 * --------------------------------------------------------------------------- */

static cJSON *host_to_json(const wol_host_t *h)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id",       h->id);
    cJSON_AddStringToObject(obj, "name",     h->name);
    cJSON_AddStringToObject(obj, "mac",      h->mac);
    cJSON_AddStringToObject(obj, "ip",       h->ip);
    cJSON_AddNumberToObject(obj, "added_at", h->added_at);
    return obj;
}

static void json_to_host(const cJSON *obj, wol_host_t *h, uint32_t now)
{
    const cJSON *jid = cJSON_GetObjectItem(obj, "id");
    const cJSON *jat = cJSON_GetObjectItem(obj, "added_at");
    h->id       = (jid && cJSON_IsNumber(jid)) ? (uint32_t)jid->valueint : 0;
    h->added_at = (jat && cJSON_IsNumber(jat)) ? (uint32_t)jat->valueint : 0;

    const cJSON *jn = cJSON_GetObjectItem(obj, "name");
    const cJSON *jm = cJSON_GetObjectItem(obj, "mac");
    const cJSON *ji = cJSON_GetObjectItem(obj, "ip");

    snprintf(h->name, WOL_HOST_NAME_MAX, "%s",
             (jn && jn->valuestring) ? jn->valuestring : "");
    snprintf(h->mac, WOL_HOST_MAC_MAX, "%s",
             (jm && jm->valuestring) ? jm->valuestring : "");
    snprintf(h->ip, WOL_HOST_IP_MAX, "%s",
             (ji && ji->valuestring) ? ji->valuestring : "");

    h->online    = false;
    h->last_seen = 0;
}

esp_err_t wol_storage_load(wol_host_list_t *list)
{
    memset(list, 0, sizeof(*list));
    list->next_id = 1;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(WOL_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No NVS data yet (fresh start)");
        return ESP_OK; /* Not an error — just empty */
    }

    /* Read next_id */
    uint32_t next_id = 1;
    nvs_get_u32(handle, "next_id", &next_id);
    list->next_id = next_id;

    /* Read JSON blob */
    size_t required = 0;
    ret = nvs_get_str(handle, WOL_NVS_KEY_HOSTS, NULL, &required);
    if (ret != ESP_OK || required == 0) {
        nvs_close(handle);
        ESP_LOGW(TAG, "No hosts saved yet");
        return ESP_OK;
    }

    char *json_str = malloc(required + 1);
    if (!json_str) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    ret = nvs_get_str(handle, WOL_NVS_KEY_HOSTS, json_str, &required);
    nvs_close(handle);

    if (ret != ESP_OK) {
        free(json_str);
        return ret;
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Invalid JSON in NVS");
        return ESP_FAIL;
    }

    int count = cJSON_GetArraySize(root);
    if (count > WOL_MAX_HOSTS) count = WOL_MAX_HOSTS;

    uint32_t now = (uint32_t)time(NULL);
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (item) json_to_host(item, &list->entries[i], now);
    }
    list->count = count;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Loaded %d hosts, next_id=%lu", count, (unsigned long)next_id);
    return ESP_OK;
}

esp_err_t wol_storage_save(const wol_host_list_t *list)
{
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < list->count; i++) {
        cJSON_AddItemToArray(root, host_to_json(&list->entries[i]));
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ESP_ERR_NO_MEM;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(WOL_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        free(json_str);
        return ret;
    }

    ret = nvs_set_str(handle, WOL_NVS_KEY_HOSTS, json_str);
    if (ret == ESP_OK) {
        ret = nvs_set_u32(handle, "next_id", list->next_id);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    free(json_str);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saved %d hosts", list->count);
    }
    return ret;
}

/* ---------------------------------------------------------------------------
 * CRUD
 * --------------------------------------------------------------------------- */

esp_err_t wol_storage_add(wol_host_list_t *list,
                          const char *name,
                          const char *mac,
                          const char *ip,
                          uint32_t *out_id)
{
    if (list->count >= WOL_MAX_HOSTS) return ESP_ERR_NO_MEM;

    wol_host_t *h = &list->entries[list->count];
    h->id       = list->next_id++;
    h->added_at = (uint32_t)time(NULL);
    h->online   = false;
    h->last_seen = 0;
    snprintf(h->name, WOL_HOST_NAME_MAX, "%s", name);
    snprintf(h->mac,  WOL_HOST_MAC_MAX,   "%s", mac);
    snprintf(h->ip,   WOL_HOST_IP_MAX,    "%s", ip);
    list->count++;

    if (out_id) *out_id = h->id;

    ESP_LOGI(TAG, "Added host %lu: %s (%s)",
             (unsigned long)h->id, h->name, h->mac);
    return ESP_OK;
}

esp_err_t wol_storage_remove(wol_host_list_t *list, uint32_t id)
{
    int idx = -1;
    for (int i = 0; i < list->count; i++) {
        if (list->entries[i].id == id) { idx = i; break; }
    }
    if (idx < 0) return ESP_ERR_NOT_FOUND;

    /* Shift remaining entries down */
    for (int i = idx; i < list->count - 1; i++) {
        list->entries[i] = list->entries[i + 1];
    }
    list->count--;
    memset(&list->entries[list->count], 0, sizeof(wol_host_t));

    ESP_LOGI(TAG, "Removed host id=%lu", (unsigned long)id);
    return ESP_OK;
}

wol_host_t *wol_storage_find(wol_host_list_t *list, uint32_t id)
{
    for (int i = 0; i < list->count; i++) {
        if (list->entries[i].id == id) return &list->entries[i];
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Thread safety
 * --------------------------------------------------------------------------- */

void wol_storage_lock(void)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void wol_storage_unlock(void)
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

/* ---------------------------------------------------------------------------
 * Online state (volatile, not persisted)
 * --------------------------------------------------------------------------- */

void wol_storage_update_online(wol_host_list_t *list,
                               uint32_t id,
                               bool online,
                               uint32_t now)
{
    wol_host_t *h = wol_storage_find(list, id);
    if (!h) return;
    h->online = online;
    if (online) h->last_seen = now;
}

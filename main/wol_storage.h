/*
 * wol_storage.h — Persistent host configuration via NVS
 *
 * Stores host list as JSON blob in NVS namespace "wol".
 * Each host: {id, name, mac, ip, added_at}
 * Online state is tracked in RAM (volatile), not persisted.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define WOL_MAX_HOSTS        32
#define WOL_HOST_NAME_MAX    32
#define WOL_HOST_MAC_MAX     18    /* "AA:BB:CC:DD:EE:FF\0" */
#define WOL_HOST_IP_MAX      16    /* "255.255.255.255\0" */
#define WOL_NVS_NAMESPACE    "wol"
#define WOL_NVS_KEY_HOSTS    "hosts_json"

typedef struct {
    uint32_t id;                    /* Auto-incremented */
    char     name[WOL_HOST_NAME_MAX];
    char     mac[WOL_HOST_MAC_MAX];
    char     ip[WOL_HOST_IP_MAX];
    uint32_t added_at;              /* Unix timestamp */
    /* Runtime state (not persisted) */
    bool     online;
    uint32_t last_seen;             /* Unix timestamp of last successful ping */
} wol_host_t;

typedef struct {
    wol_host_t entries[WOL_MAX_HOSTS];
    int        count;
    uint32_t   next_id;             /* Next auto-increment ID */
} wol_host_list_t;

/**
 * Initialize NVS partition. Must be called once before any other function.
 */
esp_err_t wol_storage_init(void);

/**
 * Load all hosts from NVS into memory list.
 * Sets count=0 and next_id=1 if no hosts saved yet.
 */
esp_err_t wol_storage_load(wol_host_list_t *list);

/**
 * Save entire host list to NVS. Overwrites previous data.
 */
esp_err_t wol_storage_save(const wol_host_list_t *list);

/**
 * Add a new host. Assigns next_id, sets added_at, appends to list, saves.
 * Returns the assigned id in *out_id.
 */
esp_err_t wol_storage_add(wol_host_list_t *list,
                          const char *name,
                          const char *mac,
                          const char *ip,
                          uint32_t *out_id);

/**
 * Remove a host by id. Shifts remaining entries, saves.
 */
esp_err_t wol_storage_remove(wol_host_list_t *list, uint32_t id);

/**
 * Find a host by id. Returns pointer into list->entries, or NULL.
 */
wol_host_t *wol_storage_find(wol_host_list_t *list, uint32_t id);

/**
 * Update online state of all hosts (called by monitor task).
 * Does NOT persist — online state is volatile.
 */
void wol_storage_update_online(wol_host_list_t *list,
                               uint32_t id,
                               bool online,
                               uint32_t now);

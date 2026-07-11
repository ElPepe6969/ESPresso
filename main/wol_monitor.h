/*
 * wol_monitor.h — Periodic ICMP ping task for host online detection
 */
#pragma once

#include "wol_storage.h"

/**
 * Start the monitor FreeRTOS task.
 * Pings each host every `interval_seconds`, updates online state.
 *
 * @param list     Pointer to the shared host list (must outlive the task)
 * @param interval_seconds  Ping interval (default 60, minimum 10)
 */
void wol_monitor_start(wol_host_list_t *list, uint32_t interval_seconds);

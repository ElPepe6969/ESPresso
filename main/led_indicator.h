/*
 * led_indicator.h — Status LED via configurable GPIO
 * 
 * Blink patterns:
 *   Slow (1Hz)   = WiFi connecting
 *   Medium (2Hz) = Tailscale connecting  
 *   Solid ON      = Connected + dashboard ready
 *   Quick pulse   = Wake packet sent
 */
#pragma once

#include "esp_err.h"

#define LED_GPIO 8     /* Change to -1 to disable. XIAO C3: none built-in.
                           Super Mini C3: GPIO8 (blue, active high).
                           Wire external LED + 220Ω: GPIO21 */

/**
 * Initialize LED GPIO. Safe to call even if LED_GPIO is -1.
 */
void led_init(void);

/** Set blink pattern */
void led_wifi_connecting(void);
void led_tailscale_connecting(void);
void led_connected(void);
void led_wake_sent(void);

/*
 * led_indicator.c — Status LED via configurable GPIO
 */
#include "led_indicator.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "led";

#if LED_GPIO >= 0

static TaskHandle_t s_led_task = NULL;
static volatile int s_pattern = 0; /* 0=off, 1=slow, 2=medium, 3=on, 4=pulse */

static void led_task(void *pv)
{
    while (1) {
        switch (s_pattern) {
        case 0: /* Off */
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        case 1: /* Slow blink — WiFi connecting */
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        case 2: /* Medium blink — Tailscale connecting */
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        case 3: /* Solid ON — Connected */
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        case 4: /* Quick pulse — Wake sent */
            for (int i = 0; i < 3; i++) {
                gpio_set_level(LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(50));
                gpio_set_level(LED_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            s_pattern = 3; /* Back to solid */
            break;
        }
    }
}

void led_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    xTaskCreate(led_task, "led", 2048, NULL, 1, &s_led_task);
    ESP_LOGI(TAG, "LED on GPIO%d", LED_GPIO);
}

void led_wifi_connecting(void)     { s_pattern = 1; }
void led_tailscale_connecting(void) { s_pattern = 2; }
void led_connected(void)            { s_pattern = 3; }
void led_wake_sent(void)            { s_pattern = 4; }

#else /* LED_GPIO < 0 — disabled */

void led_init(void)                   {}
void led_wifi_connecting(void)        {}
void led_tailscale_connecting(void)   {}
void led_connected(void)              {}
void led_wake_sent(void)              {}

#endif

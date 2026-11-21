#pragma once
#include <cstdint>
#include "esp_err.h"

typedef enum {
    ESP_RST_UNKNOWN,
    ESP_RST_POWERON,
    ESP_RST_EXT,
    ESP_RST_SW,
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
    ESP_RST_DEEPSLEEP,
    ESP_RST_BROWNOUT,
    ESP_RST_SDIO,
    ESP_RST_USB,
    ESP_RST_JTAG,
    ESP_RST_EFUSE,
    ESP_RST_PWR_GLITCH,
    ESP_RST_CPU_LOCKUP,
} esp_reset_reason_t;

inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
[[noreturn]] void esp_restart();
uint32_t esp_get_free_heap_size();
inline uint32_t esp_get_minimum_free_heap_size() { return esp_get_free_heap_size(); }
using shutdown_handler_t = void (*)();
inline esp_err_t esp_register_shutdown_handler(shutdown_handler_t) { return ESP_OK; }

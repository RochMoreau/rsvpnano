#pragma once
#include <cstdarg>
#include <cstdint>

typedef enum {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

void rsvp_log_write(esp_log_level_t level, const char* tag, const char* format, ...)
    __attribute__((format(printf, 3, 4)));
void esp_log_level_set(const char* tag, esp_log_level_t level);
uint32_t esp_log_timestamp();
using vprintf_like_t = int (*)(const char*, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t func);
int esp_rom_printf(const char* format, ...) __attribute__((format(printf, 1, 2)));

#define ESP_LOGE(tag, format, ...) rsvp_log_write(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) rsvp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) rsvp_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) rsvp_log_write(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) rsvp_log_write(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGE ESP_LOGE
#define ESP_EARLY_LOGW ESP_LOGW
#define ESP_EARLY_LOGI ESP_LOGI
#define log_e(format, ...) ESP_LOGE("arduino", format, ##__VA_ARGS__)
#define log_w(format, ...) ESP_LOGW("arduino", format, ##__VA_ARGS__)
#define log_i(format, ...) ESP_LOGI("arduino", format, ##__VA_ARGS__)
#define log_d(format, ...) ESP_LOGD("arduino", format, ##__VA_ARGS__)
#define log_v(format, ...) ESP_LOGV("arduino", format, ##__VA_ARGS__)

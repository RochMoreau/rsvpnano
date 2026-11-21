#pragma once
#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned;
using TickType_t = uint32_t;
using StackType_t = uint8_t;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr TickType_t portMAX_DELAY = 0xffffffffU;
constexpr TickType_t portTICK_PERIOD_MS = 1;
constexpr UBaseType_t tskIDLE_PRIORITY = 0;
constexpr BaseType_t tskNO_AFFINITY = 0x7fffffff;
#define configMAX_PRIORITIES 25
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#define portYIELD_FROM_ISR(...) ((void) 0)
#define portENTER_CRITICAL(mux) ((void) (mux))
#define portEXIT_CRITICAL(mux) ((void) (mux))
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

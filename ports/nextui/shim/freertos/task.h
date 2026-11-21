#pragma once
#include "FreeRTOS.h"

// FreeRTOS tasks mapped onto POSIX threads.
struct RsvpTask;
using TaskHandle_t = RsvpTask*;
using TaskFunction_t = void (*)(void*);

BaseType_t xTaskCreate(TaskFunction_t entry, const char* name, uint32_t stackBytes, void* argument,
                       UBaseType_t priority, TaskHandle_t* handle);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char* name, uint32_t stackBytes, void* argument,
                                   UBaseType_t priority, TaskHandle_t* handle, BaseType_t core);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
TaskHandle_t xTaskGetCurrentTaskHandle();
const char* pcTaskGetName(TaskHandle_t task);
inline int xPortGetCoreID() { return 0; }
void xTaskNotifyGive(TaskHandle_t task);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t* woken);
uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticks);
inline TickType_t xTaskGetTickCount() {
    extern unsigned long millis();
    return static_cast<TickType_t>(millis());
}
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
inline void taskYIELD() {}

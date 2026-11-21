#pragma once
#include <cstddef>
#include "FreeRTOS.h"

struct RsvpQueue;
using QueueHandle_t = RsvpQueue*;

QueueHandle_t xQueueCreate(size_t length, size_t itemSize);
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticks);
inline BaseType_t xQueueSendToBack(QueueHandle_t queue, const void* item, TickType_t ticks) {
    return xQueueSend(queue, item, ticks);
}
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t ticks);
BaseType_t xQueueReset(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);

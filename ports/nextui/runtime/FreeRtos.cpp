// FreeRTOS tasks, queues and notifications on top of std::thread.
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <pthread.h>

struct RsvpTask {
    std::string name;
    TaskFunction_t entry = nullptr;
    void* argument = nullptr;
    std::mutex mutex;
    std::condition_variable condition;
    uint32_t notifications = 0;
};

namespace {
    thread_local RsvpTask* tCurrentTask = nullptr;
    RsvpTask gMainTask{.name = "main"};

    RsvpTask* currentTask() {
        return tCurrentTask != nullptr ? tCurrentTask : &gMainTask;
    }

    std::chrono::steady_clock::time_point deadlineFor(TickType_t ticks) {
        return std::chrono::steady_clock::now() + std::chrono::milliseconds(ticks);
    }
} // namespace

BaseType_t xTaskCreate(TaskFunction_t entry, const char* name, uint32_t stackBytes, void* argument, UBaseType_t,
                       TaskHandle_t* handle) {
    auto* task = new RsvpTask;
    task->name = name != nullptr ? name : "task";
    task->entry = entry;
    task->argument = argument;
    if (handle != nullptr)
        *handle = task;

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    // ESP32 stacks are sized in bytes for a 32-bit target; give 64-bit hosts comfortable headroom.
    pthread_attr_setstacksize(&attributes, std::max<size_t>(stackBytes * 4U, 1024U * 1024U));
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    const int result = pthread_create(
        &thread, &attributes,
        [](void* context) -> void* {
            auto* self = static_cast<RsvpTask*>(context);
            tCurrentTask = self;
            pthread_setname_np(pthread_self(), self->name.substr(0, 15).c_str());
            self->entry(self->argument);
            return nullptr;
        },
        task);
    pthread_attr_destroy(&attributes);
    if (result != 0) {
        if (handle != nullptr)
            *handle = nullptr;
        delete task;
        return pdFAIL;
    }
    return pdPASS;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char* name, uint32_t stackBytes, void* argument,
                                   UBaseType_t priority, TaskHandle_t* handle, BaseType_t) {
    return xTaskCreate(entry, name, stackBytes, argument, priority, handle);
}

void vTaskDelete(TaskHandle_t task) {
    if (task == nullptr || task == tCurrentTask) {
        // Task objects are intentionally leaked: other threads may still hold their handle.
        pthread_exit(nullptr);
    }
    // Deleting another task is not supported by POSIX threads; the firmware only does this on shutdown.
}

void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks == 0 ? 0 : ticks));
}

TaskHandle_t xTaskGetCurrentTaskHandle() {
    return currentTask();
}

const char* pcTaskGetName(TaskHandle_t task) {
    return (task != nullptr ? task : currentTask())->name.c_str();
}

void xTaskNotifyGive(TaskHandle_t task) {
    if (task == nullptr)
        return;
    {
        const std::lock_guard lock{task->mutex};
        ++task->notifications;
    }
    task->condition.notify_all();
}

void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t* woken) {
    xTaskNotifyGive(task);
    if (woken != nullptr)
        *woken = pdFALSE;
}

uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticks) {
    RsvpTask* task = currentTask();
    std::unique_lock lock{task->mutex};
    if (task->notifications == 0 && ticks != 0) {
        if (ticks == portMAX_DELAY)
            task->condition.wait(lock, [task] { return task->notifications != 0; });
        else
            task->condition.wait_until(lock, deadlineFor(ticks), [task] { return task->notifications != 0; });
    }
    const uint32_t value = task->notifications;
    if (value != 0)
        task->notifications = clearOnExit ? 0 : value - 1;
    return value;
}

// ---------------------------------------------------------------------------------------------
// Queues
// ---------------------------------------------------------------------------------------------
struct RsvpQueue {
    size_t length = 0;
    size_t itemSize = 0;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::vector<uint8_t>> items;
};

QueueHandle_t xQueueCreate(size_t length, size_t itemSize) {
    auto* queue = new RsvpQueue;
    queue->length = length;
    queue->itemSize = itemSize;
    return queue;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticks) {
    if (queue == nullptr || item == nullptr)
        return pdFALSE;
    std::unique_lock lock{queue->mutex};
    auto hasSpace = [queue] { return queue->items.size() < queue->length; };
    if (!hasSpace()) {
        if (ticks == 0)
            return pdFALSE;
        if (ticks == portMAX_DELAY)
            queue->changed.wait(lock, hasSpace);
        else if (!queue->changed.wait_until(lock, deadlineFor(ticks), hasSpace))
            return pdFALSE;
    }
    const auto* bytes = static_cast<const uint8_t*>(item);
    queue->items.emplace_back(bytes, bytes + queue->itemSize);
    lock.unlock();
    queue->changed.notify_all();
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t ticks) {
    if (queue == nullptr || item == nullptr)
        return pdFALSE;
    std::unique_lock lock{queue->mutex};
    auto hasItem = [queue] { return !queue->items.empty(); };
    if (!hasItem()) {
        if (ticks == 0)
            return pdFALSE;
        if (ticks == portMAX_DELAY)
            queue->changed.wait(lock, hasItem);
        else if (!queue->changed.wait_until(lock, deadlineFor(ticks), hasItem))
            return pdFALSE;
    }
    std::memcpy(item, queue->items.front().data(), queue->itemSize);
    queue->items.pop_front();
    lock.unlock();
    queue->changed.notify_all();
    return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t queue) {
    if (queue == nullptr)
        return pdFALSE;
    {
        const std::lock_guard lock{queue->mutex};
        queue->items.clear();
    }
    queue->changed.notify_all();
    return pdPASS;
}

void vQueueDelete(QueueHandle_t queue) {
    delete queue;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
    if (queue == nullptr)
        return 0;
    const std::lock_guard lock{queue->mutex};
    return static_cast<UBaseType_t>(queue->items.size());
}

// ---------------------------------------------------------------------------------------------
// Semaphores
// ---------------------------------------------------------------------------------------------
struct RsvpSemaphore {
    std::mutex mutex;
    std::condition_variable condition;
    unsigned count = 0;
};

SemaphoreHandle_t xSemaphoreCreateMutex() {
    auto* semaphore = new RsvpSemaphore;
    semaphore->count = 1;
    return semaphore;
}

SemaphoreHandle_t xSemaphoreCreateBinary() {
    return new RsvpSemaphore;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    if (semaphore == nullptr)
        return pdFALSE;
    std::unique_lock lock{semaphore->mutex};
    auto available = [semaphore] { return semaphore->count > 0; };
    if (!available()) {
        if (ticks == 0)
            return pdFALSE;
        if (ticks == portMAX_DELAY)
            semaphore->condition.wait(lock, available);
        else if (!semaphore->condition.wait_until(lock, deadlineFor(ticks), available))
            return pdFALSE;
    }
    --semaphore->count;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (semaphore == nullptr)
        return pdFALSE;
    {
        const std::lock_guard lock{semaphore->mutex};
        semaphore->count = 1;
    }
    semaphore->condition.notify_one();
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    delete semaphore;
}

#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

inline void* heap_caps_malloc(size_t size, uint32_t) { return std::malloc(size); }
inline void* heap_caps_calloc(size_t n, size_t size, uint32_t) { return std::calloc(n, size); }
inline void* heap_caps_realloc(void* p, size_t size, uint32_t) { return std::realloc(p, size); }
inline void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
    return std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
}
inline void heap_caps_free(void* p) { std::free(p); }
size_t heap_caps_get_free_size(uint32_t caps);
inline size_t heap_caps_get_total_size(uint32_t caps) { return heap_caps_get_free_size(caps); }
inline size_t heap_caps_get_largest_free_block(uint32_t caps) { return heap_caps_get_free_size(caps); }
inline size_t heap_caps_get_minimum_free_size(uint32_t caps) { return heap_caps_get_free_size(caps); }
inline int heap_caps_monitor_local_minimum_free_size_start() { return 0; }
inline int heap_caps_monitor_local_minimum_free_size_stop() { return 0; }
using esp_alloc_failed_hook_t = void (*)(size_t, uint32_t, const char*);
inline int heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t) { return 0; }
inline bool heap_caps_check_integrity_all(bool) { return true; }

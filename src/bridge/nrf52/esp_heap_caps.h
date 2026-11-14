// nRF52 only: the ESP32 heap queries the bridge uses. This heap has no capability classes.
#pragma once
#include <cstddef>
#include <cstdint>

constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
// The SoftDevice has memory of its own, outside this heap.
#define BRIDGE_HEAP_RESERVE (8 * 1024)
size_t heap_caps_get_free_size(uint32_t caps);
// The allocator here does not report its largest block; the free total stands in for it.
inline size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    return heap_caps_get_free_size(caps);
}
inline size_t heap_caps_get_minimum_free_size(uint32_t)
{
    return 0;
}
inline void heap_caps_register_failed_alloc_callback(void (*)(size_t, uint32_t, const char *)) {}

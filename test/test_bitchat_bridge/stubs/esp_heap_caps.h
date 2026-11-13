#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
inline size_t fakeFree = 100000, fakeLargest = 90000;
inline size_t heap_caps_get_free_size(uint32_t)
{
    return fakeFree;
}
inline size_t heap_caps_get_largest_free_block(uint32_t)
{
    return fakeLargest;
}
inline size_t heap_caps_get_minimum_free_size(uint32_t)
{
    return fakeFree;
}
inline int heap_caps_register_failed_alloc_callback(void (*)(size_t, uint32_t, const char *))
{
    return 0;
}

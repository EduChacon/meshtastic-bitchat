#pragma once
#include <cassert>
#include <cstdlib>
#include <esp_heap_caps.h>
#include <new>

// Only nothrow bridge allocations consume the simulated internal heap.
static bool failAllocation = false;
static size_t allocationAttempt = 0, failAt = 0, liveBytes = 0, peakBytes = 0;
struct Allocation {
    void *pointer = nullptr;
    size_t bytes = 0;
};
static Allocation allocations[128];
void *operator new(size_t size)
{
    if (auto *p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new(size_t size, const std::nothrow_t &) noexcept
{
    ++allocationAttempt;
    const size_t cost = size + 16;
    if (failAllocation || (failAt && allocationAttempt == failAt) || cost > fakeFree || cost > fakeLargest)
        return nullptr;
    auto *p = std::malloc(size ? size : 1);
    if (!p)
        return nullptr;
    for (auto &entry : allocations)
        if (!entry.pointer) {
            entry = {p, cost};
            fakeFree -= cost;
            liveBytes += cost;
            if (peakBytes < liveBytes)
                peakBytes = liveBytes;
            return p;
        }
    std::free(p);
    return nullptr;
}
void operator delete(void *p) noexcept
{
    for (auto &entry : allocations)
        if (entry.pointer && entry.pointer == p) {
            fakeFree += entry.bytes;
            liveBytes -= entry.bytes;
            entry = {};
            break;
        }
    std::free(p);
}
void operator delete(void *p, size_t) noexcept
{
    ::operator delete(p);
}
void operator delete(void *p, const std::nothrow_t &) noexcept
{
    ::operator delete(p);
}

// Host stand-in for the firmware clock and its interval helper.
#pragma once
#include <cstdint>

extern uint32_t fakeMillis;
inline uint32_t millis()
{
    return fakeMillis;
}
namespace Throttle
{
inline bool isWithinTimespanMs(uint32_t since, uint32_t span)
{
    return uint32_t(millis() - since) < span;
}
} // namespace Throttle

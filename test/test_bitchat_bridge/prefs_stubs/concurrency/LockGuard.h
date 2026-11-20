// Host stand-in: one thread, nothing to lock.
#pragma once
namespace concurrency
{
struct LockGuard {
    template <typename Lock> explicit LockGuard(Lock *) {}
};
} // namespace concurrency

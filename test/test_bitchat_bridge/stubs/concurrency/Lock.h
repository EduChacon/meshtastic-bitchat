#pragma once
#include <mutex>
namespace concurrency
{
class Lock
{
  public:
    void lock() { mutex.lock(); }
    void unlock() { mutex.unlock(); }

  private:
    std::mutex mutex;
};
} // namespace concurrency

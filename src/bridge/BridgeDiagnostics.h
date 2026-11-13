#pragma once
#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#ifdef MESHTASTIC_ENABLE_BITCHAT
namespace bitchat {
void setupBootDiagnostics();
void bootDiagnostic(const char *stage);
bool memoryAvailable(size_t total, size_t largest);
void memoryRequest(const char *stage, size_t total, size_t largest);

template <typename T> bool allocateBuffer(std::unique_ptr<T> &buffer) {
  if (buffer)
    return true;
  constexpr size_t overhead = 256;
  if (!memoryAvailable(sizeof(T) + overhead,
                       std::max(sizeof(T) + overhead, size_t(8192))))
    return false;
  buffer.reset(new (std::nothrow) T);
  if (buffer && memoryAvailable(0, 8192))
    return true;
  buffer.reset();
  return false;
}
} // namespace bitchat
#endif

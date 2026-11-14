#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#ifdef MESHTASTIC_ENABLE_BITCHAT
namespace bitchat
{
void setupBootDiagnostics();
void bootDiagnostic(const char *stage);
bool memoryAvailable(size_t total, size_t largest);
void memoryRequest(const char *stage, size_t total, size_t largest);

template <typename T> bool allocateBuffer(std::unique_ptr<T> &buffer)
{
    if (buffer)
        return true;
    constexpr size_t overhead = 256;
    if (!memoryAvailable(sizeof(T) + overhead, std::max(sizeof(T) + overhead, size_t(8192))))
        return false;
    buffer.reset(new (std::nothrow) T);
    if (buffer && memoryAvailable(0, 8192))
        return true;
    buffer.reset();
    return false;
}

struct ByteDeleter {
    void operator()(uint8_t *bytes) const { ::operator delete(bytes); }
};
using ByteBuffer = std::unique_ptr<uint8_t, ByteDeleter>;

// Exact-size payloads keep a deep queue of short chat frames within the same heap budget.
inline bool allocateBytes(ByteBuffer &buffer, size_t size)
{
    constexpr size_t overhead = 64;
    buffer.reset();
    if (!size || !memoryAvailable(size + overhead, std::max(size + overhead, size_t(8192))))
        return false;
    buffer.reset(static_cast<uint8_t *>(::operator new(size, std::nothrow)));
    if (buffer && memoryAvailable(0, 8192))
        return true;
    buffer.reset();
    return false;
}
} // namespace bitchat
#endif

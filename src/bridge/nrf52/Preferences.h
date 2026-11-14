// nRF52 only: the ESP32 key-value calls the bridge uses, kept in one small file on the internal filesystem.
#pragma once
#include <cstddef>
#include <cstdint>

class Preferences
{
  public:
    bool begin(const char *name, bool readOnly);
    bool isKey(const char *key);
    size_t getBytesLength(const char *key);
    size_t getBytes(const char *key, void *out, size_t size);
    size_t putBytes(const char *key, const void *in, size_t size);
};

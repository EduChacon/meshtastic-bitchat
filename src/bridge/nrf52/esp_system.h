// nRF52 only: the ESP32 random calls the bridge uses, served by the firmware's hardware source.
#pragma once
#include <cstddef>
#include <cstdint>

uint32_t esp_random();
void esp_fill_random(void *out, size_t size);

#ifdef MESHTASTIC_ENABLE_BITCHAT
#include "BridgeDiagnostics.h"
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>

namespace bitchat {
static constexpr uint32_t CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
static constexpr size_t RESERVE = 24 * 1024;

static void allocationFailed(size_t size, uint32_t caps, const char *) {
  esp_rom_printf("[bridge-memory] allocation failed bytes=%u caps=%u\n",
                 unsigned(size), unsigned(caps));
}

void setupBootDiagnostics() {
  heap_caps_register_failed_alloc_callback(allocationFailed);
  bootDiagnostic("modules");
}

void bootDiagnostic(const char *stage) {
  esp_rom_printf("[bridge-boot] %s free=%u largest=%u min=%u\n", stage,
                 unsigned(heap_caps_get_free_size(CAPS)),
                 unsigned(heap_caps_get_largest_free_block(CAPS)),
                 unsigned(heap_caps_get_minimum_free_size(CAPS)));
}

void memoryRequest(const char *stage, size_t total, size_t largest) {
  esp_rom_printf("[bridge-memory] %s need=%u block=%u reserve=%u\n", stage,
                 unsigned(total), unsigned(largest), unsigned(RESERVE));
  bootDiagnostic(stage);
}

bool memoryAvailable(size_t total, size_t largest) {
  const size_t available = heap_caps_get_free_size(CAPS);
  return available >= RESERVE && total <= available - RESERVE &&
         heap_caps_get_largest_free_block(CAPS) >= largest;
}
} // namespace bitchat
#endif

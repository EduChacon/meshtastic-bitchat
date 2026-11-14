// nRF52 only: boot diagnostics go nowhere here; the serial console of this target is the firmware log.
#pragma once
inline void esp_rom_printf(const char *, ...) {}

#pragma once
// The bridge asks one object whether Bluetooth is up and to redo its advertising.
#if defined(ARCH_NRF52)
namespace bitchat
{
struct PlatformBluetooth {
    bool isDeInit = false, startupFailed = false, up = false;
    bool isActive() const { return up; }
    void refreshBridgeAdvertising();
};
// Implemented beside the firmware's own Bluetooth code, which owns the Meshtastic service.
void nativeAdvertising();
void nativeScanResponse();
void platformAttach();
// The firmware's main task has a 4 KB stack here, less than one bridge frame buffer needs. The work
// runs in a task with a larger one while the main task waits, so nothing runs at the same time.
bool onLargeStack(void (*work)(void *), void *argument);
} // namespace bitchat
extern bitchat::PlatformBluetooth *nimbleBluetooth;
#else
#include "nimble/NimbleBluetooth.h"
namespace bitchat
{
inline bool onLargeStack(void (*work)(void *), void *argument)
{
    work(argument);
    return true;
}
} // namespace bitchat
#endif

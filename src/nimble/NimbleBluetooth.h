#pragma once
#include "BluetoothCommon.h"

class NimbleBluetooth : BluetoothApi
{
  public:
    void setup();
    void shutdown();
    void deinit();
    void clearBonds();
    bool isActive();
    bool isConnected();
    int getRssi();
    void sendLog(const uint8_t *logMessage, size_t length);
#if defined(NIMBLE_TWO)
    void startAdvertising();
#endif
    bool isDeInit = false;
#ifdef MESHTASTIC_ENABLE_BITCHAT
    void refreshBridgeAdvertising() { startAdvertising(); }
#endif

  private:
    void setupService();
#if !defined(NIMBLE_TWO)
    void startAdvertising();
#endif
};

void setBluetoothEnable(bool enable);
void clearNVS();

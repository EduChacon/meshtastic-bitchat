#include "HeapModel.h"
#include "bridge/BridgeBle.h"
#include "bridge/BridgeDiagnostics.h"
#include "bridge/BridgeState.h"
#include "bridge/BridgeTunnel.h"
#include <NimBLEDevice.h>
#include <cstdio>
using namespace bitchat;
#include "legacy_memory_layout.h"
struct Bluetooth {
  bool active = true, isDeInit = false;
  bool isActive() const { return active; }
} bluetooth;
Bluetooth *nimbleBluetooth = &bluetooth;
class BitchatBridgeModule {
public:
#include "runtime-layout.inc"
  std::unique_ptr<Runtime> runtime;
  bool resourceError = false;
  bool ensureRuntime();
  void releaseRuntime();
};
#include "runtime-code.inc"

int main() {
  NimBLEServer server;
  bleAttach(&server);
  BitchatBridgeModule module;
  const size_t oldTotal = sizeof(legacy::Runtime) + sizeof(legacy::Links);
  const size_t oldLargest =
      std::max(sizeof(legacy::Runtime), sizeof(legacy::Links));
  fakeFree = 64 * 1024;
  fakeLargest = 16 * 1024;
  assert(!memoryAvailable(oldTotal, oldLargest));
  assert(module.ensureRuntime());
  assert(module.runtime->peers && module.runtime->dedup);
  assert(!module.runtime->fragments && !module.runtime->tunnelFragments);
  const auto newTotal = liveBytes;
  assert(newTotal < 25 * 1024);
  assert(fakeFree >= 24 * 1024);
  assert(module.ensureRuntime() && liveBytes == newTotal);
  module.releaseRuntime();
  assert(liveBytes == 0 && fakeFree == 64 * 1024);

  for (size_t failure = 1; failure <= 4; ++failure) {
    allocationAttempt = 0;
    failAt = failure;
    module.resourceError = false;
    assert(!module.ensureRuntime() && module.resourceError);
    assert(!module.runtime && liveBytes == 0 && fakeFree == 64 * 1024);
  }
  failAt = 0;
  fakeLargest = 8192;
  assert(!module.ensureRuntime() && liveBytes == 0);
  fakeLargest = 16384;
  fakeFree = 40 * 1024;
  assert(!module.ensureRuntime() && liveBytes == 0);
  fakeFree = 64 * 1024;
  bluetooth.active = false;
  assert(!module.ensureRuntime() && liveBytes == 0);
  bluetooth.active = true;
  bluetooth.isDeInit = true;
  assert(!module.ensureRuntime() && liveBytes == 0);
  bluetooth.isDeInit = false;

  for (unsigned cycle = 0; cycle < 200; ++cycle) {
    assert(module.ensureRuntime());
    assert(allocateBuffer(module.runtime->fragments));
    assert(allocateBuffer(module.runtime->tunnelFragments));
    assert(module.runtime->fragments->empty() &&
           module.runtime->tunnelFragments->empty());
    bleConfigure(true, false, false);
    bleConnect(7);
    ble_gap_conn_desc peer{7};
    auto &c = server.service.characteristic;
    c.callbacks->onSubscribe(&c, &peer, 1);
    Frame frame;
    const uint8_t body[] = {'h', 'i'};
    assert(encode(2, PeerId{1}, nullptr, 1, 7, {body, sizeof(body)}, {},
                  frame) == Result::Ok);
    assert(bleBroadcast(frame.bytes()));
    blePoll();
    c.value.assign(reinterpret_cast<const char *>(frame.data.data()),
                   frame.size);
    c.callbacks->onWrite(&c, &peer);
    BleInput received;
    assert(bleReceive(received));
    assert(equalConstantTime(frame.bytes(), received.frame.bytes()));
    bleDisconnect(7);
    module.releaseRuntime();
    assert(liveBytes == 0 && fakeFree == 64 * 1024);
  }
  bleDetach();
  printf("Memory host model: legacy startup=%zu largest=%zu; new startup=%zu "
         "(including modeled heap overhead), peak=%zu. PASS; not device "
         "measurements.\n",
         oldTotal, oldLargest, newTotal, peakBytes);
}

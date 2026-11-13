#include "bridge/BridgeBle.h"
#include "bridge/BridgeDiagnostics.h"
#include <NimBLEDevice.h>
#include <cassert>
#include <cstdio>
#include <esp_heap_caps.h>
#include <limits>
#include <new>

static bool failAllocation = false;
void *operator new(size_t size, const std::nothrow_t &) noexcept
{
    if (failAllocation)
        return nullptr;
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void *ptr, const std::nothrow_t &) noexcept
{
    ::operator delete(ptr);
}

using namespace bitchat;

int main()
{
    fakeFree = 24 * 1024 - 1;
    assert(!memoryAvailable(0, 0));
    fakeFree = 24 * 1024;
    assert(memoryAvailable(0, 0));
    assert(!memoryAvailable(1, 0));
    assert(!memoryAvailable(std::numeric_limits<size_t>::max(), 0));
    fakeFree = 100000;
    fakeLargest = 1024;
    assert(!memoryAvailable(2048, 2048));
    fakeLargest = 90000;

    BleInput received;
    assert(!bleAllocateBuffers());
    assert(!bleReceive(received));
    assert(bleSubscribers() == 0);
    NimBLEServer server;
    bleAttach(&server);
    bleConnect(7);
    bleConnect(7);
    bleConfigure(false, false, false);
    assert(!bleAdvertise());
    assert(!bleReceive(received));
    assert(bleBufferSize() > 30000);
    fakeFree = 30000;
    assert(!bleAllocateBuffers());
    bleConfigure(true, false, false);
    assert(!bleAdvertise());
    fakeFree = 100000;
    failAllocation = true;
    assert(!bleAllocateBuffers());
    failAllocation = false;
    assert(bleAllocateBuffers());
    assert(bleAllocateBuffers());
    bleConfigure(true, false, false);
    assert(bleAdvertise());

    auto &c = server.service.characteristic;
    ble_gap_conn_desc a{7}, b{8}, d{9};
    bleConnect(8);
    bleConnect(9);
    c.callbacks->onSubscribe(&c, &a, 1);
    c.callbacks->onSubscribe(&c, &b, 1);
    c.callbacks->onSubscribe(&c, &d, 1);
    assert(bleSubscribers() == 3);
    Frame frame;
    const uint8_t body[] = {'h', 'i'};
    assert(encode(2, PeerId{1}, nullptr, 1, 7, {body, sizeof(body)}, {}, frame) == Result::Ok);
    const size_t half = frame.size / 2;
    c.value.assign(reinterpret_cast<const char *>(frame.data.data()), half);
    c.callbacks->onWrite(&c, &a);
    assert(!bleReceive(received));
    c.value.assign(reinterpret_cast<const char *>(frame.data.data() + half), frame.size - half);
    c.callbacks->onWrite(&c, &a);
    assert(bleReceive(received));
    assert(received.link == 7 && equalConstantTime(received.frame.bytes(), frame.bytes()));

    c.value.assign(reinterpret_cast<const char *>(frame.data.data()), frame.size);
    const auto before = bleDrops();
    for (int i = 0; i < 3; ++i)
        c.callbacks->onWrite(&c, &a);
    c.callbacks->onWrite(&c, &b);
    assert(bleDrops() == before + 1);
    assert(bleReceive(received) && received.link == 8);
    assert(bleReceive(received) && received.link == 7);
    assert(bleReceive(received) && received.link == 7);
    assert(!bleReceive(received));
    assert(bleBroadcast(frame.bytes(), 8));
    blePoll();
    assert(fakeSent.size() == 2 && fakeSent[0].first == 7 && fakeSent[1].first == 9);
    assert(fakeSent[0].second == std::vector<uint8_t>(frame.data.begin(), frame.data.begin() + frame.size));

    assert(bleBroadcast(frame.bytes()));
    fakeNotifyResult = BLE_HS_ENOMEM;
    blePoll();
    assert(fakeSent.size() == 2);
    fakeNow += 10001;
    blePoll();
    assert(server.disconnected.size() == 3);
    bleReleaseBuffers();
    c.callbacks->onWrite(&c, &a);
    c.callbacks->onSubscribe(&c, &a, 1);
    bleDisconnect(7);
    assert(!bleReceive(received) && bleSubscribers() == 0 && !bleAdvertise());
    assert(bleAllocateBuffers());
    bleConfigure(true, false, false);
    assert(!bleReceive(received));
    assert(bleSubscribers() == 2);
    assert(bleBroadcast(frame.bytes()));
    fakeNotifyResult = 0;
    const auto sentBeforeResume = fakeSent.size();
    blePoll();
    assert(fakeSent.size() == sentBeforeResume + 2);
    bleDisconnect(8);
    assert(bleSubscribers() == 1);
    bleDetach();
    blePoll();
    assert(!bleAllocateBuffers());
    assert(!bleBroadcast(frame.bytes()));
    std::puts("BLE adapter lifecycle, allocation failure, queues, fairness and expiry: PASS (host fakes, no hardware)");
}

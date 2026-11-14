#include "bridge/BridgeBle.h"
#include "bridge/BridgeDiagnostics.h"
#include <NimBLEDevice.h>
#include <cassert>
#include <cstdio>
#include <esp_heap_caps.h>
#include <limits>
#include <new>

#include "HeapModel.h"

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
    assert(bleBufferSize() < 1024);
    fakeFree = 24 * 1024;
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
    // A phone bursts several frames when it connects: six wait their turn, the seventh is counted as lost.
    for (int i = 0; i < 7; ++i)
        c.callbacks->onWrite(&c, &a);
    c.callbacks->onWrite(&c, &b);
    assert(bleDrops() == before + 1);
    assert(bleReceive(received) && received.link == 8);
    for (int i = 0; i < 6; ++i)
        assert(bleReceive(received) && received.link == 7);
    assert(!bleReceive(received));
    // A whole frame replaces a stalled partial one instead of being glued to it.
    c.value.assign(reinterpret_cast<const char *>(frame.data.data()), half);
    c.callbacks->onWrite(&c, &a);
    c.value.assign(reinterpret_cast<const char *>(frame.data.data()), frame.size);
    c.callbacks->onWrite(&c, &a);
    assert(bleDrops() == before + 2);
    assert(bleReceive(received) && equalConstantTime(received.frame.bytes(), frame.bytes()));
    assert(!bleReceive(received));
    // Padding is accepted on any frame type when the write holds the whole frame.
    Frame padded;
    assert(encode(2, PeerId{1}, nullptr, 1, 7, {body, sizeof(body)}, {}, padded, true) == Result::Ok && padded.size == 256);
    c.value.assign(reinterpret_cast<const char *>(padded.data.data()), padded.size);
    c.callbacks->onWrite(&c, &a);
    assert(bleReceive(received) && equalConstantTime(received.frame.bytes(), padded.bytes()));
    c.value.assign("\x03junk-not-a-frame", 18);
    c.callbacks->onWrite(&c, &a);
    assert(bleDrops() == before + 3 && !bleReceive(received));
    assert(bleBroadcast(frame.bytes(), 8));
    blePoll();
    assert(fakeSent.size() == 2 && fakeSent[0].first == 7 && fakeSent[1].first == 9);
    assert(fakeSent[0].second == std::vector<uint8_t>(frame.data.begin(), frame.data.begin() + frame.size));
    // Six short frames queue per phone without reserving full-size buffers; the seventh is refused.
    const size_t quiet = liveBytes;
    for (int i = 0; i < 6; ++i)
        assert(bleBroadcast(frame.bytes()));
    assert(liveBytes == quiet + 6 * (frame.size + 16));
    assert(!bleBroadcast(frame.bytes()));
    blePoll();
    assert(fakeSent.size() == 2 + 3 * 2);
    blePoll();
    blePoll();
    assert(fakeSent.size() == 2 + 3 * 6 && liveBytes == quiet);
    fakeSent.resize(2);

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
    assert(liveBytes == 0);
    fakeFree = 64 * 1024;
    fakeLargest = 16 * 1024;
    bleAttach(&server);
    assert(bleAllocateBuffers());
    bleConfigure(true, false, false);
    for (auto desc : {a, b, d}) {
        bleConnect(desc.conn_handle);
        c.callbacks->onSubscribe(&c, &desc, 1);
    }
    const size_t idle = liveBytes;
    std::array<uint8_t, 1700> bodyLarge{};
    for (size_t i = 0; i < bodyLarge.size(); ++i)
        bodyLarge[i] = uint8_t(i);
    assert(encode(2, PeerId{1}, nullptr, 1, 7, {bodyLarge.data(), bodyLarge.size()}, {}, frame) == Result::Ok);
    fakeSent.clear();
    assert(bleBroadcast(frame.bytes()));
    // Broadcast retains one payload across three independent notification
    // streams.
    assert(liveBytes == idle + frame.size + 16);
    blePoll();
    // The app drops a frame whose pieces arrive 250 ms apart: one poll sends two of them to each phone.
    assert(fakeSent.size() == 3 * 2 && fakeSent[1].first == 7 && fakeSent[2].first == 8);
    bleDisconnect(8);
    while (liveBytes > idle)
        blePoll();
    for (auto handle : {7, 9}) {
        std::vector<uint8_t> wire;
        for (auto &part : fakeSent)
            if (part.first == handle)
                wire.insert(wire.end(), part.second.begin(), part.second.end());
        assert(wire == std::vector<uint8_t>(frame.data.begin(), frame.data.begin() + frame.size));
    }
    bleConnect(8);
    c.callbacks->onSubscribe(&c, &b, 1);
    auto write = [&](ble_gap_conn_desc &desc) {
        for (size_t offset = 0; offset < frame.size; offset += 244) {
            c.value.assign(reinterpret_cast<const char *>(frame.data.data() + offset),
                           std::min(size_t(244), frame.size - offset));
            c.callbacks->onWrite(&c, &desc);
        }
    };
    for (auto desc : {a, b, d}) {
        write(desc);
        write(desc);
    }
    // Large frames stay bounded by the pool: nine of these fit in the bytes eight full-size frames used to take.
    for (int i = 0; i < 3; ++i)
        assert(bleBroadcast(frame.bytes()));
    assert(liveBytes == idle + 9 * (frame.size + 16) && 9 * frame.size <= 8 * MAX_FRAME);
    assert(!bleBroadcast(frame.bytes()));
    for (unsigned i = 0; i < 6; ++i) {
        assert(bleReceive(received));
        assert(equalConstantTime(received.frame.bytes(), frame.bytes()));
    }
    assert(!bleReceive(received));
    while (liveBytes > idle)
        blePoll();
    // Partial input expires even when its sender never writes again.
    c.value.assign(reinterpret_cast<const char *>(frame.data.data()), 20);
    c.callbacks->onWrite(&c, &a);
    assert(liveBytes > idle);
    fakeNow += 3001;
    blePoll();
    assert(liveBytes == idle);
    failAllocation = true;
    assert(!bleBroadcast(frame.bytes()));
    c.callbacks->onWrite(&c, &a);
    assert(liveBytes == idle);
    failAllocation = false;
    write(a);
    assert(bleReceive(received) && equalConstantTime(received.frame.bytes(), frame.bytes()));
    bleDetach();
    assert(liveBytes == 0);
    std::puts("BLE adapter lifecycle, allocation failure, queues, fairness and "
              "expiry: PASS (host fakes, no hardware)");
}

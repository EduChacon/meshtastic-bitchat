#ifdef MESHTASTIC_ENABLE_BITCHAT
#include "BridgeBle.h"
#include "BridgeDiagnostics.h"
#include "BridgeState.h"
#include "concurrency/Lock.h"
#include "concurrency/LockGuard.h"
#include "nimble/nimble/host/include/host/ble_gatt.h"
#include "nimble/nimble/host/include/host/ble_hs.h"
#include "nimble/nimble/host/services/gatt/include/services/gatt/ble_svc_gatt.h"
#include <NimBLEDevice.h>
#include <atomic>
#include <memory>
#include <new>

namespace bitchat
{
static constexpr const char *SERVICE_UUID = "F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C";
static constexpr const char *CHAR_UUID = "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D";
static constexpr size_t LINKS = 3;
static concurrency::Lock lock;
static NimBLEServer *server = nullptr;
static NimBLECharacteristic *characteristic = nullptr;
static std::atomic<bool> enabled{false}, nativeWindow{false}, economy{false}, announceNeeded{false};
static std::atomic<uint32_t> dropped{0};
struct Connection {
    uint16_t handle = NO_LINK;
    bool subscribed = false;
};
static std::array<Connection, LINKS> connections;
static constexpr uint8_t NO_FRAME = 255;
static constexpr size_t QUEUE = 6;
// Three phones at two pieces each use half of NimBLE's twelve shared buffers per poll.
static constexpr unsigned BURST = 2;
struct Outgoing {
    uint8_t frame = NO_FRAME;
    uint16_t offset = 0;
    uint32_t started = 0;
};
struct Link {
    uint16_t handle = NO_LINK;
    bool subscribed = false;
    // A bonded phone's old subscription returns by itself; only a write shows the
    // Bitchat app is there.
    bool talked = false;
    uint32_t generation = 0;
    uint32_t receivedAt = 0;
    uint8_t incoming = NO_FRAME;
    BoundedQueue<uint8_t, QUEUE> received;
    BoundedQueue<Outgoing, QUEUE> sent;
};
struct Links : std::array<Link, LINKS> {
    struct Slot {
        ByteBuffer data;
        uint16_t size = 0, capacity = 0;
        uint8_t references = 0;
        Bytes bytes() const { return {data.get(), size}; }
    };
    // Same worst case as eight full frames, shared by many more short ones.
    static constexpr size_t POOL_BYTES = 8 * MAX_FRAME;
    std::array<Slot, 16> frames;
    size_t pooled = 0;
    uint8_t allocate(size_t capacity)
    {
        if (!capacity || capacity > MAX_FRAME || capacity > POOL_BYTES - pooled)
            return NO_FRAME;
        for (uint8_t i = 0; i < frames.size(); ++i) {
            auto &slot = frames[i];
            if (!slot.references) {
                if (!allocateBytes(slot.data, capacity))
                    return NO_FRAME;
                slot.size = 0;
                slot.capacity = uint16_t(capacity);
                slot.references = 1;
                pooled += capacity;
                return i;
            }
        }
        return NO_FRAME;
    }
    uint8_t store(Bytes frame)
    {
        const uint8_t index = allocate(frame.size);
        if (index != NO_FRAME) {
            std::copy_n(frame.data, frame.size, frames[index].data.get());
            frames[index].size = uint16_t(frame.size);
        }
        return index;
    }
    Slot &get(uint8_t index) { return frames[index]; }
    void retain(uint8_t index) { ++frames[index].references; }
    void release(uint8_t index)
    {
        if (index != NO_FRAME && --frames[index].references == 0) {
            pooled -= frames[index].capacity;
            frames[index].data.reset();
            frames[index].size = frames[index].capacity = 0;
        }
    }
    void clearSent(Link &link)
    {
        while (auto *message = link.sent.front()) {
            release(message->frame);
            link.sent.pop();
        }
    }
    void clear(Link &link)
    {
        release(link.incoming);
        link.incoming = NO_FRAME;
        while (auto *frame = link.received.front()) {
            release(*frame);
            link.received.pop();
        }
        clearSent(link);
        link.talked = false;
    }
};
static_assert(sizeof(Links) < 1024, "BLE startup must not reserve per-connection payloads");
static std::unique_ptr<Links> links;
static size_t nextReceive = 0;
static uint32_t nextGeneration = 0;

static Link *findLink(uint16_t handle)
{
    if (!links)
        return nullptr;
    for (auto &link : *links)
        if (link.handle == handle)
            return &link;
    return nullptr;
}

class Callbacks : public NimBLECharacteristicCallbacks
{
    static void dropIncoming(Link &link)
    {
        links->release(link.incoming);
        link.incoming = NO_FRAME;
        ++dropped;
    }
    static void deliver(Link &link, uint8_t frame)
    {
        if (!link.received.push(frame)) {
            links->release(frame);
            ++dropped;
        }
    }
    void onWrite(NimBLECharacteristic *c, ble_gap_conn_desc *desc) override
    {
        if (!enabled || !desc)
            return;
        const auto value = c->getValue();
        const Bytes chunk{reinterpret_cast<const uint8_t *>(value.data()), value.size()};
        if (!chunk.size)
            return;
        concurrency::LockGuard guard(&lock);
        Link *link = findLink(desc->conn_handle);
        if (!link) {
            ++dropped;
            return;
        }
        link->talked = true;
        Packet whole;
        if (decode(chunk, whole) == Result::Ok) {
            // Bitchat sends one frame per write, so a whole frame ends any stalled partial stream.
            if (link->incoming != NO_FRAME)
                dropIncoming(*link);
            const uint8_t frame = links->store(chunk);
            if (frame == NO_FRAME)
                ++dropped;
            else
                deliver(*link, frame);
            return;
        }
        if (link->incoming != NO_FRAME && !Throttle::isWithinTimespanMsAt(link->receivedAt, 3000, millis()))
            dropIncoming(*link);
        size_t length = 0;
        if (link->incoming == NO_FRAME) {
            const Result start = wireFrameLength(chunk, length);
            if (start != Result::Ok && start != Result::Incomplete) {
                ++dropped;
                return;
            }
            link->incoming = links->allocate(start == Result::Ok ? length : MAX_FRAME);
            if (link->incoming == NO_FRAME) {
                ++dropped;
                return;
            }
            link->receivedAt = millis();
        }
        auto &incoming = links->get(link->incoming);
        if (chunk.size > size_t(incoming.capacity - incoming.size)) {
            dropIncoming(*link);
            return;
        }
        std::copy_n(chunk.data, chunk.size, incoming.data.get() + incoming.size);
        incoming.size += uint16_t(chunk.size);
        const Result result = wireFrameLength(incoming.bytes(), length);
        if (result == Result::Incomplete)
            return;
        if (result != Result::Ok || incoming.size > length) {
            dropIncoming(*link);
        } else if (incoming.size == length) {
            deliver(*link, link->incoming);
            link->incoming = NO_FRAME;
        }
    }
    void onSubscribe(NimBLECharacteristic *, ble_gap_conn_desc *desc, uint16_t value) override
    {
        if (!desc)
            return;
        concurrency::LockGuard guard(&lock);
        for (auto &connection : connections)
            if (connection.handle == desc->conn_handle)
                connection.subscribed = (value & 1) != 0;
        if (auto *link = findLink(desc->conn_handle)) {
            link->subscribed = (value & 1) != 0;
            announceNeeded = true;
        }
    }
};
static Callbacks callbacks;

void bleAttach(NimBLEServer *s)
{
    concurrency::LockGuard guard(&lock);
    server = s;
    auto *service = server->createService(SERVICE_UUID);
    characteristic = service->createCharacteristic(
        CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY, 512);
    characteristic->setCallbacks(&callbacks);
    service->start();
}

void bleDetach()
{
    enabled = false;
    concurrency::LockGuard guard(&lock);
    server = nullptr;
    characteristic = nullptr;
    links.reset();
    connections = {};
}

size_t bleBufferSize()
{
    return sizeof(Links);
}

bool bleAllocateBuffers()
{
    concurrency::LockGuard guard(&lock);
    if (!server || !characteristic)
        return false;
    if (links)
        return true;
    if (!allocateBuffer(links))
        return false;
    for (size_t index = 0; index < LINKS; ++index) {
        auto &link = (*links)[index];
        link.generation = ++nextGeneration;
        link.handle = connections[index].handle;
        link.subscribed = connections[index].subscribed;
    }
    nextReceive = 0;
    return true;
}

void bleReleaseBuffers()
{
    enabled = false;
    concurrency::LockGuard guard(&lock);
    links.reset();
    announceNeeded = false;
}

void bleConnect(uint16_t handle)
{
    {
        concurrency::LockGuard guard(&lock);
        for (const auto &connection : connections)
            if (connection.handle == handle)
                return;
        for (size_t index = 0; index < LINKS; ++index) {
            if (connections[index].handle == NO_LINK) {
                connections[index] = {handle, false};
                if (links) {
                    auto &link = (*links)[index];
                    link.handle = handle;
                    link.subscribed = false;
                    link.generation = ++nextGeneration;
                    links->clear(link);
                }
                break;
            }
        }
    }
    if (enabled)
        NimBLEDevice::getAdvertising()->start();
}

void bleDisconnect(uint16_t handle)
{
    concurrency::LockGuard guard(&lock);
    for (auto &connection : connections)
        if (connection.handle == handle)
            connection = {};
    if (auto *link = findLink(handle)) {
        link->handle = NO_LINK;
        link->subscribed = false;
        links->clear(*link);
    }
}

void bleConfigure(bool on, bool native, bool lowPower)
{
    concurrency::LockGuard guard(&lock);
    enabled = on && bool(links);
    nativeWindow = native;
    economy = lowPower;
    if (!on && links) {
        for (auto &link : *links) {
            links->clear(link);
        }
    }
}

bool bleAdvertise()
{
    if (!enabled || nativeWindow)
        return false;
    auto *advertising = NimBLEDevice::getAdvertising();
    advertising->stop();
    advertising->reset();
    NimBLEAdvertisementData primary, response;
    primary.setFlags(0x06);
    primary.setCompleteServices(NimBLEUUID(SERVICE_UUID));
    response.setName("BC Mesh Bridge");
    advertising->setAdvertisementData(primary);
    advertising->setScanResponseData(response);
    advertising->setScanResponse(true);
    // Apple-listed intervals: 152.5 ms fixed, 1022.5 ms economy.
    advertising->setMinInterval(economy ? 1636 : 244);
    advertising->setMaxInterval(economy ? 1636 : 244);
    advertising->start();
    return true;
}

bool bleReceive(BleInput &output)
{
    concurrency::LockGuard guard(&lock);
    if (!enabled || !links)
        return false;
    for (size_t offset = 0; offset < LINKS; ++offset) {
        size_t index = (nextReceive + offset) % LINKS;
        auto &link = (*links)[index];
        if (auto *frame = link.received.front()) {
            const auto &slot = links->get(*frame);
            std::copy_n(slot.data.get(), slot.size, output.frame.data.begin());
            output.frame.size = slot.size;
            output.link = link.handle;
            links->release(*frame);
            link.received.pop();
            nextReceive = (index + 1) % LINKS;
            return true;
        }
    }
    return false;
}

bool bleBroadcast(Bytes frame, uint16_t exclude)
{
    if (!enabled || !frame.size || frame.size > MAX_FRAME)
        return false;
    concurrency::LockGuard guard(&lock);
    if (!links || !enabled)
        return false;
    bool recipient = false;
    for (const auto &link : *links)
        recipient |= link.subscribed && link.handle != exclude && link.handle != NO_LINK;
    if (!recipient)
        return true;
    const uint8_t slot = links->store(frame);
    if (slot == NO_FRAME) {
        ++dropped;
        return false;
    }
    Outgoing message{slot, 0, millis()};
    bool accepted = true;
    for (auto &link : *links) {
        if (link.subscribed && link.handle != exclude && link.handle != NO_LINK) {
            if (link.sent.push(message))
                links->retain(slot);
            else {
                accepted = false;
                ++dropped;
            }
        }
    }
    links->release(slot);
    return accepted;
}

// Sends the next piece of one link's oldest frame; false when that link has nothing more to send now.
static bool sendChunk(size_t index)
{
    uint16_t handle = NO_LINK, attribute = 0;
    uint32_t generation = 0;
    std::array<uint8_t, 512> chunk;
    size_t count = 0;
    bool expired = false;
    {
        concurrency::LockGuard guard(&lock);
        if (!links || !enabled || !server || !characteristic)
            return false;
        auto &link = (*links)[index];
        if (link.incoming != NO_FRAME && !Throttle::isWithinTimespanMsAt(link.receivedAt, 3000, millis())) {
            links->release(link.incoming);
            link.incoming = NO_FRAME;
            ++dropped;
        }
        auto *message = link.sent.front();
        if (!message)
            return false;
        if (!link.subscribed || link.handle == NO_LINK) {
            links->clearSent(link);
            return false;
        }
        handle = link.handle;
        generation = link.generation;
        expired = !Throttle::isWithinTimespanMsAt(message->started, 10000, millis());
        if (expired) {
            links->clearSent(link);
            ++dropped;
        } else {
            const auto &payload = links->get(message->frame);
            const size_t mtu = server->getPeerMTU(handle);
            count = std::min({size_t(payload.size - message->offset), mtu > 3 ? mtu - 3 : size_t(20), chunk.size()});
            std::copy_n(payload.data.get() + message->offset, count, chunk.begin());
            attribute = characteristic->getHandle();
        }
    }
    if (expired) {
        // Discard incomplete receiver stream before sending another frame.
        if (server)
            server->disconnect(handle);
        return false;
    }
    auto *buffer = ble_hs_mbuf_from_flat(chunk.data(), count);
    if ((buffer ? ble_gatts_notify_custom(handle, attribute, buffer) : BLE_HS_ENOMEM) != 0)
        return false;
    concurrency::LockGuard guard(&lock);
    if (!links)
        return false;
    auto &link = (*links)[index];
    if (link.handle != handle || link.generation != generation)
        return false;
    if (auto *message = link.sent.front()) {
        message->offset += uint16_t(count);
        if (message->offset == links->get(message->frame).size) {
            links->release(message->frame);
            link.sent.pop();
        }
    }
    return true;
}

void blePoll()
{
    if (!enabled)
        return;
    // Bitchat discards a frame whose pieces arrive more than 250 ms apart, so each poll sends more than one.
    for (size_t index = 0; index < LINKS; ++index)
        for (unsigned sent = 0; sent < BURST && sendChunk(index); ++sent) {
        }
}

size_t bleSubscribers()
{
    concurrency::LockGuard guard(&lock);
    size_t count = 0;
    if (!links)
        return 0;
    for (const auto &link : *links)
        count += link.subscribed && link.handle != NO_LINK;
    return count;
}
size_t blePhones()
{
    concurrency::LockGuard guard(&lock);
    size_t count = 0;
    if (!links)
        return 0;
    for (const auto &link : *links)
        count += link.subscribed && link.talked && link.handle != NO_LINK;
    return count;
}
size_t bleConnections()
{
    concurrency::LockGuard guard(&lock);
    size_t count = 0;
    for (const auto &connection : connections)
        count += connection.handle != NO_LINK;
    return count;
}
void bleServicesChanged()
{
    // Asks connected and bonded phones to read the service list again instead of
    // trusting an old copy.
    ble_svc_gatt_changed(0x0001, 0xffff);
}
void bleDropConnections()
{
    std::array<uint16_t, LINKS> handles;
    NimBLEServer *target = nullptr;
    {
        concurrency::LockGuard guard(&lock);
        target = server;
        for (size_t index = 0; index < LINKS; ++index)
            handles[index] = connections[index].handle;
    }
    for (const uint16_t handle : handles)
        if (target && handle != NO_LINK)
            target->disconnect(handle);
}
uint32_t bleDrops()
{
    return dropped.load();
}
bool bleNeedsAnnouncement()
{
    return announceNeeded.exchange(false);
}
} // namespace bitchat
#endif

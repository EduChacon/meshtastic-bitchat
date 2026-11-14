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
// Raw link events for the self-test: they tell a silent phone from a write that never reached the bridge.
static std::atomic<uint32_t> connects{0}, disconnects{0}, subscriptions{0}, writes{0}, notified{0}, notifyFailures{0},
    remoteFrames{0}, remoteWrites{0};
// A Bitchat phone publishes the same service. Subscribing to it over the link the phone opened gives a
// second path, its notifications in and bridge writes out, that does not need the phone's own writes.
enum class Remote : uint8_t {
    Idle,
    Wanted,
    Service,
    HasService,
    Characteristic,
    HasCharacteristic,
    Descriptor,
    HasDescriptor,
    Subscribing,
    Ready,
    Absent
};
struct Connection {
    uint16_t handle = NO_LINK;
    bool subscribed = false;
    Remote remote = Remote::Idle;
    bool remoteSeen = false;
    // Kept here as well so that it outlives the link buffers, which are released while the bridge is off.
    bool talked = false;
    uint16_t remoteStart = 0, remoteEnd = 0, remoteValue = 0, remoteCccd = 0;
    uint32_t remoteAt = 0;
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
    // Nonzero once the phone's own characteristic is subscribed: frames then leave as writes to it.
    uint16_t remoteValue = 0;
    // Nothing is sent before the search for that characteristic ends, so the first frame takes the right path.
    bool settled = false;
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
        link.remoteValue = 0;
        link.settled = false;
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

static Connection *findConnection(uint16_t handle)
{
    for (auto &connection : connections)
        if (connection.handle == handle)
            return &connection;
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
        ++writes;
        if (!desc)
            return;
        const auto value = c->getValue();
        receive(desc->conn_handle, {reinterpret_cast<const uint8_t *>(value.data()), value.size()});
    }

  public:
    // One piece from a phone, written to the bridge or notified by the phone's own characteristic.
    static void receive(uint16_t handle, Bytes chunk)
    {
        if (!enabled || !chunk.size)
            return;
        concurrency::LockGuard guard(&lock);
        Link *link = findLink(handle);
        if (!link) {
            ++dropped;
            return;
        }
        if (!link->talked && !link->settled) {
            // No service of its own was found, but its writes arrive: the notification path is safe to use.
            link->settled = true;
            announceNeeded = true;
        }
        link->talked = true;
        if (auto *connection = findConnection(handle))
            connection->talked = true;
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
        subscriptions += value & 1;
        if (auto *link = findLink(desc->conn_handle))
            link->subscribed = (value & 1) != 0;
        // Only a Bitchat app subscribes here, so its own service is looked for now, and again on every
        // subscription because the app publishes it anew each time it starts.
        for (auto &connection : connections)
            if (connection.handle == desc->conn_handle) {
                connection.remote = (value & 1) ? Remote::Wanted : Remote::Idle;
                connection.remoteAt = millis();
                connection.talked = false;
                if (auto *link = findLink(desc->conn_handle)) {
                    link->remoteValue = 0;
                    link->settled = link->talked = false;
                }
            }
    }
};
static Callbacks callbacks;

// Lock held. Ready or Absent ends the search either way, and the phone is then told who the bridge is.
static void settle(Connection &connection, Remote state)
{
    connection.remote = state;
    connection.remoteSeen = false;
    connection.remoteAt = millis();
    if (state != Remote::Ready && state != Remote::Absent)
        return;
    // Without the phone's service, the bridge speaks only to a phone whose writes have arrived: an
    // announcement notified to a silent one would bind it to the path that is not working.
    if (auto *link = findLink(connection.handle)) {
        link->remoteValue = state == Remote::Ready ? connection.remoteValue : 0;
        link->settled = state == Remote::Ready || link->talked;
        announceNeeded = announceNeeded || link->settled;
    }
}

static int onRemoteService(uint16_t handle, const ble_gatt_error *error, const ble_gatt_svc *service, void *)
{
    concurrency::LockGuard guard(&lock);
    auto *connection = findConnection(handle);
    if (!connection || connection->remote != Remote::Service)
        return 0;
    if (error && error->status == 0 && service) {
        connection->remoteStart = service->start_handle;
        connection->remoteEnd = service->end_handle;
        connection->remoteSeen = true;
    } else {
        const bool found = error && error->status == BLE_HS_EDONE && connection->remoteSeen;
        settle(*connection, found ? Remote::HasService : Remote::Absent);
    }
    return 0;
}

static int onRemoteCharacteristic(uint16_t handle, const ble_gatt_error *error, const ble_gatt_chr *found, void *)
{
    concurrency::LockGuard guard(&lock);
    auto *connection = findConnection(handle);
    if (!connection || connection->remote != Remote::Characteristic)
        return 0;
    if (error && error->status == 0 && found) {
        connection->remoteValue = found->val_handle;
        connection->remoteSeen = true;
    } else {
        const bool known = error && error->status == BLE_HS_EDONE && connection->remoteSeen;
        settle(*connection, known ? Remote::HasCharacteristic : Remote::Absent);
    }
    return 0;
}

static int onRemoteDescriptor(uint16_t handle, const ble_gatt_error *error, uint16_t, const ble_gatt_dsc *found, void *)
{
    concurrency::LockGuard guard(&lock);
    auto *connection = findConnection(handle);
    if (!connection || connection->remote != Remote::Descriptor)
        return 0;
    if (error && error->status == 0 && found) {
        if (found->uuid.u.type == BLE_UUID_TYPE_16 && ble_uuid_u16(&found->uuid.u) == 0x2902) {
            connection->remoteCccd = found->handle;
            connection->remoteSeen = true;
        }
    } else {
        const bool known = error && error->status == BLE_HS_EDONE && connection->remoteSeen;
        settle(*connection, known ? Remote::HasDescriptor : Remote::Absent);
    }
    return 0;
}

static int onRemoteSubscribed(uint16_t handle, const ble_gatt_error *error, ble_gatt_attr *, void *)
{
    concurrency::LockGuard guard(&lock);
    auto *connection = findConnection(handle);
    if (connection && connection->remote == Remote::Subscribing)
        settle(*connection, error && error->status == 0 ? Remote::Ready : Remote::Absent);
    return 0;
}

// Notifications from the phone's characteristic arrive as link events, not through the bridge's own service.
static int onGapEvent(ble_gap_event *event, void *)
{
    if (!event || event->type != BLE_GAP_EVENT_NOTIFY_RX || !event->notify_rx.om)
        return 0;
    const uint16_t handle = event->notify_rx.conn_handle;
    {
        concurrency::LockGuard guard(&lock);
        const auto *connection = findConnection(handle);
        if (!connection || connection->remote != Remote::Ready || connection->remoteValue != event->notify_rx.attr_handle)
            return 0;
    }
    std::array<uint8_t, 512> flat;
    uint16_t size = 0;
    if (ble_hs_mbuf_to_flat(event->notify_rx.om, flat.data(), flat.size(), &size) != 0) {
        ++dropped;
        return 0;
    }
    ++remoteFrames;
    Callbacks::receive(handle, {flat.data(), size});
    return 0;
}
static ble_gap_event_listener listener;
static bool listening = false;

static void uuidFromText(const char *text, ble_uuid128_t &out)
{
    // The stack keeps 128-bit UUIDs least significant byte first.
    out.u.type = BLE_UUID_TYPE_128;
    size_t index = sizeof(out.value);
    const auto digit = [](char h) { return uint8_t(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10); };
    for (const char *c = text; c[0] && c[1] && index; ++c) {
        if (*c == '-')
            continue;
        out.value[--index] = uint8_t(digit(c[0]) << 4 | digit(c[1]));
        ++c;
    }
}

// One step of the search per call, issued outside the lock because the stack calls back into it.
static void advanceRemote()
{
    struct Step {
        uint16_t handle = NO_LINK;
        Remote stage = Remote::Idle;
        uint16_t first = 0, last = 0;
    };
    std::array<Step, LINKS> steps;
    {
        concurrency::LockGuard guard(&lock);
        for (size_t index = 0; index < LINKS; ++index) {
            auto &connection = connections[index];
            if (connection.handle == NO_LINK)
                continue;
            Remote next = Remote::Idle;
            switch (connection.remote) {
            case Remote::Wanted:
                next = Remote::Service;
                break;
            case Remote::HasService:
                next = Remote::Characteristic;
                steps[index].first = connection.remoteStart;
                steps[index].last = connection.remoteEnd;
                break;
            case Remote::HasCharacteristic:
                next = Remote::Descriptor;
                steps[index].first = connection.remoteValue;
                steps[index].last = connection.remoteEnd;
                break;
            case Remote::HasDescriptor:
                next = Remote::Subscribing;
                steps[index].first = connection.remoteCccd;
                break;
            case Remote::Service:
            case Remote::Characteristic:
            case Remote::Descriptor:
            case Remote::Subscribing:
                if (!Throttle::isWithinTimespanMsAt(connection.remoteAt, 8000, millis()))
                    settle(connection, Remote::Absent);
                break;
            case Remote::Absent: {
                // The app may start, and publish its service, long after the link came up.
                const auto *link = findLink(connection.handle);
                if (connection.subscribed && link && !link->talked &&
                    !Throttle::isWithinTimespanMsAt(connection.remoteAt, 10000, millis()))
                    next = Remote::Service;
                break;
            }
            default:
                break;
            }
            if (next != Remote::Idle) {
                settle(connection, next);
                steps[index].handle = connection.handle;
                steps[index].stage = next;
            }
        }
    }
    for (const auto &step : steps) {
        if (step.handle == NO_LINK)
            continue;
        ble_uuid128_t service{}, value{};
        uuidFromText(SERVICE_UUID, service);
        uuidFromText(CHAR_UUID, value);
        static const uint8_t notify[2] = {1, 0};
        int result = -1;
        if (step.stage == Remote::Service)
            result = ble_gattc_disc_svc_by_uuid(step.handle, &service.u, onRemoteService, nullptr);
        else if (step.stage == Remote::Characteristic)
            result = ble_gattc_disc_chrs_by_uuid(step.handle, step.first, step.last, &value.u, onRemoteCharacteristic, nullptr);
        else if (step.stage == Remote::Descriptor)
            result = ble_gattc_disc_all_dscs(step.handle, step.first, step.last, onRemoteDescriptor, nullptr);
        else if (step.stage == Remote::Subscribing)
            result = ble_gattc_write_flat(step.handle, step.first, notify, sizeof(notify), onRemoteSubscribed, nullptr);
        if (result != 0) {
            concurrency::LockGuard guard(&lock);
            auto *connection = findConnection(step.handle);
            if (connection && connection->remote == step.stage)
                settle(*connection, Remote::Absent);
        }
    }
}

void bleAttach(NimBLEServer *s)
{
    concurrency::LockGuard guard(&lock);
    server = s;
    auto *service = server->createService(SERVICE_UUID);
    characteristic = service->createCharacteristic(
        CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY, 512);
    characteristic->setCallbacks(&callbacks);
    service->start();
    if (!listening)
        listening = ble_gap_event_listener_register(&listener, onGapEvent, nullptr) == 0;
}

void bleDetach()
{
    enabled = false;
    if (listening)
        ble_gap_event_listener_unregister(&listener);
    listening = false;
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
        link.remoteValue = connections[index].remote == Remote::Ready ? connections[index].remoteValue : 0;
        link.talked = connections[index].talked;
        link.settled = connections[index].remote == Remote::Ready ||
                       (connections[index].remote == Remote::Absent && connections[index].talked);
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
    bool added = false;
    {
        concurrency::LockGuard guard(&lock);
        for (const auto &connection : connections)
            if (connection.handle == handle)
                return;
        for (size_t index = 0; index < LINKS; ++index) {
            if (connections[index].handle == NO_LINK) {
                connections[index] = {};
                connections[index].handle = handle;
                ++connects;
                added = true;
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
    // The app sizes its first packet a moment after connecting. At the default 23-byte MTU nothing it
    // sends fits a write, so the bridge asks for a larger one at once instead of waiting for the phone.
    if (added)
        ble_gattc_exchange_mtu(handle, nullptr, nullptr);
    if (enabled)
        NimBLEDevice::getAdvertising()->start();
}

void bleDisconnect(uint16_t handle)
{
    concurrency::LockGuard guard(&lock);
    for (auto &connection : connections)
        if (connection.handle == handle) {
            connection = {};
            ++disconnects;
        }
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
        recipient |= link.subscribed && link.settled && link.handle != exclude && link.handle != NO_LINK;
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
        if (link.subscribed && link.settled && link.handle != exclude && link.handle != NO_LINK) {
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
    uint16_t handle = NO_LINK, attribute = 0, remote = 0;
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
            const size_t room = std::min(mtu > 3 ? mtu - 3 : size_t(20), chunk.size());
            // The phone takes one whole frame per write; a frame too large for that keeps the notification stream.
            if (link.remoteValue && !message->offset && payload.size <= room)
                remote = link.remoteValue;
            count = std::min(size_t(payload.size - message->offset), room);
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
    if (remote) {
        if (ble_gattc_write_no_rsp_flat(handle, remote, chunk.data(), uint16_t(count)) != 0) {
            ++notifyFailures;
            return false;
        }
        ++remoteWrites;
    } else {
        auto *buffer = ble_hs_mbuf_from_flat(chunk.data(), count);
        if ((buffer ? ble_gatts_notify_custom(handle, attribute, buffer) : BLE_HS_ENOMEM) != 0) {
            ++notifyFailures;
            return false;
        }
        ++notified;
    }
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
    advanceRemote();
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
size_t bleListeners()
{
    concurrency::LockGuard guard(&lock);
    size_t count = 0;
    if (!links)
        return 0;
    for (const auto &link : *links)
        count += link.subscribed && link.settled && link.handle != NO_LINK;
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
size_t bleDescribe(char *out, size_t size)
{
    struct View {
        uint16_t handle = NO_LINK;
        bool subscribed = false, talked = false;
        Remote remote = Remote::Idle;
    };
    std::array<View, LINKS> views;
    {
        concurrency::LockGuard guard(&lock);
        for (size_t index = 0; index < LINKS; ++index) {
            views[index].handle = connections[index].handle;
            views[index].subscribed = connections[index].subscribed;
            views[index].remote = connections[index].remote;
            views[index].talked = connections[index].talked;
        }
    }
    const auto count = [](const std::atomic<uint32_t> &value) { return static_cast<unsigned long>(value.load()); };
    // wr and ntf are the bridge's own characteristic; in and out are the phone's, used when "path" is 9.
    int used = snprintf(out, size, "con %lu dis %lu sub %lu wr %lu ntf %lu err %lu in %lu out %lu", count(connects),
                        count(disconnects), count(subscriptions), count(writes), count(notified), count(notifyFailures),
                        count(remoteFrames), count(remoteWrites));
    // The host stack is asked outside the bridge lock, which its own callbacks take.
    for (const auto &view : views) {
        ble_gap_conn_desc desc{};
        if (view.handle == NO_LINK || used < 0 || size_t(used) >= size || ble_gap_conn_find(view.handle, &desc) != 0)
            continue;
        used += snprintf(out + used, size - used, "\nlink %u mtu %u itv %u enc %u bond %u sub %u wr %u path %u",
                         unsigned(view.handle), unsigned(ble_att_mtu(view.handle)), unsigned(desc.conn_itvl),
                         unsigned(desc.sec_state.encrypted), unsigned(desc.sec_state.bonded), unsigned(view.subscribed),
                         unsigned(view.talked), unsigned(view.remote));
    }
    return used < 0 ? 0 : std::min(size_t(used), size ? size - 1 : 0);
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

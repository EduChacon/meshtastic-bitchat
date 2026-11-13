#ifdef MESHTASTIC_ENABLE_BITCHAT
#include "BridgeBle.h"
#include "BridgeDiagnostics.h"
#include "BridgeState.h"
#include "concurrency/Lock.h"
#include "concurrency/LockGuard.h"
#include "nimble/nimble/host/include/host/ble_gatt.h"
#include "nimble/nimble/host/include/host/ble_hs.h"
#include <NimBLEDevice.h>
#include <atomic>
#include <memory>
#include <new>

namespace bitchat {
static constexpr const char *SERVICE_UUID =
    "F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C";
static constexpr const char *CHAR_UUID = "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D";
static constexpr size_t LINKS = 3;
static concurrency::Lock lock;
static NimBLEServer *server = nullptr;
static NimBLECharacteristic *characteristic = nullptr;
static std::atomic<bool> enabled{false}, nativeWindow{false}, economy{false},
    announceNeeded{false};
static std::atomic<uint32_t> dropped{0};
struct Connection {
  uint16_t handle = NO_LINK;
  bool subscribed = false;
};
static std::array<Connection, LINKS> connections;
static constexpr uint8_t NO_FRAME = 255;
struct Outgoing {
  uint8_t frame = NO_FRAME;
  size_t offset = 0;
  uint32_t started = 0;
};
struct Link {
  uint16_t handle = NO_LINK;
  bool subscribed = false;
  uint32_t generation = 0;
  uint32_t receivedAt = 0;
  uint8_t incoming = NO_FRAME;
  BoundedQueue<uint8_t, 2> received;
  BoundedQueue<Outgoing, 2> sent;
};
struct Links : std::array<Link, LINKS> {
  struct Slot {
    std::unique_ptr<Frame> frame;
    uint8_t references = 0;
  };
  std::array<Slot, 8> frames;
  uint8_t allocate() {
    for (uint8_t i = 0; i < frames.size(); ++i) {
      auto &slot = frames[i];
      if (!slot.references) {
        if (!allocateBuffer(slot.frame))
          return NO_FRAME;
        slot.references = 1;
        return i;
      }
    }
    return NO_FRAME;
  }
  Frame &get(uint8_t index) { return *frames[index].frame; }
  void retain(uint8_t index) { ++frames[index].references; }
  void release(uint8_t index) {
    if (index != NO_FRAME && --frames[index].references == 0)
      frames[index].frame.reset();
  }
  void clearSent(Link &link) {
    while (auto *message = link.sent.front()) {
      release(message->frame);
      link.sent.pop();
    }
  }
  void clear(Link &link) {
    release(link.incoming);
    link.incoming = NO_FRAME;
    while (auto *frame = link.received.front()) {
      release(*frame);
      link.received.pop();
    }
    clearSent(link);
  }
};
static_assert(sizeof(Links) < 1024,
              "BLE startup must not reserve per-connection payloads");
static std::unique_ptr<Links> links;
static size_t nextReceive = 0;
static uint32_t nextGeneration = 0;

static Link *findLink(uint16_t handle) {
  if (!links)
    return nullptr;
  for (auto &link : *links)
    if (link.handle == handle)
      return &link;
  return nullptr;
}

class Callbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, ble_gap_conn_desc *desc) override {
    if (!enabled || !desc)
      return;
    const auto value = c->getValue();
    concurrency::LockGuard guard(&lock);
    Link *link = findLink(desc->conn_handle);
    if (!link) {
      ++dropped;
      return;
    }
    if (link->incoming != NO_FRAME &&
        !Throttle::isWithinTimespanMsAt(link->receivedAt, 3000, millis())) {
      links->release(link->incoming);
      link->incoming = NO_FRAME;
    }
    if (link->incoming == NO_FRAME) {
      link->incoming = links->allocate();
      if (link->incoming == NO_FRAME) {
        ++dropped;
        return;
      }
      link->receivedAt = millis();
    }
    auto &incoming = links->get(link->incoming);
    if (value.size() > MAX_FRAME - incoming.size) {
      links->release(link->incoming);
      link->incoming = NO_FRAME;
      ++dropped;
      return;
    }
    std::copy_n(value.data(), value.size(),
                incoming.data.begin() + incoming.size);
    incoming.size += value.size();
    size_t length = 0;
    Result result = wireFrameLength(incoming.bytes(), length);
    if (result == Result::Incomplete)
      return;
    if (result != Result::Ok || incoming.size > length) {
      links->release(link->incoming);
      link->incoming = NO_FRAME;
      ++dropped;
      return;
    }
    if (incoming.size == length) {
      if (!link->received.push(link->incoming)) {
        links->release(link->incoming);
        ++dropped;
      }
      link->incoming = NO_FRAME;
    }
  }
  void onSubscribe(NimBLECharacteristic *, ble_gap_conn_desc *desc,
                   uint16_t value) override {
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

void bleAttach(NimBLEServer *s) {
  concurrency::LockGuard guard(&lock);
  server = s;
  auto *service = server->createService(SERVICE_UUID);
  characteristic = service->createCharacteristic(
      CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY,
      512);
  characteristic->setCallbacks(&callbacks);
  service->start();
}

void bleDetach() {
  enabled = false;
  concurrency::LockGuard guard(&lock);
  server = nullptr;
  characteristic = nullptr;
  links.reset();
  connections = {};
}

size_t bleBufferSize() { return sizeof(Links); }

bool bleAllocateBuffers() {
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

void bleReleaseBuffers() {
  enabled = false;
  concurrency::LockGuard guard(&lock);
  links.reset();
  announceNeeded = false;
}

void bleConnect(uint16_t handle) {
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

void bleDisconnect(uint16_t handle) {
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

void bleConfigure(bool on, bool native, bool lowPower) {
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

bool bleAdvertise() {
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

bool bleReceive(BleInput &output) {
  concurrency::LockGuard guard(&lock);
  if (!enabled || !links)
    return false;
  for (size_t offset = 0; offset < LINKS; ++offset) {
    size_t index = (nextReceive + offset) % LINKS;
    auto &link = (*links)[index];
    if (auto *frame = link.received.front()) {
      output.frame = links->get(*frame);
      output.link = link.handle;
      links->release(*frame);
      link.received.pop();
      nextReceive = (index + 1) % LINKS;
      return true;
    }
  }
  return false;
}

bool bleBroadcast(Bytes frame, uint16_t exclude) {
  if (!enabled || frame.size > MAX_FRAME)
    return false;
  concurrency::LockGuard guard(&lock);
  if (!links || !enabled)
    return false;
  bool recipient = false;
  for (const auto &link : *links)
    recipient |=
        link.subscribed && link.handle != exclude && link.handle != NO_LINK;
  if (!recipient)
    return true;
  const uint8_t slot = links->allocate();
  if (slot == NO_FRAME) {
    ++dropped;
    return false;
  }
  auto &payload = links->get(slot);
  payload.size = frame.size;
  std::copy_n(frame.data, frame.size, payload.data.begin());
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

void blePoll() {
  if (!enabled)
    return;
  for (size_t i = 0; i < LINKS; ++i) {
    uint16_t handle = NO_LINK, attribute = 0;
    uint32_t generation = 0;
    std::array<uint8_t, 512> chunk;
    size_t count = 0;
    bool expired = false;
    {
      concurrency::LockGuard guard(&lock);
      if (!links || !enabled)
        return;
      auto &link = (*links)[i];
      if (link.incoming != NO_FRAME &&
          !Throttle::isWithinTimespanMsAt(link.receivedAt, 3000, millis())) {
        links->release(link.incoming);
        link.incoming = NO_FRAME;
        ++dropped;
      }
      auto *message = link.sent.front();
      if (!server || !characteristic)
        return;
      if (!message)
        continue;
      if (!link.subscribed || link.handle == NO_LINK) {
        links->clearSent(link);
        continue;
      }
      handle = link.handle;
      generation = link.generation;
      expired =
          !Throttle::isWithinTimespanMsAt(message->started, 10000, millis());
      if (expired) {
        links->clearSent(link);
        ++dropped;
      } else {
        const auto &payload = links->get(message->frame);
        const size_t mtu = server->getPeerMTU(handle);
        count = std::min({payload.size - message->offset,
                          mtu > 3 ? mtu - 3 : size_t(20), chunk.size()});
        std::copy_n(payload.data.data() + message->offset, count,
                    chunk.begin());
        attribute = characteristic->getHandle();
      }
    }
    if (expired) {
      // Discard incomplete receiver stream before sending another frame.
      if (server)
        server->disconnect(handle);
      continue;
    }
    auto *buffer = ble_hs_mbuf_from_flat(chunk.data(), count);
    int result = buffer ? ble_gatts_notify_custom(handle, attribute, buffer)
                        : BLE_HS_ENOMEM;
    if (result == 0) {
      concurrency::LockGuard guard(&lock);
      if (!links)
        return;
      auto &link = (*links)[i];
      if (link.handle == handle && link.generation == generation) {
        if (auto *message = link.sent.front()) {
          message->offset += count;
          if (message->offset == links->get(message->frame).size) {
            links->release(message->frame);
            link.sent.pop();
          }
        }
      }
    }
  }
}

size_t bleSubscribers() {
  concurrency::LockGuard guard(&lock);
  size_t count = 0;
  if (!links)
    return 0;
  for (const auto &link : *links)
    count += link.subscribed && link.handle != NO_LINK;
  return count;
}
uint32_t bleDrops() { return dropped.load(); }
bool bleNeedsAnnouncement() { return announceNeeded.exchange(false); }
} // namespace bitchat
#endif

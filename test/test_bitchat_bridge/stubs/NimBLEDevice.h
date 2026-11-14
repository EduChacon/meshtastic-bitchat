#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

inline uint32_t fakeNow = 1;
inline uint32_t millis()
{
    return fakeNow;
}
struct ble_gap_conn_desc {
    uint16_t conn_handle;
};
class NimBLECharacteristic;
class NimBLECharacteristicCallbacks
{
  public:
    virtual ~NimBLECharacteristicCallbacks() = default;
    virtual void onWrite(NimBLECharacteristic *, ble_gap_conn_desc *) {}
    virtual void onSubscribe(NimBLECharacteristic *, ble_gap_conn_desc *, uint16_t) {}
};
class NimBLECharacteristic
{
  public:
    NimBLECharacteristicCallbacks *callbacks = nullptr;
    std::string value;
    std::string getValue() const { return value; }
    uint16_t getHandle() const { return 11; }
    void setCallbacks(NimBLECharacteristicCallbacks *cb) { callbacks = cb; }
};
namespace NIMBLE_PROPERTY
{
constexpr unsigned READ = 1, WRITE = 2, WRITE_NR = 4, NOTIFY = 8;
}
class NimBLEService
{
  public:
    NimBLECharacteristic characteristic;
    NimBLECharacteristic *createCharacteristic(const char *, unsigned, size_t) { return &characteristic; }
    void start() {}
};
class NimBLEServer
{
  public:
    NimBLEService service;
    std::vector<uint16_t> peers, disconnected;
    NimBLEService *createService(const char *) { return &service; }
    std::vector<uint16_t> getPeerDevices() const { return peers; }
    size_t getPeerMTU(uint16_t) const { return 247; }
    void disconnect(uint16_t handle) { disconnected.push_back(handle); }
};
class NimBLEUUID
{
  public:
    explicit NimBLEUUID(const char *) {}
};
class NimBLEAdvertisementData
{
  public:
    void setFlags(int) {}
    void setCompleteServices(const NimBLEUUID &) {}
    void setName(const char *) {}
};
class NimBLEAdvertising
{
  public:
    unsigned starts = 0;
    void start() { ++starts; }
    void stop() {}
    void reset() {}
    void setAdvertisementData(const NimBLEAdvertisementData &) {}
    void setScanResponseData(const NimBLEAdvertisementData &) {}
    void setScanResponse(bool) {}
    void setMinInterval(int) {}
    void setMaxInterval(int) {}
};
class NimBLEDevice
{
  public:
    static NimBLEAdvertising *getAdvertising()
    {
        static NimBLEAdvertising value;
        return &value;
    }
};
struct os_mbuf {
    std::vector<uint8_t> bytes;
};
inline int fakeNotifyResult = 0;
inline std::vector<std::pair<uint16_t, std::vector<uint8_t>>> fakeSent;
inline os_mbuf *ble_hs_mbuf_from_flat(const void *data, size_t count)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    return new os_mbuf{{bytes, bytes + count}};
}
inline int ble_gatts_notify_custom(uint16_t handle, uint16_t, os_mbuf *buffer)
{
    if (fakeNotifyResult == 0)
        fakeSent.emplace_back(handle, buffer->bytes);
    delete buffer;
    return fakeNotifyResult;
}
constexpr int BLE_HS_ENOMEM = 6;
inline unsigned fakeServiceChanges = 0;
inline void ble_svc_gatt_changed(uint16_t, uint16_t)
{
    ++fakeServiceChanges;
}

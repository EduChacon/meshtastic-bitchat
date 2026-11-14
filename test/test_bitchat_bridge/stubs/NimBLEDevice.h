#pragma once
#include <algorithm>
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
    uint16_t conn_itvl = 24;
    struct {
        unsigned encrypted : 1, authenticated : 1, bonded : 1;
    } sec_state{};
};
inline uint16_t fakeMtu = 185;
inline int ble_gap_conn_find(uint16_t handle, ble_gap_conn_desc *out)
{
    *out = ble_gap_conn_desc{handle};
    return 0;
}
inline uint16_t ble_att_mtu(uint16_t)
{
    return fakeMtu;
}
inline std::vector<uint16_t> fakeMtuRequests;
inline int ble_gattc_exchange_mtu(uint16_t handle, void *, void *)
{
    fakeMtuRequests.push_back(handle);
    return 0;
}

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
inline std::string fakeScanName;
class NimBLEAdvertisementData
{
  public:
    void setFlags(int) {}
    void setCompleteServices(const NimBLEUUID &) {}
    void setName(const char *text) { fakeScanName = text; }
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

// Client side of the stack: the bridge looks for the phone's own Bitchat service over the same link.
constexpr int BLE_HS_EDONE = 14, BLE_GAP_EVENT_NOTIFY_RX = 12, BLE_UUID_TYPE_16 = 16, BLE_UUID_TYPE_128 = 128;
struct ble_uuid_t {
    uint8_t type;
};
struct ble_uuid16_t {
    ble_uuid_t u;
    uint16_t value;
};
struct ble_uuid128_t {
    ble_uuid_t u;
    uint8_t value[16];
};
union ble_uuid_any_t {
    ble_uuid_t u;
    ble_uuid16_t u16;
    ble_uuid128_t u128;
};
inline uint16_t ble_uuid_u16(const ble_uuid_t *uuid)
{
    return reinterpret_cast<const ble_uuid16_t *>(uuid)->value;
}
struct ble_gatt_error {
    uint16_t status, att_handle;
};
struct ble_gatt_svc {
    uint16_t start_handle, end_handle;
    ble_uuid_any_t uuid;
};
struct ble_gatt_chr {
    uint16_t def_handle, val_handle;
    uint8_t properties;
    ble_uuid_any_t uuid;
};
struct ble_gatt_dsc {
    uint16_t handle;
    ble_uuid_any_t uuid;
};
struct ble_gatt_attr {
    uint16_t handle, offset;
    os_mbuf *om;
};
using ble_gatt_disc_svc_fn = int(uint16_t, const ble_gatt_error *, const ble_gatt_svc *, void *);
using ble_gatt_chr_fn = int(uint16_t, const ble_gatt_error *, const ble_gatt_chr *, void *);
using ble_gatt_dsc_fn = int(uint16_t, const ble_gatt_error *, uint16_t, const ble_gatt_dsc *, void *);
using ble_gatt_attr_fn = int(uint16_t, const ble_gatt_error *, ble_gatt_attr *, void *);
// Whether the simulated phone publishes the service, and at which handles; a refusal models a failed step.
inline bool fakeRemoteService = false;
inline int fakeRemoteRefusal = 0;
inline std::vector<uint8_t> fakeRemoteUuids;
inline std::vector<std::pair<uint16_t, uint16_t>> fakeRemoteSubscriptions;
inline std::vector<std::pair<uint16_t, std::vector<uint8_t>>> fakeRemoteWrites;
inline int ble_gattc_disc_svc_by_uuid(uint16_t handle, const ble_uuid_t *uuid, ble_gatt_disc_svc_fn *cb, void *arg)
{
    const auto *wide = reinterpret_cast<const ble_uuid128_t *>(uuid);
    fakeRemoteUuids.assign(wide->value, wide->value + 16);
    const ble_gatt_error found{0, 0}, done{BLE_HS_EDONE, 0};
    const ble_gatt_svc service{40, 49, {}};
    if (fakeRemoteService)
        cb(handle, &found, &service, arg);
    cb(handle, &done, nullptr, arg);
    return 0;
}
inline int ble_gattc_disc_chrs_by_uuid(uint16_t handle, uint16_t, uint16_t, const ble_uuid_t *, ble_gatt_chr_fn *cb, void *arg)
{
    if (fakeRemoteRefusal == 1)
        return 6;
    const ble_gatt_error found{0, 0}, done{BLE_HS_EDONE, 0};
    const ble_gatt_chr value{41, 42, 0x1e, {}};
    cb(handle, &found, &value, arg);
    cb(handle, &done, nullptr, arg);
    return 0;
}
inline int ble_gattc_disc_all_dscs(uint16_t handle, uint16_t value, uint16_t, ble_gatt_dsc_fn *cb, void *arg)
{
    const ble_gatt_error found{0, 0}, done{BLE_HS_EDONE, 0};
    ble_gatt_dsc other{43, {}}, configuration{44, {}};
    other.uuid.u16 = {{BLE_UUID_TYPE_16}, 0x2901};
    configuration.uuid.u16 = {{BLE_UUID_TYPE_16}, 0x2902};
    cb(handle, &found, value, &other, arg);
    if (fakeRemoteRefusal != 2)
        cb(handle, &found, value, &configuration, arg);
    cb(handle, &done, value, nullptr, arg);
    return 0;
}
inline int ble_gattc_write_flat(uint16_t handle, uint16_t attribute, const void *, uint16_t, ble_gatt_attr_fn *cb, void *arg)
{
    const ble_gatt_error result{uint16_t(fakeRemoteRefusal == 3 ? 0x103 : 0), 0};
    if (!result.status)
        fakeRemoteSubscriptions.emplace_back(handle, attribute);
    cb(handle, &result, nullptr, arg);
    return 0;
}
inline int ble_gattc_write_no_rsp_flat(uint16_t handle, uint16_t attribute, const void *data, uint16_t size)
{
    if (fakeNotifyResult != 0)
        return fakeNotifyResult;
    const auto *bytes = static_cast<const uint8_t *>(data);
    fakeRemoteWrites.emplace_back(uint16_t(handle * 1000 + attribute), std::vector<uint8_t>(bytes, bytes + size));
    return 0;
}
struct ble_gap_event {
    uint8_t type;
    struct {
        os_mbuf *om;
        uint16_t attr_handle, conn_handle;
        uint8_t indication : 1;
    } notify_rx;
};
using ble_gap_event_fn = int(ble_gap_event *, void *);
struct ble_gap_event_listener {
    ble_gap_event_fn *fn;
    void *arg;
};
inline ble_gap_event_listener *fakeListener = nullptr;
inline int ble_gap_event_listener_register(ble_gap_event_listener *listener, ble_gap_event_fn *fn, void *arg)
{
    listener->fn = fn;
    listener->arg = arg;
    fakeListener = listener;
    return 0;
}
inline int ble_gap_event_listener_unregister(ble_gap_event_listener *)
{
    fakeListener = nullptr;
    return 0;
}
inline int ble_hs_mbuf_to_flat(const os_mbuf *om, void *flat, uint16_t capacity, uint16_t *size)
{
    if (om->bytes.size() > capacity)
        return 1;
    std::copy(om->bytes.begin(), om->bytes.end(), static_cast<uint8_t *>(flat));
    *size = uint16_t(om->bytes.size());
    return 0;
}

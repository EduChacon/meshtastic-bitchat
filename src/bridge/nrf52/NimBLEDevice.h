// nRF52 only: the few NimBLE names the bridge uses, implemented over the Bluefruit stack, so the ESP32
// link code compiles unchanged. Found on the include path of the nRF52 bridge targets, never on ESP32.
#pragma once
#include <Arduino.h>
#include <cstddef>
#include <cstdint>

struct ble_gap_conn_desc {
    uint16_t conn_handle;
    uint16_t conn_itvl;
    struct {
        unsigned encrypted : 1, authenticated : 1, bonded : 1;
    } sec_state;
};

class NimBLECharacteristic;
class NimBLECharacteristicCallbacks
{
  public:
    virtual ~NimBLECharacteristicCallbacks() = default;
    virtual void onWrite(NimBLECharacteristic *, ble_gap_conn_desc *) {}
    virtual void onSubscribe(NimBLECharacteristic *, ble_gap_conn_desc *, uint16_t) {}
};
// A view of the bytes just written; valid only inside the write callback.
struct NimBLEAttValue {
    const uint8_t *bytes = nullptr;
    size_t length = 0;
    const char *data() const { return reinterpret_cast<const char *>(bytes); }
    size_t size() const { return length; }
};
class NimBLECharacteristic
{
  public:
    NimBLECharacteristicCallbacks *callbacks = nullptr;
    NimBLEAttValue value;
    NimBLEAttValue getValue() const { return value; }
    uint16_t getHandle() const;
    void setCallbacks(NimBLECharacteristicCallbacks *target) { callbacks = target; }
};
namespace NIMBLE_PROPERTY
{
constexpr unsigned READ = 1, WRITE = 2, WRITE_NR = 4, NOTIFY = 8;
}
class NimBLEService
{
  public:
    NimBLECharacteristic *createCharacteristic(const char *uuid, unsigned properties, size_t maxLength);
    void start() {}
};
class NimBLEServer
{
  public:
    NimBLEService *createService(const char *uuid);
    size_t getPeerMTU(uint16_t handle) const;
    void disconnect(uint16_t handle);
};
class NimBLEUUID
{
  public:
    explicit NimBLEUUID(const char *) {}
};
class NimBLEAdvertisementData
{
  public:
    const char *name = nullptr;
    void setFlags(int) {}
    void setCompleteServices(const NimBLEUUID &) {}
    void setName(const char *text) { name = text; }
};
class NimBLEAdvertising
{
    uint16_t interval = 0;

  public:
    void start();
    void stop();
    void reset();
    void setAdvertisementData(const NimBLEAdvertisementData &);
    void setScanResponseData(const NimBLEAdvertisementData &);
    void setScanResponse(bool) {}
    void setMinInterval(int units) { interval = uint16_t(units); }
    void setMaxInterval(int) {}
};
class NimBLEDevice
{
  public:
    static NimBLEAdvertising *getAdvertising();
};

// Kept in the bridge's namespace: the SoftDevice headers define ble_uuid_t and ble_uuid128_t differently.
namespace bitchat
{
struct os_mbuf {
    const uint8_t *data;
    uint16_t size;
};
constexpr int BLE_HS_ENOMEM = 6, BLE_HS_EDONE = 14, BLE_GAP_EVENT_NOTIFY_RX = 12, BLE_UUID_TYPE_16 = 16, BLE_UUID_TYPE_128 = 128;
os_mbuf *ble_hs_mbuf_from_flat(const void *data, size_t size);
int ble_hs_mbuf_to_flat(const os_mbuf *buffer, void *flat, uint16_t capacity, uint16_t *size);
int ble_gatts_notify_custom(uint16_t handle, uint16_t attribute, os_mbuf *buffer);
int ble_gap_conn_find(uint16_t handle, ble_gap_conn_desc *out);
uint16_t ble_att_mtu(uint16_t handle);
// The SoftDevice here is configured without the service-changed indication.
inline void ble_svc_gatt_changed(uint16_t, uint16_t) {}

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
int ble_gattc_exchange_mtu(uint16_t handle, void *, void *);
int ble_gattc_disc_svc_by_uuid(uint16_t handle, const ble_uuid_t *uuid, ble_gatt_disc_svc_fn *callback, void *argument);
int ble_gattc_disc_chrs_by_uuid(uint16_t handle, uint16_t first, uint16_t last, const ble_uuid_t *uuid, ble_gatt_chr_fn *callback,
                                void *argument);
int ble_gattc_disc_all_dscs(uint16_t handle, uint16_t first, uint16_t last, ble_gatt_dsc_fn *callback, void *argument);
int ble_gattc_write_flat(uint16_t handle, uint16_t attribute, const void *data, uint16_t size, ble_gatt_attr_fn *callback,
                         void *argument);
int ble_gattc_write_no_rsp_flat(uint16_t handle, uint16_t attribute, const void *data, uint16_t size);

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
int ble_gap_event_listener_register(ble_gap_event_listener *listener, ble_gap_event_fn *callback, void *argument);
int ble_gap_event_listener_unregister(ble_gap_event_listener *listener);
} // namespace bitchat

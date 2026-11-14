#include "configuration.h"
#if defined(MESHTASTIC_ENABLE_BITCHAT) && defined(ARCH_NRF52)
#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "bridge/BridgeBle.h"
#include "bridge/BridgePlatform.h"
#include "concurrency/LockGuard.h"
#include "mesh/HardwareRNG.h"
#include "mesh/Throttle.h"
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <algorithm>
#include <bluefruit.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_system.h>

static bitchat::PlatformBluetooth platform;
bitchat::PlatformBluetooth *nimbleBluetooth = &platform;

namespace
{
enum class Step : uint8_t { None, Service, Characteristic, Descriptor, Write };
struct Slot {
    uint16_t handle = BLE_CONN_HANDLE_INVALID;
    bool subscribed = false;
    Step step = Step::None;
    uint16_t last = 0;
    ::ble_uuid_t target{};
    union {
        bitchat::ble_gatt_disc_svc_fn *service;
        bitchat::ble_gatt_chr_fn *characteristic;
        bitchat::ble_gatt_dsc_fn *descriptor;
        bitchat::ble_gatt_attr_fn *write;
    } callback{};
    void *argument = nullptr;
};
Slot slots[3];
BLEService bridgeService;
BLECharacteristic bridgeValue;
uint8_t valueBytes[244];
NimBLEServer shimServer;
NimBLEService shimService;
NimBLECharacteristic shimValue;
NimBLEAdvertising shimAdvertising;
bitchat::ble_gap_event_listener *listener = nullptr;

Slot *find(uint16_t handle)
{
    for (auto &slot : slots)
        if (handle != BLE_CONN_HANDLE_INVALID && slot.handle == handle)
            return &slot;
    return nullptr;
}

// Least significant byte first, as the SoftDevice keeps 128-bit UUIDs.
BLEUuid uuidFromText(const char *text)
{
    uint8_t bytes[16] = {};
    size_t index = sizeof(bytes);
    const auto digit = [](char h) { return uint8_t(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10); };
    for (const char *c = text; c[0] && c[1] && index; ++c) {
        if (*c == '-')
            continue;
        bytes[--index] = uint8_t(digit(c[0]) << 4 | digit(c[1]));
        ++c;
    }
    return BLEUuid(bytes);
}

void report(uint16_t handle, uint16_t cccd)
{
    if (auto *slot = find(handle))
        slot->subscribed = (cccd & 1) != 0;
    ble_gap_conn_desc desc{};
    desc.conn_handle = handle;
    if (shimValue.callbacks)
        shimValue.callbacks->onSubscribe(&shimValue, &desc, cccd);
}

void onValueWrite(uint16_t handle, BLECharacteristic *, uint8_t *data, uint16_t size)
{
    if (!shimValue.callbacks)
        return;
    ble_gap_conn_desc desc{};
    desc.conn_handle = handle;
    shimValue.value.bytes = data;
    shimValue.value.length = size;
    shimValue.callbacks->onWrite(&shimValue, &desc);
    shimValue.value = {};
}

void onValueCccd(uint16_t handle, BLECharacteristic *, uint16_t cccd)
{
    report(handle, cccd);
}

void onClientEvent(uint16_t handle, ble_evt_t *event)
{
    const auto &gattc = event->evt.gattc_evt;
    const bool success = gattc.gatt_status == BLE_GATT_STATUS_SUCCESS;
    const bitchat::ble_gatt_error ok{0, 0}, done{uint16_t(bitchat::BLE_HS_EDONE), 0};
    auto *slot = find(handle);
    switch (event->header.evt_id) {
    case BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP: {
        if (!slot || slot->step != Step::Service)
            return;
        const auto callback = slot->callback.service;
        void *argument = slot->argument;
        slot->step = Step::None;
        const auto &found = gattc.params.prim_srvc_disc_rsp;
        if (success && found.count) {
            bitchat::ble_gatt_svc out{};
            out.start_handle = found.services[0].handle_range.start_handle;
            out.end_handle = found.services[0].handle_range.end_handle;
            callback(handle, &ok, &out, argument);
        }
        callback(handle, &done, nullptr, argument);
        return;
    }
    case BLE_GATTC_EVT_CHAR_DISC_RSP: {
        if (!slot || slot->step != Step::Characteristic)
            return;
        const auto callback = slot->callback.characteristic;
        void *argument = slot->argument;
        const auto &found = gattc.params.char_disc_rsp;
        uint16_t next = 0;
        for (uint16_t i = 0; success && i < found.count; ++i) {
            const auto &item = found.chars[i];
            next = uint16_t(item.handle_value + 1);
            if (item.uuid.type != slot->target.type || item.uuid.uuid != slot->target.uuid)
                continue;
            slot->step = Step::None;
            bitchat::ble_gatt_chr out{};
            out.def_handle = item.handle_decl;
            out.val_handle = item.handle_value;
            callback(handle, &ok, &out, argument);
            callback(handle, &done, nullptr, argument);
            return;
        }
        const ble_gattc_handle_range_t range{next, slot->last};
        if (next && next <= slot->last && sd_ble_gattc_characteristics_discover(handle, &range) == NRF_SUCCESS)
            return;
        slot->step = Step::None;
        callback(handle, &done, nullptr, argument);
        return;
    }
    case BLE_GATTC_EVT_DESC_DISC_RSP: {
        if (!slot || slot->step != Step::Descriptor)
            return;
        const auto callback = slot->callback.descriptor;
        void *argument = slot->argument;
        const auto &found = gattc.params.desc_disc_rsp;
        uint16_t next = 0;
        for (uint16_t i = 0; success && i < found.count; ++i) {
            const auto &item = found.descs[i];
            next = uint16_t(item.handle + 1);
            bitchat::ble_gatt_dsc out{};
            out.handle = item.handle;
            if (item.uuid.type == BLE_UUID_TYPE_BLE) {
                out.uuid.u16.u.type = bitchat::BLE_UUID_TYPE_16;
                out.uuid.u16.value = item.uuid.uuid;
            }
            callback(handle, &ok, 0, &out, argument);
        }
        const ble_gattc_handle_range_t range{next, slot->last};
        if (next && next <= slot->last && sd_ble_gattc_descriptors_discover(handle, &range) == NRF_SUCCESS)
            return;
        slot->step = Step::None;
        callback(handle, &done, 0, nullptr, argument);
        return;
    }
    case BLE_GATTC_EVT_WRITE_RSP: {
        if (!slot || slot->step != Step::Write || gattc.params.write_rsp.handle != slot->last)
            return;
        const auto callback = slot->callback.write;
        void *argument = slot->argument;
        slot->step = Step::None;
        const bitchat::ble_gatt_error result{uint16_t(success ? 0 : 1), 0};
        callback(handle, &result, nullptr, argument);
        return;
    }
    case BLE_GATTC_EVT_HVX: {
        const auto &hvx = gattc.params.hvx;
        if (hvx.type != BLE_GATT_HVX_NOTIFICATION || !listener || !listener->fn)
            return;
        bitchat::os_mbuf view{hvx.data, hvx.len};
        bitchat::ble_gap_event out{};
        out.type = bitchat::BLE_GAP_EVENT_NOTIFY_RX;
        out.notify_rx.om = &view;
        out.notify_rx.attr_handle = hvx.handle;
        out.notify_rx.conn_handle = handle;
        listener->fn(&out, listener->arg);
        return;
    }
    default:
        return;
    }
}

// Runs in the Bluetooth task, after the library has handled the event itself.
void onBleEvent(ble_evt_t *event)
{
    const uint16_t handle = event->evt.common_evt.conn_handle;
    switch (event->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
        if (event->evt.gap_evt.params.connected.role != BLE_GAP_ROLE_PERIPH)
            return;
        for (auto &slot : slots)
            if (slot.handle == BLE_CONN_HANDLE_INVALID) {
                slot = {};
                slot.handle = handle;
                break;
            }
        bitchat::bleConnect(handle);
        return;
    case BLE_GAP_EVT_DISCONNECTED:
        if (auto *slot = find(handle))
            *slot = {};
        bitchat::bleDisconnect(handle);
        // The library resumes advertising only when no link is left.
        if (platform.up)
            Bluefruit.Advertising.start(0);
        return;
    case BLE_GAP_EVT_CONN_SEC_UPDATE: {
        // A bonded phone's subscription comes back with the encryption, without a write to report it.
        const auto *slot = find(handle);
        if (slot && !slot->subscribed && bridgeValue.notifyEnabled(handle))
            report(handle, 1);
        return;
    }
    default:
        onClientEvent(handle, event);
    }
}

bool decodeUuid(const bitchat::ble_uuid_t *uuid, ::ble_uuid_t &out)
{
    const auto *whole = reinterpret_cast<const bitchat::ble_uuid128_t *>(uuid);
    return uuid && uuid->type == bitchat::BLE_UUID_TYPE_128 && sd_ble_uuid_decode(16, whole->value, &out) == NRF_SUCCESS;
}
} // namespace

void bitchat::PlatformBluetooth::refreshBridgeAdvertising()
{
    if (!bleAdvertise())
        nativeAdvertising();
}

namespace
{
TaskHandle_t worker = nullptr;
SemaphoreHandle_t workReady = nullptr, workDone = nullptr;
void (*workFunction)(void *) = nullptr;
void *workArgument = nullptr;

void workerLoop(void *)
{
    for (;;) {
        xSemaphoreTake(workReady, portMAX_DELAY);
        workFunction(workArgument);
        xSemaphoreGive(workDone);
    }
}
} // namespace

bool bitchat::onLargeStack(void (*work)(void *), void *argument)
{
    // A call made from inside the work, such as a packet the bridge sent coming back to it, runs in place.
    if (worker && xTaskGetCurrentTaskHandle() == worker) {
        work(argument);
        return true;
    }
    if (!worker) {
        if (!workReady)
            workReady = xSemaphoreCreateBinary();
        if (!workDone)
            workDone = xSemaphoreCreateBinary();
        // 3072 words are 12 KB; the bridge keeps frame buffers of 2 KB on the stack.
        if (!workReady || !workDone || xTaskCreate(workerLoop, "bitchat", 3072, nullptr, TASK_PRIO_LOW, &worker) != pdPASS) {
            worker = nullptr;
            return false;
        }
    }
    workFunction = work;
    workArgument = argument;
    xSemaphoreGive(workReady);
    xSemaphoreTake(workDone, portMAX_DELAY);
    return true;
}

void bitchat::platformAttach()
{
    Bluefruit.setEventCallback(onBleEvent);
    bleAttach(&shimServer);
}

NimBLEService *NimBLEServer::createService(const char *uuid)
{
    bridgeService.setUuid(uuidFromText(uuid));
    bridgeService.begin();
    return &shimService;
}

size_t NimBLEServer::getPeerMTU(uint16_t handle) const
{
    auto *connection = Bluefruit.Connection(handle);
    return connection ? connection->getMtu() : 0;
}

void NimBLEServer::disconnect(uint16_t handle)
{
    Bluefruit.disconnect(handle);
}

NimBLECharacteristic *NimBLEService::createCharacteristic(const char *uuid, unsigned, size_t)
{
    // Open to unpaired phones, like the Bitchat app's own service.
    bridgeValue.setUuid(uuidFromText(uuid));
    bridgeValue.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP | CHR_PROPS_NOTIFY);
    bridgeValue.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    bridgeValue.setFixedLen(0);
    bridgeValue.setMaxLen(sizeof(valueBytes));
    bridgeValue.setBuffer(valueBytes, sizeof(valueBytes));
    bridgeValue.setWriteCallback(onValueWrite, false);
    bridgeValue.setCccdWriteCallback(onValueCccd, false);
    bridgeValue.begin();
    return &shimValue;
}

uint16_t NimBLECharacteristic::getHandle() const
{
    return bridgeValue.handles().value_handle;
}

NimBLEAdvertising *NimBLEDevice::getAdvertising()
{
    return &shimAdvertising;
}

void NimBLEAdvertising::start()
{
    Bluefruit.Advertising.restartOnDisconnect(true);
    if (interval)
        Bluefruit.Advertising.setInterval(interval, interval);
    // Refused by the stack while it already advertises or has no free connection; both are fine.
    Bluefruit.Advertising.start(0);
}

void NimBLEAdvertising::stop()
{
    Bluefruit.Advertising.stop();
}

void NimBLEAdvertising::reset()
{
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();
}

void NimBLEAdvertising::setAdvertisementData(const NimBLEAdvertisementData &)
{
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(bridgeService);
}

void NimBLEAdvertising::setScanResponseData(const NimBLEAdvertisementData &data)
{
    bitchat::nativeScanResponse();
    if (data.name)
        Bluefruit.ScanResponse.addData(BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, data.name, uint8_t(strlen(data.name)));
    else
        Bluefruit.ScanResponse.addName();
}

namespace bitchat
{
os_mbuf *ble_hs_mbuf_from_flat(const void *data, size_t size)
{
    // Only the bridge thread sends, one piece at a time.
    static os_mbuf view;
    view.data = static_cast<const uint8_t *>(data);
    view.size = uint16_t(size);
    return &view;
}

int ble_hs_mbuf_to_flat(const os_mbuf *buffer, void *flat, uint16_t capacity, uint16_t *size)
{
    if (!buffer || buffer->size > capacity)
        return 1;
    memcpy(flat, buffer->data, buffer->size);
    *size = buffer->size;
    return 0;
}

int ble_gatts_notify_custom(uint16_t handle, uint16_t, os_mbuf *buffer)
{
    return bridgeValue.notify(handle, buffer->data, buffer->size) ? 0 : 1;
}

int ble_gap_conn_find(uint16_t handle, ble_gap_conn_desc *out)
{
    auto *connection = Bluefruit.Connection(handle);
    if (!connection || !connection->connected())
        return 1;
    out->conn_handle = handle;
    out->conn_itvl = connection->getConnectionInterval();
    out->sec_state.encrypted = connection->secured();
    out->sec_state.bonded = connection->bonded();
    return 0;
}

uint16_t ble_att_mtu(uint16_t handle)
{
    auto *connection = Bluefruit.Connection(handle);
    return connection ? connection->getMtu() : 0;
}

int ble_gattc_exchange_mtu(uint16_t handle, void *, void *)
{
    auto *connection = Bluefruit.Connection(handle);
    return connection && connection->requestMtuExchange(Bluefruit.getMaxMtu(BLE_GAP_ROLE_PERIPH)) ? 0 : 1;
}

int ble_gattc_disc_svc_by_uuid(uint16_t handle, const ble_uuid_t *uuid, ble_gatt_disc_svc_fn *callback, void *argument)
{
    auto *slot = find(handle);
    ::ble_uuid_t target{};
    if (!slot || !decodeUuid(uuid, target))
        return 1;
    slot->callback.service = callback;
    slot->argument = argument;
    slot->step = Step::Service;
    if (sd_ble_gattc_primary_services_discover(handle, 1, &target) == NRF_SUCCESS)
        return 0;
    slot->step = Step::None;
    return 1;
}

int ble_gattc_disc_chrs_by_uuid(uint16_t handle, uint16_t first, uint16_t last, const ble_uuid_t *uuid, ble_gatt_chr_fn *callback,
                                void *argument)
{
    auto *slot = find(handle);
    ::ble_uuid_t target{};
    if (!slot || !decodeUuid(uuid, target))
        return 1;
    slot->callback.characteristic = callback;
    slot->argument = argument;
    slot->target = target;
    slot->last = last;
    slot->step = Step::Characteristic;
    const ble_gattc_handle_range_t range{first, last};
    if (sd_ble_gattc_characteristics_discover(handle, &range) == NRF_SUCCESS)
        return 0;
    slot->step = Step::None;
    return 1;
}

int ble_gattc_disc_all_dscs(uint16_t handle, uint16_t first, uint16_t last, ble_gatt_dsc_fn *callback, void *argument)
{
    auto *slot = find(handle);
    if (!slot)
        return 1;
    if (first >= last) {
        const ble_gatt_error done{uint16_t(BLE_HS_EDONE), 0};
        callback(handle, &done, 0, nullptr, argument);
        return 0;
    }
    slot->callback.descriptor = callback;
    slot->argument = argument;
    slot->last = last;
    slot->step = Step::Descriptor;
    const ble_gattc_handle_range_t range{uint16_t(first + 1), last};
    if (sd_ble_gattc_descriptors_discover(handle, &range) == NRF_SUCCESS)
        return 0;
    slot->step = Step::None;
    return 1;
}

int ble_gattc_write_flat(uint16_t handle, uint16_t attribute, const void *data, uint16_t size, ble_gatt_attr_fn *callback,
                         void *argument)
{
    auto *slot = find(handle);
    if (!slot)
        return 1;
    slot->callback.write = callback;
    slot->argument = argument;
    slot->last = attribute;
    slot->step = Step::Write;
    ble_gattc_write_params_t request{};
    request.write_op = BLE_GATT_OP_WRITE_REQ;
    request.handle = attribute;
    request.len = size;
    request.p_value = static_cast<const uint8_t *>(data);
    if (sd_ble_gattc_write(handle, &request) == NRF_SUCCESS)
        return 0;
    slot->step = Step::None;
    return 1;
}

int ble_gattc_write_no_rsp_flat(uint16_t handle, uint16_t attribute, const void *data, uint16_t size)
{
    // The library counts the stack's queue of write commands; taking a place first keeps the two in step.
    auto *connection = Bluefruit.Connection(handle);
    if (!connection || !connection->getWriteCmdPacket())
        return BLE_HS_ENOMEM;
    ble_gattc_write_params_t request{};
    request.write_op = BLE_GATT_OP_WRITE_CMD;
    request.handle = attribute;
    request.len = size;
    request.p_value = static_cast<const uint8_t *>(data);
    return sd_ble_gattc_write(handle, &request) == NRF_SUCCESS ? 0 : 1;
}

int ble_gap_event_listener_register(ble_gap_event_listener *target, ble_gap_event_fn *callback, void *argument)
{
    target->fn = callback;
    target->arg = argument;
    listener = target;
    return 0;
}

int ble_gap_event_listener_unregister(ble_gap_event_listener *)
{
    listener = nullptr;
    return 0;
}
} // namespace bitchat

// Settings, identity and clock live in one small file. The journal of recent messages is not kept across
// restarts here: the internal flash is small and wears with every write.
namespace
{
constexpr char STORE_PATH[] = "/prefs/bitchat.dat";
constexpr uint32_t STORE_MAGIC = 0x31504342;
struct Entry {
    uint8_t size = 0;
    uint8_t bytes[96] = {};
};
struct Store {
    uint32_t magic = STORE_MAGIC;
    Entry entries[3];
    uint8_t check = 0;
} store;
bool storeLoaded = false, clockWritten = false;
uint32_t clockWrittenAt = 0;

uint8_t checksum(const Store &image)
{
    uint8_t sum = 0x5a;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&image);
    for (size_t i = 0; i < offsetof(Store, check); ++i)
        sum = uint8_t(sum * 31 + bytes[i]);
    return sum;
}

Entry *entryFor(const char *key)
{
    static constexpr const char *KEYS[] = {"settings", "identity", "clock"};
    for (size_t i = 0; i < 3; ++i)
        if (!strcmp(key, KEYS[i]))
            return &store.entries[i];
    return nullptr;
}

bool writeStore()
{
    store.check = checksum(store);
    {
        concurrency::LockGuard guard(spiLock);
        FSCom.mkdir("/prefs");
    }
    SafeFile file(STORE_PATH, true);
    const bool whole = file.write(reinterpret_cast<const uint8_t *>(&store), sizeof(store)) == sizeof(store);
    return file.close() && whole;
}
} // namespace

bool Preferences::begin(const char *, bool)
{
    if (storeLoaded)
        return true;
    Store image;
    bool whole = false;
    {
        concurrency::LockGuard guard(spiLock);
        auto file = FSCom.open(STORE_PATH, FILE_O_READ);
        if (file) {
            whole = file.read(reinterpret_cast<uint8_t *>(&image), sizeof(image)) == int(sizeof(image));
            file.close();
        }
    }
    // A missing or damaged file starts the bridge from its defaults, with a new identity.
    if (whole && image.magic == STORE_MAGIC && image.check == checksum(image))
        store = image;
    storeLoaded = true;
    return true;
}

bool Preferences::isKey(const char *key)
{
    const auto *entry = entryFor(key);
    return entry && entry->size;
}

size_t Preferences::getBytesLength(const char *key)
{
    const auto *entry = entryFor(key);
    return entry ? entry->size : 0;
}

size_t Preferences::getBytes(const char *key, void *out, size_t size)
{
    const auto *entry = entryFor(key);
    if (!entry || !entry->size || entry->size > size)
        return 0;
    memcpy(out, entry->bytes, entry->size);
    return entry->size;
}

size_t Preferences::putBytes(const char *key, const void *in, size_t size)
{
    auto *entry = entryFor(key);
    if (!entry)
        return key[0] == 'j' ? size : 0;
    if (!size || size > sizeof(entry->bytes))
        return 0;
    const bool same = entry->size == size && !memcmp(entry->bytes, in, size);
    entry->size = uint8_t(size);
    memcpy(entry->bytes, in, size);
    if (same)
        return size;
    if (!strcmp(key, "clock")) {
        // The hourly clock mark reaches the flash once a day, or with the next settings change.
        if (clockWritten && Throttle::isWithinTimespanMs(clockWrittenAt, 86400000))
            return size;
        clockWritten = true;
        clockWrittenAt = millis();
    }
    return writeStore() ? size : 0;
}

uint32_t esp_random()
{
    uint32_t out = 0;
    esp_fill_random(&out, sizeof(out));
    return out;
}

void esp_fill_random(void *out, size_t size)
{
    auto *bytes = static_cast<uint8_t *>(out);
    if (HardwareRNG::fill(bytes, size))
        return;
    // Second source: the SoftDevice pool, which refills slowly and is drained in small pieces.
    const uint32_t started = millis();
    for (size_t done = 0; done < size && Throttle::isWithinTimespanMs(started, 3000);) {
        uint8_t available = 0;
        sd_rand_application_bytes_available_get(&available);
        const uint8_t take = uint8_t(std::min<size_t>(available, size - done));
        if (take && sd_rand_application_vector_get(bytes + done, take) == NRF_SUCCESS)
            done += take;
        else
            delay(2);
    }
}

size_t heap_caps_get_free_size(uint32_t)
{
    return size_t(std::max(0, dbgHeapFree()));
}
#endif

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

#define ESP_PLATFORM 1
#define ARCH_ESP32 1
#define MESHTASTIC_ENABLE_BITCHAT 1
#define LOG_INFO(...) ((void)0)
#define NIMBLE_LOGE(...) (++errors)
#define ESP_ERROR_CHECK(expression) assert((expression) == ESP_OK)
#define pdMS_TO_TICKS(ms) (ms)
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT()                                                                                      \
    {                                                                                                                            \
    }
using TickType_t = uint32_t;
using BaseType_t = int;
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NO_MEM = 10, pdPASS = 1;
constexpr int ESP_ERR_NVS_NO_FREE_PAGES = 100, ESP_ERR_NVS_NEW_VERSION_FOUND = 101;
constexpr int ESP_BT_MODE_CLASSIC_BT = 1, ESP_BT_MODE_BLE = 2, ESP_PWR_LVL_P9 = 9;
constexpr int CONFIG_BT_NIMBLE_MAX_CONNECTIONS = 3, NIMBLE_HS_STACK_SIZE = 4096, NIMBLE_CORE = 0;
constexpr int configMAX_PRIORITIES = 25, BLE_HS_IO_NO_INPUT_OUTPUT = 0, BLE_HS_IO_DISPLAY_ONLY = 1;
constexpr int BLE_SM_PAIR_KEY_DIST_ENC = 1, BLE_SM_PAIR_KEY_DIST_ID = 2;
constexpr int BLE_SM_PAIR_AUTHREQ_BOND = 1, BLE_SM_PAIR_AUTHREQ_MITM = 4, BLE_SM_PAIR_AUTHREQ_SC = 8;
constexpr int meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN = 0;
struct {
    struct {
        int mode = 1;
    } bluetooth;
} config;
struct esp_bt_controller_config_t {
    int mode, ble_max_conn, normal_adv_size, scan_duplicate_type;
};
struct {
    void (*reset_cb)(int);
    void (*sync_cb)();
    void (*store_status_cb)();
    int sm_io_cap, sm_bonding, sm_mitm, sm_sc, sm_our_key_dist, sm_their_key_dist;
} ble_hs_cfg;
static int failStage, calls, errors, erased, advertised, services, deinitialized, securityAuth;
static TickType_t ticks, started;
static int syncAfter;
static bool initialized;
static void *host_task_h;
static int operation(int stage)
{
    ++calls;
    return failStage == stage ? 123 : ESP_OK;
}
int nvs_flash_init()
{
    return operation(1);
}
int nvs_flash_erase()
{
    ++erased;
    return ESP_OK;
}
void esp_bt_controller_mem_release(int) {}
int esp_bt_controller_init(esp_bt_controller_config_t *)
{
    return operation(2);
}
int esp_bt_controller_enable(int)
{
    return operation(3);
}
int esp_nimble_hci_init()
{
    return operation(4);
}
void nimble_port_init() {}
void ble_store_util_status_rr() {}
void ble_store_config_init() {}
int ble_svc_gap_device_name_set(const char *)
{
    return operation(5);
}
int xTaskCreatePinnedToCore(void *, const char *, int, void *, int, void **handle, int)
{
    if (operation(6) != ESP_OK)
        return 0;
    *handle = reinterpret_cast<void *>(1);
    return pdPASS;
}
TickType_t xTaskGetTickCount()
{
    return ticks;
}
void vTaskDelay(int amount)
{
    ticks += amount;
    if (syncAfter >= 0 && TickType_t(ticks - started) >= unsigned(syncAfter))
        ble_hs_cfg.sync_cb();
}
class NimbleBluetoothServerCallback
{
};
struct Server {
    void setCallbacks(NimbleBluetoothServerCallback *callback, bool) { delete callback; }
    int getConnectedCount() { return 0; }
};
static Server server;
static Server *bleServer;
class NimBLEDevice
{
  public:
    static std::atomic<bool> m_synced;
    static constexpr int m_scanDuplicateSize = 100, m_scanFilterMode = 0;
    static void init(const std::string &);
    static void onReset(int) { m_synced = false; }
    static void onSync() { m_synced = true; }
    static void host_task(void *) {}
    static bool getInitialized() { return initialized; }
    static void setPower(int) {}
    static void setSecurityAuth(int value) { securityAuth = value; }
    static void setSecurityInitKey(int) {}
    static void setSecurityRespKey(int) {}
    static void setSecurityIOCap(int) {}
    static Server *createServer() { return &server; }
    static void deinit() { ++deinitialized; }
};
std::atomic<bool> NimBLEDevice::m_synced{false};
namespace bitchat
{
void bootDiagnostic(const char *) {}
void bleDetach() {}
} // namespace bitchat
std::string getDeviceName()
{
    return "synthetic";
}
class NimbleBluetooth
{
  public:
    bool startupFailed = false, isDeInit = false;
    void setup();
    void deinit();
    bool isActive();
    bool isConnected();
    void setupService() { ++services; }
    void startAdvertising() { ++advertised; }
};

#ifdef BRIDGE_UNPATCHED_CONTROL
struct UnboundedWait {
};
esp_err_t esp_nimble_enable(void *);
void nimble_port_freertos_init(void (*task)(void *))
{
    esp_nimble_enable(reinterpret_cast<void *>(task));
}
void taskYIELD()
{
    if (TickType_t(ticks - started) >= 6000)
        throw UnboundedWait{};
    vTaskDelay(1);
}
#include "startup-original-control.inc"
#else
#include "startup-under-test.inc"
#endif

static void reset(int fail, int sync, TickType_t start = 0)
{
    failStage = fail;
    syncAfter = sync;
    ticks = started = start;
    calls = errors = erased = advertised = services = deinitialized = securityAuth = 0;
    initialized = false;
    NimBLEDevice::m_synced = false;
    host_task_h = nullptr;
    bleServer = nullptr;
}

int main()
{
#ifdef BRIDGE_UNPATCHED_CONTROL
    for (int failure : {0, 6}) {
        reset(failure, -1);
        NimbleBluetooth bluetooth;
        bool blocked = false;
        try {
            bluetooth.setup();
        } catch (const UnboundedWait &) {
            blocked = true;
        }
        assert(blocked && !initialized && !advertised);
    }
    puts("Negative control: unpatched startup remains blocked with no sync or failed host task");
#else
    for (int failure = 1; failure <= 6; ++failure) {
        reset(failure, 1);
        NimbleBluetooth bluetooth;
        bluetooth.setup();
        assert(bluetooth.startupFailed && !initialized && !bluetooth.isActive());
        assert(!bluetooth.isConnected() && !services && !advertised && !erased && errors == 1);
        const int firstCalls = calls;
        bluetooth.setup();
        bluetooth.deinit();
        assert(calls == firstCalls && !deinitialized);
    }
    for (TickType_t start : {TickType_t(0), UINT32_MAX - 10}) {
        reset(0, -1, start);
        NimbleBluetooth bluetooth;
        bluetooth.setup();
        assert(bluetooth.startupFailed && !initialized && errors == 1);
        assert(TickType_t(ticks - start) == 5000 && !services && !advertised);
        NimBLEDevice::onSync();
        bluetooth.setup();
        assert(!services && !advertised && !initialized);
    }
    for (int delay : {1, 40, 4999, 5000}) {
        reset(0, delay, UINT32_MAX - 100);
        NimbleBluetooth bluetooth;
        bluetooth.setup();
        assert(!bluetooth.startupFailed && initialized && bluetooth.isActive());
        assert(services == 1 && advertised == 1 && !errors && !erased);
        assert(securityAuth == (BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_SC));
        assert(TickType_t(ticks - started) == unsigned(delay));
        bluetooth.deinit();
        assert(deinitialized == 1);
    }
    puts("Startup: 6 injected failures, missing/late sync, tick wrap, success and unchanged authentication passed");
#endif
}

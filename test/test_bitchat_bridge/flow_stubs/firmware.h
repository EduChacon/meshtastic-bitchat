// Host stand-ins for the firmware services the bridge module uses; never compiled into firmware.
#pragma once
#include <NimBLEDevice.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <strings.h>
#include <vector>

using NodeNum = uint32_t;
constexpr NodeNum NODENUM_BROADCAST = 0xffffffff;
enum meshtastic_PortNum { meshtastic_PortNum_TEXT_MESSAGE_APP = 1, meshtastic_PortNum_PRIVATE_APP = 256 };
enum meshtastic_MeshPacket_Priority {
    meshtastic_MeshPacket_Priority_BACKGROUND = 10,
    meshtastic_MeshPacket_Priority_DEFAULT = 64
};
constexpr int meshtastic_MeshPacket_decoded_tag = 4;
struct meshtastic_MeshPacket {
    NodeNum from = 0, to = NODENUM_BROADCAST;
    uint8_t channel = 0;
    int which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    struct {
        meshtastic_PortNum portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        struct {
            uint16_t size = 0;
            uint8_t bytes[233]{};
        } payload;
    } decoded;
    uint32_t id = 0;
    bool want_ack = false, via_mqtt = false, pki_encrypted = false;
    float rx_snr = 0;
    int32_t rx_rssi = 0;
    uint8_t hop_limit = 3, hop_start = 3;
    meshtastic_MeshPacket_Priority priority = meshtastic_MeshPacket_Priority_DEFAULT;
};
enum class ProcessMessage { CONTINUE, STOP };
class MeshModule
{
  public:
    explicit MeshModule(const char *) {}
    virtual ~MeshModule() = default;

  protected:
    virtual bool wantPacket(const meshtastic_MeshPacket *) { return true; }
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &) { return ProcessMessage::CONTINUE; }
    virtual bool wantUIFrame() { return false; }
};
namespace concurrency
{
class OSThread
{
  public:
    explicit OSThread(const char *) {}
    virtual ~OSThread() = default;

  protected:
    virtual int32_t runOnce() = 0;
    void setIntervalFromNow(unsigned) {}
};
} // namespace concurrency

enum { meshtastic_Channel_Role_DISABLED, meshtastic_Channel_Role_PRIMARY, meshtastic_Channel_Role_SECONDARY };
struct meshtastic_Channel {
    unsigned index = 0, role = meshtastic_Channel_Role_DISABLED;
    bool has_settings = true;
    struct {
        uint32_t channel_num = 0, id = 0;
        struct {
            size_t size = 0;
            uint8_t bytes[32]{};
        } psk;
        char name[12]{};
        bool uplink_enabled = false, downlink_enabled = false, has_module_settings = false;
        struct {
            unsigned position_precision = 0;
            bool is_muted = false;
        } module_settings;
    } settings;
};
constexpr meshtastic_Channel meshtastic_Channel_init_zero{};
struct Channels {
    std::array<meshtastic_Channel, 8> items{};
    unsigned getNumChannels() { return items.size(); }
    unsigned getPrimaryIndex() { return 0; }
    meshtastic_Channel &getByIndex(unsigned i) { return items.at(i); }
    const char *getName(unsigned i) { return items.at(i).settings.name; }
    bool isDefaultChannel(unsigned) { return false; }
    void setChannel(const meshtastic_Channel &c) { items.at(c.index) = c; }
    void onConfigChanged() {}
};
inline Channels channels;
constexpr int SEGMENT_CHANNELS = 8;
struct meshtastic_NodeInfoLite {
    NodeNum num = 0;
    bool has_user = true;
    struct {
        char long_name[40]{};
        struct {
            uint16_t size = 0;
            uint8_t bytes[32]{};
        } public_key;
    } user;
    float snr = 0;
    uint32_t last_heard = 0;
    bool via_mqtt = false, has_hops_away = false;
    uint8_t hops_away = 0;
    bool has_position = false;
    struct {
        int32_t latitude_i = 0, longitude_i = 0;
    } position;
};
struct NodeDB {
    NodeNum number = 0x00c0ffee;
    std::map<NodeNum, meshtastic_NodeInfoLite> nodes;
    NodeNum getNodeNum() { return number; }
    meshtastic_NodeInfoLite *getMeshNode(NodeNum n) { return nodes.count(n) ? &nodes[n] : nullptr; }
    size_t getNumMeshNodes() { return nodes.size(); }
    meshtastic_NodeInfoLite *getMeshNodeByIndex(size_t x)
    {
        auto it = nodes.begin();
        std::advance(it, x);
        it->second.num = it->first;
        return &it->second;
    }
    bool saveToDisk(int) { return true; }
};
inline NodeDB fakeNodeDB;
inline NodeDB *nodeDB = &fakeNodeDB;
inline NodeNum getFrom(const meshtastic_MeshPacket *p)
{
    return p->from ? p->from : nodeDB->getNodeNum();
}
struct Owner {
    bool is_licensed = false;
    char long_name[40] = "Casa Sul";
};
inline Owner owner;
struct Config {
    struct {
        bool enabled = true;
    } bluetooth;
    struct {
        bool is_power_saving = false;
    } power;
    struct {
        struct {
            uint16_t size = 32;
            uint8_t bytes[32]{};
        } private_key;
    } security;
};
inline Config config;

enum RTCQuality { RTCQualityNone, RTCQualityDevice, RTCQualityFromNet };
inline bool lastSetFromPhoneNtpOrGps = true;
inline uint32_t fakeEpoch = 1795000000;
inline RTCQuality getRTCQuality()
{
    return RTCQualityNone;
}
inline uint32_t getValidTime(RTCQuality)
{
    return fakeEpoch + millis() / 1000;
}
inline uint32_t sinceLastSeen(const meshtastic_NodeInfoLite *n)
{
    const int delta = int(getValidTime(RTCQualityDevice) - n->last_heard);
    return delta < 0 ? 0 : uint32_t(delta);
}

enum RxSource { RX_SRC_LOCAL };
inline std::vector<meshtastic_MeshPacket> fakeRadio, fakeApp;
inline const char *getDeviceName()
{
    return "Meshtastic_1a2b";
}
inline bool isFromUs(const meshtastic_MeshPacket *p)
{
    return p->from == 0 || p->from == nodeDB->getNodeNum();
}
inline unsigned fakeQueueFree = 8;
struct Router {
    uint32_t nextId = 100;
    meshtastic_MeshPacket *allocForSending()
    {
        auto *packet = new meshtastic_MeshPacket;
        packet->from = nodeDB->getNodeNum();
        packet->id = nextId++;
        return packet;
    }
    uint32_t bridgeAirtime(const meshtastic_MeshPacket *) { return 1000; }
    struct Status {
        unsigned free;
    };
    Status getQueueStatus() { return {fakeQueueFree}; }
};
inline Router fakeRouter;
inline Router *router = &fakeRouter;
struct PacketPool {
    void release(meshtastic_MeshPacket *packet) { delete packet; }
};
inline PacketPool packetPool;
inline struct MeshService {
    void sendToMesh(meshtastic_MeshPacket *packet, RxSource, bool)
    {
        fakeRadio.push_back(*packet);
        delete packet;
    }
    void sendToPhone(meshtastic_MeshPacket *packet)
    {
        fakeApp.push_back(*packet);
        delete packet;
    }
} fakeService;
inline MeshService *service = &fakeService;
inline struct AirTime {
    bool allowed = true;
    bool isTxAllowedAirUtil() { return allowed; }
    bool isTxAllowedChannelUtil(bool) { return allowed; }
} fakeAirTime;
inline AirTime *airTime = &fakeAirTime;

inline struct NimbleBluetooth {
    bool isDeInit = false, startupFailed = false;
    unsigned refreshes = 0;
    bool isActive() { return true; }
    void refreshBridgeAdvertising() { ++refreshes; }
} fakeBluetooth;
inline NimbleBluetooth *nimbleBluetooth = &fakeBluetooth;

inline uint32_t esp_random()
{
    return 7;
}
inline void esp_fill_random(void *out, size_t size)
{
    auto *bytes = static_cast<uint8_t *>(out);
    for (size_t i = 0; i < size; ++i)
        bytes[i] = uint8_t(0xb0 + 7 * i);
}

struct Preferences {
    static inline std::map<std::string, std::vector<uint8_t>> data;
    static inline unsigned writes = 0;
    bool begin(const char *, bool) { return true; }
    bool isKey(const char *key) { return data.count(key); }
    size_t getBytesLength(const char *key) { return data.at(key).size(); }
    size_t getBytes(const char *key, void *out, size_t n)
    {
        memcpy(out, data.at(key).data(), n);
        return n;
    }
    size_t putBytes(const char *key, const void *in, size_t n)
    {
        ++writes;
        const auto *bytes = static_cast<const uint8_t *>(in);
        data[key] = {bytes, bytes + n};
        return n;
    }
};

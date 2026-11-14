#include "bridge/BridgeDefaults.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace bitchat
{
using Digest = std::array<uint8_t, 32>;
}
using namespace bitchat;
struct ChannelSettings {
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
};
enum { meshtastic_Channel_Role_DISABLED, meshtastic_Channel_Role_PRIMARY, meshtastic_Channel_Role_SECONDARY };
struct meshtastic_Channel {
    unsigned index = 0, role = meshtastic_Channel_Role_DISABLED;
    bool has_settings = true;
    ChannelSettings settings;
};
constexpr meshtastic_Channel meshtastic_Channel_init_zero{};
struct Channels {
    std::array<meshtastic_Channel, 4> items{};
    unsigned changes = 0, mutations = 0;
    unsigned getNumChannels() { return items.size(); }
    unsigned getPrimaryIndex() { return 0; }
    meshtastic_Channel &getByIndex(unsigned i) { return items.at(i); }
    void setChannel(const meshtastic_Channel &c)
    {
        items.at(c.index) = c;
        ++mutations;
    }
    void onConfigChanged() { ++changes; }
} channels;
constexpr int SEGMENT_CHANNELS = 8;
struct NodeDB {
    bool writable = true;
    unsigned saves = 0;
    bool saveToDisk(int segment)
    {
        assert(segment == SEGMENT_CHANNELS);
        ++saves;
        return writable;
    }
} db;
NodeDB *nodeDB = &db;
struct {
    bool is_licensed = false;
} owner;
struct {
    struct {
        bool enabled = true;
    } bluetooth;
    struct {
        bool is_power_saving = false;
    } power;
} config;
struct Preferences {
    static inline std::map<std::string, std::vector<uint8_t>> data;
    static inline bool writable = true, readable = true, available = true;
    static inline unsigned writes = 0;
    bool begin(const char *, bool) { return available; }
    bool isKey(const char *key) { return data.count(key); }
    size_t getBytesLength(const char *key) { return data.at(key).size(); }
    size_t getBytes(const char *key, void *out, size_t n)
    {
        if (!readable)
            return 0;
        const auto &bytes = data.at(key);
        assert(bytes.size() == n);
        memcpy(out, bytes.data(), n);
        return n;
    }
    size_t putBytes(const char *key, const void *in, size_t n)
    {
        ++writes;
        if (!writable)
            return 0;
        const auto *bytes = static_cast<const uint8_t *>(in);
        data[key] = {bytes, bytes + n};
        return n;
    }
};
uint32_t millis()
{
    return 1200;
}
uint32_t esp_random()
{
    return 123;
}
void bleConfigure(bool on, bool, bool)
{
    assert(!on);
}
struct MeshModule {
    explicit MeshModule(const char *) {}
};
struct OSThread {
    explicit OSThread(const char *) {}
    void setIntervalFromNow(unsigned) {}
};
class BitchatBridgeModule : MeshModule, OSThread
{
  public:
#include "settings-under-test.inc"
    struct JournalEntry {
        std::array<uint8_t, 16> id{};
        uint64_t utc = 0;
    };
    std::array<JournalEntry, 64> journal{};
    Preferences storage;
    bool storageReady = false, identityReady = false, advertisingChanged = true;
    bool defaultsPending = false, defaultChannelUnavailable = false, clockWindow = false;
    bool memoryOk = true, identityOk = true, textReady = false, link = true;
    unsigned publicIndex = 1;
    uint32_t bootId = 0, textGuardAt = 0;
    uint64_t time = 0, clockFloor = 0;
    BitchatBridgeModule();
    bool persist();
    uint8_t provisionPublicChannel();
    void applyDefaults();
    void ensureDefaultChannel();
    void publicBinding(Digest &digest) const { digest.fill(publicIndex); }
    void updateClockWindow();
    void refreshChannels() {}
    bool ensureRuntime() { return memoryOk; }
    bool initIdentity()
    {
        identityReady = identityOk;
        return identityReady;
    }
    bool eligibleChannel(uint8_t i) const
    {
        const auto &c = channels.getByIndex(i);
        return i != 0 && c.role == meshtastic_Channel_Role_SECONDARY && c.settings.psk.size == 32 && !c.settings.uplink_enabled &&
               !c.settings.downlink_enabled;
    }
    bool binding(uint8_t i, Digest &digest) const
    {
        digest.fill(i);
        return eligibleChannel(i);
    }
    uint64_t utc() const { return time; }
};
#include "defaults-under-test.inc"

static void reset()
{
    channels = {};
    db = {};
    nodeDB = &db;
    owner = {};
    config.bluetooth.enabled = true;
    config.power.is_power_saving = false;
    Preferences::data.clear();
    Preferences::writable = Preferences::readable = Preferences::available = true;
    Preferences::writes = 0;
    for (unsigned i = 0; i < channels.getNumChannels(); ++i)
        channels.items[i].index = i;
    auto &primary = channels.items[0];
    primary.role = meshtastic_Channel_Role_PRIMARY;
    strcpy(primary.settings.name, "Private");
    primary.settings.psk.size = 32;
    memset(primary.settings.psk.bytes, 42, 32);
}

int main()
{
    reset();
    BitchatBridgeModule fresh;
    assert(fresh.settings.enabled && fresh.defaultsPending && channels.mutations == 0 && Preferences::writes == 0);
    fresh.applyDefaults();
    assert(fresh.settings.enabled && fresh.settings.textChannel == 1 && !fresh.defaultsPending);
    assert(fresh.settings.tunnelChannel == 255 && !fresh.settings.privateTunnel);
    assert(!strcmp(channels.items[0].settings.name, "Private") && channels.items[0].settings.psk.bytes[0] == 42);
    assert(channels.items[1].role == meshtastic_Channel_Role_SECONDARY);
    assert(!strcmp(channels.items[1].settings.name, "Bitchat"));
    assert(channels.items[1].settings.psk.size == 32);
    assert(std::equal(PUBLIC_CHANNEL_KEY.begin(), PUBLIC_CHANNEL_KEY.end(), channels.items[1].settings.psk.bytes));
    assert(channels.items[1].settings.has_module_settings && channels.items[1].settings.module_settings.position_precision == 0);
    assert(!channels.items[1].settings.uplink_enabled && !channels.items[1].settings.downlink_enabled);
    assert(db.saves == 1 && Preferences::writes == 1);
    fresh.applyDefaults();
    BitchatBridgeModule reboot;
    reboot.applyDefaults();
    assert(reboot.settings.textChannel == 1 && channels.mutations == 1 && Preferences::writes == 1);
    reboot.settings.enabled = false;
    assert(reboot.persist());
    BitchatBridgeModule off;
    off.applyDefaults();
    assert(!off.settings.enabled && !off.defaultsPending && channels.mutations == 1);
    off.settings.enabled = true;
    off.settings.textChannel = 255;
    assert(off.persist());
    BitchatBridgeModule localOnly;
    localOnly.applyDefaults();
    assert(localOnly.settings.textChannel == 255 && channels.mutations == 1);

    reset();
    BitchatBridgeModule legacy;
    legacy.settings.enabled = false;
    legacy.storage.putBytes("settings", &legacy.settings, sizeof(legacy.settings));
    BitchatBridgeModule migrated;
    migrated.applyDefaults();
    assert(migrated.settings.enabled && migrated.settings.textChannel == 1 && migrated.settings.reserved[0] == 1);

    reset();
    BitchatBridgeModule selected;
    selected.settings.textChannel = 2;
    selected.settings.textBinding.fill(78);
    selected.storage.putBytes("settings", &selected.settings, sizeof(selected.settings));
    BitchatBridgeModule preserveSelection;
    preserveSelection.applyDefaults();
    assert(preserveSelection.settings.textChannel == 2 && preserveSelection.settings.textBinding[0] == 78);
    assert(channels.mutations == 0 && db.saves == 0);

    reset();
    BitchatBridgeModule existingTunnel;
    assert(existingTunnel.provisionPublicChannel() == 1);
    existingTunnel.settings.tunnelChannel = 1;
    existingTunnel.applyDefaults();
    assert(existingTunnel.settings.textChannel == 255 && existingTunnel.settings.tunnelChannel == 1);
    assert(channels.mutations == 1);

    reset();
    BitchatBridgeModule noScreen;
    noScreen.applyDefaults();
    noScreen.advertisingChanged = false;
    noScreen.updateClockWindow();
    assert(noScreen.clockWindow && noScreen.advertisingChanged);
    noScreen.advertisingChanged = false;
    noScreen.updateClockWindow();
    assert(!noScreen.advertisingChanged);
    noScreen.time = 1794596400000;
    noScreen.updateClockWindow();
    assert(!noScreen.clockWindow && noScreen.advertisingChanged);

    reset();
    BitchatBridgeModule interrupted;
    assert(interrupted.provisionPublicChannel() == 1);
    assert(Preferences::writes == 0);
    BitchatBridgeModule resume;
    resume.applyDefaults();
    assert(resume.settings.textChannel == 1 && channels.mutations == 1 && db.saves == 1);

    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        reset();
        auto &s = channels.items[1].settings;
        if (scenario < 5) {
            strcpy(s.name, scenario == 4 ? "BITCHAT" : "Bitchat");
            channels.items[1].role = meshtastic_Channel_Role_SECONDARY;
            s.psk.size = 32;
            std::copy(PUBLIC_CHANNEL_KEY.begin(), PUBLIC_CHANNEL_KEY.end(), s.psk.bytes);
            if (scenario == 0)
                s.psk.bytes[0] ^= 1;
            if (scenario == 1)
                s.uplink_enabled = true;
            if (scenario == 2)
                s.downlink_enabled = true;
            if (scenario == 3)
                s.psk.size = 16;
        } else if (scenario == 5) {
            for (auto &c : channels.items)
                c.role = meshtastic_Channel_Role_PRIMARY;
        } else if (scenario == 6) {
            owner.is_licensed = true;
        } else {
            nodeDB = nullptr;
        }
        BitchatBridgeModule blocked;
        blocked.applyDefaults();
        assert(blocked.settings.enabled && blocked.settings.textChannel == 255 && blocked.defaultChannelUnavailable);
        assert(channels.mutations == 0 && db.saves == 0);
    }
    reset();
    strcpy(channels.items[1].settings.name, "Reserved");
    BitchatBridgeModule skipDisabled;
    skipDisabled.applyDefaults();
    assert(skipDisabled.settings.textChannel == 2 && !strcmp(channels.items[1].settings.name, "Reserved"));

    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        reset();
        BitchatBridgeModule refused;
        if (scenario == 0)
            refused.memoryOk = false;
        if (scenario == 1)
            refused.identityOk = false;
        if (scenario == 2)
            config.bluetooth.enabled = false;
        if (scenario == 3)
            config.power.is_power_saving = true;
        refused.applyDefaults();
        assert(channels.mutations == 0 && Preferences::writes == 0 && refused.settings.textChannel == 255);
    }
    reset();
    db.writable = false;
    BitchatBridgeModule diskFailure;
    diskFailure.applyDefaults();
    assert(!diskFailure.storageReady && !diskFailure.settings.enabled && diskFailure.settings.textChannel == 255);
    assert(channels.items[1].role == meshtastic_Channel_Role_DISABLED && channels.items[1].settings.psk.size == 0);
    reset();
    Preferences::writable = false;
    BitchatBridgeModule nvsFailure;
    nvsFailure.applyDefaults();
    assert(!nvsFailure.storageReady && !nvsFailure.settings.enabled);
    assert(channels.items[1].role == meshtastic_Channel_Role_SECONDARY);
    Preferences::writable = true;
    BitchatBridgeModule retry;
    retry.applyDefaults();
    assert(retry.settings.enabled && retry.settings.textChannel == 1 && channels.mutations == 1);

    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        reset();
        BitchatBridgeModule bad;
        if (scenario == 0)
            bad.settings.magic = 0;
        if (scenario == 1)
            bad.settings.reserved[0] = 2;
        bad.storage.putBytes("settings", &bad.settings, sizeof(bad.settings));
        if (scenario == 2)
            Preferences::data["settings"].pop_back();
        if (scenario == 3)
            Preferences::readable = false;
        if (scenario == 4)
            Preferences::available = false;
        BitchatBridgeModule corrupt;
        corrupt.applyDefaults();
        assert(!corrupt.storageReady && channels.mutations == 0);
    }
    auto wipe = [](unsigned slot) {
        channels.items[slot] = {};
        channels.items[slot].index = slot;
    };
    // The channel list was wiped after the defaults had been applied: the default channel comes back by itself.
    reset();
    {
        BitchatBridgeModule first;
        first.applyDefaults();
        assert(first.settings.textChannel == 1);
    }
    wipe(1);
    BitchatBridgeModule wiped;
    wiped.applyDefaults();
    assert(channels.items[1].role == meshtastic_Channel_Role_DISABLED && wiped.settings.textChannel == 1);
    wiped.ensureDefaultChannel();
    assert(channels.items[1].role == meshtastic_Channel_Role_SECONDARY && !strcmp(channels.items[1].settings.name, "Bitchat"));
    assert(wiped.settings.textChannel == 1 && !wiped.defaultChannelUnavailable);
    // An app wrote the unused slots with default module settings: they are still empty slots.
    reset();
    for (unsigned i = 1; i < 4; ++i)
        channels.items[i].settings.has_module_settings = true;
    BitchatBridgeModule written;
    written.applyDefaults();
    assert(written.settings.textChannel == 1);
    // No usable slot at first boot, one freed later: the channel is created then, without the menu.
    reset();
    for (unsigned i = 1; i < 4; ++i) {
        channels.items[i].settings.has_module_settings = true;
        channels.items[i].settings.module_settings.is_muted = true;
    }
    BitchatBridgeModule full;
    full.applyDefaults();
    assert(full.settings.enabled && full.settings.textChannel == 255 && full.defaultChannelUnavailable);
    wipe(2);
    full.ensureDefaultChannel();
    assert(full.settings.textChannel == 2 && !full.defaultChannelUnavailable);
    // The operator chose Bluetooth only: the channel is not created behind their back.
    reset();
    BitchatBridgeModule local;
    local.applyDefaults();
    local.settings.textChannel = 255;
    local.settings.textBinding = {};
    local.settings.reserved[2] = 1;
    assert(local.persist());
    wipe(1);
    BitchatBridgeModule stays;
    stays.ensureDefaultChannel();
    assert(channels.items[1].role == meshtastic_Channel_Role_DISABLED && stays.settings.textChannel == 255);
    // The operator chose another channel that later changed: it is not replaced by the public one.
    reset();
    BitchatBridgeModule custom;
    custom.applyDefaults();
    custom.settings.textChannel = 2;
    custom.settings.textBinding.fill(2);
    assert(custom.persist());
    const unsigned mutations = channels.mutations;
    custom.ensureDefaultChannel();
    assert(custom.settings.textChannel == 2 && channels.mutations == mutations);
    // A bridge that is off creates nothing.
    custom.settings.textChannel = 255;
    custom.settings.enabled = false;
    custom.ensureDefaultChannel();
    assert(custom.settings.textChannel == 255 && channels.mutations == mutations);
    puts("PASS: automatic public channel, legacy migration, OFF/local persistence, private-channel preservation, failures, clock "
         "discovery, default channel restored when missing");
}

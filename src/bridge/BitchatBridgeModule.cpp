#ifdef MESHTASTIC_ENABLE_BITCHAT
#include "BitchatBridgeModule.h"
#include "BridgeDefaults.h"
#include "BridgeDiagnostics.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "airtime.h"
#include "graphics/Screen.h"
#include "main.h"
#include "nimble/NimbleBluetooth.h"
#include <Curve25519.h>
#include <HKDF.h>
#include <esp_system.h>
#include <new>

using namespace bitchat;
BitchatBridgeModule *bitchatBridge = nullptr;
static constexpr uint32_t DEDUP_MS = 125000;
static constexpr uint64_t JOURNAL_MS = 600000;
static constexpr uint32_t GUARD_MS = 30000;
static constexpr uint32_t CONFLICT_MS = 90000;
static constexpr uint32_t RADIO_DEADLINE_MS = 120000;
static constexpr uint32_t BUDGET_MS = 12000;

BitchatBridgeModule::BitchatBridgeModule() : MeshModule("bitchat"), OSThread("bitchat")
{
    storageReady = storage.begin("bitchat-v1", false);
    if (storageReady && storage.isKey("settings")) {
        if (storage.getBytesLength("settings") != sizeof(settings) ||
            storage.getBytes("settings", &settings, sizeof(settings)) != sizeof(settings) || settings.magic != 0x42433031 ||
            settings.enabled > 1 || settings.economy > 1 || settings.privateTunnel > 1 || settings.reserved[0] > 1 ||
            settings.reserved[1] > 1 || settings.reserved[2] > 1) {
            settings = Settings{};
            storageReady = false;
        }
    }
    defaultsPending = storageReady && settings.reserved[0] == 0;
    for (size_t i = 0; storageReady && i < journal.size(); ++i) {
        char key[8];
        snprintf(key, sizeof(key), "j%02u", unsigned(i));
        auto &entry = journal[i];
        if (storage.isKey(key) &&
            (storage.getBytesLength(key) != sizeof(entry) || storage.getBytes(key, &entry, sizeof(entry)) != sizeof(entry)))
            storageReady = false;
    }
    if (storageReady && storage.isKey("clock") && storage.getBytesLength("clock") == sizeof(clockFloor))
        storage.getBytes("clock", &clockFloor, sizeof(clockFloor));
#ifdef BUILD_EPOCH
    clockFloor = std::max<uint64_t>(clockFloor, uint64_t(BUILD_EPOCH) * 1000);
#endif
    bootId = esp_random();
    refreshChannels();
    bleConfigure(false, false, settings.economy);
    setIntervalFromNow(1000);
}

uint64_t BitchatBridgeModule::trustedUtc() const
{
    if (!lastSetFromPhoneNtpOrGps && getRTCQuality() != RTCQualityDevice)
        return 0;
    return uint64_t(getValidTime(RTCQualityDevice)) * 1000;
}

uint64_t BitchatBridgeModule::utc() const
{
    const uint64_t trusted = trustedUtc();
    if (trusted || !peerClockUtc)
        return trusted;
    return peerClockUtc + uint32_t(millis() - peerClockAt);
}

// A radio without RTC forgets the time when power is cut. Until a trusted source returns, a phone that
// announces itself directly supplies it: never before the last trusted time, and afterwards only forward.
void BitchatBridgeModule::adoptPeerClock(Bytes frame, const Packet &packet)
{
    if (trustedUtc() || packet.type != 1 || packet.ttl != 7 || packet.timestamp < clockFloor)
        return;
    const uint64_t current = utc();
    if (current && packet.timestamp <= current + FRESHNESS_MS)
        return;
    Peer *peer = nullptr;
    if (runtime->peers->announce(crypto, frame, packet.timestamp, millis(), peer) != Result::Ok)
        return;
    peerClockUtc = packet.timestamp;
    peerClockAt = millis();
    announcedAt = peerClockAt - 60000;
}

void BitchatBridgeModule::keepClock(uint32_t now)
{
    const uint64_t trusted = trustedUtc();
    if (trusted ? (clockSaved && Throttle::isWithinTimespanMsAt(clockSavedAt, 3600000, now))
                : (!peerClockUtc || Throttle::isWithinTimespanMsAt(peerClockAt, 3600000, now)))
        return;
    if (trusted) {
        // Only a trusted time raises the floor kept across restarts; a phone could otherwise lock the bridge in the future.
        clockFloor = trusted;
        clockSaved = true;
        clockSavedAt = now;
        if (storageReady)
            storage.putBytes("clock", &clockFloor, sizeof(clockFloor));
    } else {
        peerClockUtc += uint32_t(now - peerClockAt);
        peerClockAt = now;
    }
}

bool BitchatBridgeModule::persist()
{
    settings.reserved[0] = 1;
    defaultsPending = false;
    if (!storageReady || storage.putBytes("settings", &settings, sizeof(settings)) != sizeof(settings)) {
        storageReady = false;
        settings.enabled = false;
        advertisingChanged = true;
        return false;
    }
    advertisingChanged = true;
    return true;
}

uint8_t BitchatBridgeModule::provisionPublicChannel()
{
    if (!nodeDB || owner.is_licensed)
        return 255;
    uint8_t available = 255;
    for (uint8_t i = 0; i < channels.getNumChannels(); ++i) {
        const auto &channel = channels.getByIndex(i);
        const auto &s = channel.settings;
        if (strnlen(s.name, sizeof(s.name)) == sizeof(PUBLIC_CHANNEL_NAME) - 1 &&
            strncasecmp(s.name, PUBLIC_CHANNEL_NAME, sizeof(PUBLIC_CHANNEL_NAME) - 1) == 0) {
            if (strcmp(s.name, PUBLIC_CHANNEL_NAME) != 0 || i == settings.tunnelChannel || !eligibleChannel(i) ||
                !std::equal(PUBLIC_CHANNEL_KEY.begin(), PUBLIC_CHANNEL_KEY.end(), s.psk.bytes))
                return 255;
            return i;
        }
        // Apps may write an unused slot with default module settings; that is still an empty slot.
        if (available == 255 && i != channels.getPrimaryIndex() && channel.role == meshtastic_Channel_Role_DISABLED &&
            s.name[0] == 0 && s.psk.size == 0 && s.channel_num == 0 && s.id == 0 && !s.uplink_enabled && !s.downlink_enabled &&
            (!s.has_module_settings || (s.module_settings.position_precision == 0 && !s.module_settings.is_muted)))
            available = i;
    }
    if (available == 255)
        return 255;
    const auto previous = channels.getByIndex(available);
    meshtastic_Channel channel = meshtastic_Channel_init_zero;
    channel.index = available;
    channel.role = meshtastic_Channel_Role_SECONDARY;
    channel.has_settings = true;
    strcpy(channel.settings.name, PUBLIC_CHANNEL_NAME);
    channel.settings.psk.size = PUBLIC_CHANNEL_KEY.size();
    std::copy(PUBLIC_CHANNEL_KEY.begin(), PUBLIC_CHANNEL_KEY.end(), channel.settings.psk.bytes);
    channel.settings.has_module_settings = true;
    channel.settings.module_settings.position_precision = 0;
    channels.setChannel(channel);
    channels.onConfigChanged();
    if (!nodeDB->saveToDisk(SEGMENT_CHANNELS)) {
        channels.setChannel(previous);
        channels.onConfigChanged();
        storageReady = false;
        return 255;
    }
    return available;
}

void BitchatBridgeModule::applyDefaults()
{
    if (!defaultsPending || !storageReady || !config.bluetooth.enabled || config.power.is_power_saving || !ensureRuntime())
        return;
    if (!initIdentity())
        return;
    if (settings.textChannel == 255) {
        const uint8_t channel = provisionPublicChannel();
        Digest fingerprint{};
        if (channel != 255 && binding(channel, fingerprint)) {
            settings.textChannel = channel;
            settings.textBinding = fingerprint;
        } else {
            defaultChannelUnavailable = true;
        }
    }
    // Apply this migration once; subsequent explicit OFF/local-only choices
    // survive reboot.
    settings.enabled = true;
    textGuardAt = millis();
    persist();
    refreshChannels();
}

// The public channel is part of the out-of-the-box setup. Unless the operator chose Bluetooth only or
// another channel, it is created again whenever it is missing, so a wiped channel list heals by itself.
void BitchatBridgeModule::ensureDefaultChannel()
{
    if (defaultsPending || !storageReady || !settings.enabled || settings.reserved[2] || textReady)
        return;
    Digest standard;
    publicBinding(standard);
    if (settings.textChannel != 255 && settings.textBinding != standard)
        return;
    const uint8_t channel = provisionPublicChannel();
    Digest fingerprint{};
    if (channel == 255 || !binding(channel, fingerprint)) {
        defaultChannelUnavailable = true;
        return;
    }
    settings.textChannel = channel;
    settings.textBinding = fingerprint;
    defaultChannelUnavailable = false;
    textGuardAt = millis();
    persist();
    refreshChannels();
}

void BitchatBridgeModule::updateClockWindow()
{
    // Either app can supply the time, so discovery alternates between the two until one does.
    const bool needed = settings.enabled && !utc() && (millis() / 20000) % 2 == 0;
    if (clockWindow != needed) {
        clockWindow = needed;
        advertisingChanged = true;
    }
}

bool BitchatBridgeModule::initIdentity()
{
    if (identityReady)
        return true;
    if (!storageReady)
        return false;
    if (storage.isKey("identity")) {
        if (storage.getBytesLength("identity") != identity.size() ||
            storage.getBytes("identity", identity.data(), identity.size()) != identity.size()) {
            storageReady = false;
            return false;
        }
    } else {
        if (!nimbleBluetooth || !nimbleBluetooth->isActive() || nimbleBluetooth->isDeInit)
            return false;
        esp_fill_random(identity.data(), identity.size());
        identity[32] &= 0xf8;
        identity[63] = (identity[63] & 0x7f) | 0x40;
        if (storage.putBytes("identity", identity.data(), identity.size()) != identity.size()) {
            storageReady = false;
            return false;
        }
    }
    Ed25519::derivePublicKey(signingPublic.data(), identity.data());
    if (!Curve25519::eval(noisePublic.data(), identity.data() + 32, nullptr))
        return false;
    Digest hash;
    crypto.hash({noisePublic.data(), noisePublic.size()}, hash);
    std::copy_n(hash.begin(), ownId.size(), ownId.begin());
    identityReady = true;
    return true;
}

bool BitchatBridgeModule::setEnabled(bool value)
{
    resourceError = false;
    if (value) {
        if (!config.bluetooth.enabled || config.power.is_power_saving || !ensureRuntime())
            return false;
        if (!initIdentity()) {
            releaseRuntime();
            return false;
        }
    } else {
        releaseRuntime();
    }
    settings.enabled = value;
    textGuardAt = millis();
    if (runtime)
        runtime->radioQueue.clear();
    tokens = value ? BUDGET_MS : 0;
    lastBudget = millis();
    return persist();
}

bool BitchatBridgeModule::ensureRuntime()
{
    if (runtime)
        return true;
    if (!nimbleBluetooth || !nimbleBluetooth->isActive() || nimbleBluetooth->isDeInit)
        return false;
    memoryRequest("bridge-runtime", sizeof(Runtime) + sizeof(PeerTable) + sizeof(DedupCache<256>) + bleBufferSize(),
                  std::max(sizeof(Runtime), sizeof(DedupCache<256>)));
    if (allocateBuffer(runtime) && allocateBuffer(runtime->peers) && allocateBuffer(runtime->dedup) && bleAllocateBuffers()) {
        bootDiagnostic("bridge-buffers-ready");
        return true;
    }
    releaseRuntime();
    resourceError = true;
    bootDiagnostic("bridge-memory-refused");
    return false;
}

bool BitchatBridgeModule::startBasicMode(uint8_t channel)
{
    Digest fingerprint{};
    if (channel != 255 && !binding(channel, fingerprint))
        return false;
    resourceError = false;
    if (!config.bluetooth.enabled || config.power.is_power_saving || !ensureRuntime())
        return false;
    if (!initIdentity()) {
        releaseRuntime();
        return false;
    }
    settings.textChannel = channel;
    settings.textBinding = fingerprint;
    settings.reserved[2] = channel == 255;
    settings.tunnelChannel = 255;
    settings.tunnelBinding = {};
    settings.privateTunnel = false;
    settings.enabled = true;
    defaultChannelUnavailable = false;
    textConflict = false;
    textGuardAt = millis();
    runtime->radioQueue.clear();
    tokens = BUDGET_MS;
    lastBudget = millis();
    closeNativeWindow();
    refreshChannels();
    return persist();
}

void BitchatBridgeModule::releaseRuntime()
{
    bleReleaseBuffers();
    runtime.reset();
}
bool BitchatBridgeModule::setEconomy(bool value)
{
    settings.economy = value;
    tokens = 0;
    return persist();
}
bool BitchatBridgeModule::setPrivateTunnel(bool value)
{
    settings.privateTunnel = value;
    return persist();
}
bool BitchatBridgeModule::setPrefixOnly(bool value)
{
    settings.reserved[1] = value;
    return persist();
}

bool BitchatBridgeModule::eligibleChannel(uint8_t index) const
{
    if (index >= channels.getNumChannels() || index == channels.getPrimaryIndex())
        return false;
    const auto &channel = channels.getByIndex(index);
    return channel.role == meshtastic_Channel_Role_SECONDARY && channel.settings.psk.size == 32 &&
           channel.settings.name[0] != 0 && !channel.settings.uplink_enabled && !channel.settings.downlink_enabled &&
           !channels.isDefaultChannel(index);
}

static void bindingOf(const char *name, size_t nameSize, const uint8_t *psk, Digest &out)
{
    SHA256 sha;
    static const char purpose[] = "meshtastic-bitchat/channel/v1";
    sha.update(purpose, sizeof(purpose));
    sha.update(name, nameSize);
    sha.update(psk, 32);
    sha.finalize(out.data(), out.size());
}

bool BitchatBridgeModule::binding(uint8_t index, Digest &out) const
{
    if (!eligibleChannel(index))
        return false;
    const auto &s = channels.getByIndex(index).settings;
    bindingOf(s.name, strnlen(s.name, sizeof(s.name)), s.psk.bytes, out);
    return true;
}

void BitchatBridgeModule::publicBinding(Digest &out) const
{
    bindingOf(PUBLIC_CHANNEL_NAME, sizeof(PUBLIC_CHANNEL_NAME) - 1, PUBLIC_CHANNEL_KEY.data(), out);
}

bool BitchatBridgeModule::setChannel(bool tunnel, uint8_t index)
{
    Digest fingerprint{};
    if (index != 255 && (!binding(index, fingerprint) || index == (tunnel ? settings.textChannel : settings.tunnelChannel)))
        return false;
    (tunnel ? settings.tunnelChannel : settings.textChannel) = index;
    (tunnel ? settings.tunnelBinding : settings.textBinding) = fingerprint;
    if (!tunnel) {
        settings.reserved[2] = index == 255;
        textConflict = false;
        textGuardAt = millis();
    }
    if (runtime)
        runtime->radioQueue.clear();
    bool result = persist();
    refreshChannels();
    return result;
}

void BitchatBridgeModule::refreshChannels()
{
    Digest current;
    const bool hadText = textReady;
    textReady = binding(settings.textChannel, current) && current == settings.textBinding;
    tunnelReady = binding(settings.tunnelChannel, current) && current == settings.tunnelBinding;
    if (settings.textChannel == settings.tunnelChannel) {
        textReady = false;
        tunnelReady = false;
    }
    channelsAt = millis();
    // Other bridges learn about this one from the proof in its next announcement.
    if (textReady != hadText)
        announcedAt = channelsAt - 60000;
    if (textReady && textKeyBinding != settings.textBinding) {
        const auto &s = channels.getByIndex(settings.textChannel).settings;
        static const char info[] = "meshtastic-bitchat/text/v1";
        hkdf<SHA256>(textKey.data(), textKey.size(), s.psk.bytes, 32, settings.textBinding.data(), 32, info, sizeof(info) - 1);
        textKeyBinding = settings.textBinding;
    }
    if (tunnelReady && tunnelKeyBinding != settings.tunnelBinding) {
        const auto &s = channels.getByIndex(settings.tunnelChannel).settings;
        static const char info[] = "meshtastic-bitchat/tunnel/v1";
        hkdf<SHA256>(tunnelKey.data(), tunnelKey.size(), s.psk.bytes, 32, settings.tunnelBinding.data(), 32, info,
                     sizeof(info) - 1);
        std::copy_n(settings.tunnelBinding.begin(), tunnelDomain.size(), tunnelDomain.begin());
        tunnelKeyBinding = settings.tunnelBinding;
    }
}

bool BitchatBridgeModule::textGuard() const
{
    return Throttle::isWithinTimespanMsAt(textGuardAt, GUARD_MS, millis());
}

void BitchatBridgeModule::openNativeWindow()
{
    nativeWindow = true;
    nativeOpenedAt = millis();
    advertisingChanged = true;
}

void BitchatBridgeModule::closeNativeWindow()
{
    nativeWindow = false;
    advertisingChanged = true;
}

const char *BitchatBridgeModule::status() const
{
    if (nimbleBluetooth && nimbleBluetooth->startupFailed)
        return "Bluetooth error";
    if (!storageReady)
        return "Storage error";
    if (resourceError)
        return "Low memory: bridge OFF";
    if (!settings.enabled)
        return "Bridge is off";
    if (!config.bluetooth.enabled)
        return "Bluetooth is off";
    if (config.power.is_power_saving)
        return "Power saving is on";
    if (!identityReady)
        return "Starting...";
    if (!utc())
        return "Clock not set";
    if (nativeWindow)
        return "Meshtastic app: 5 min";
    if ((settings.textChannel != 255 && !textReady) || (settings.tunnelChannel != 255 && !tunnelReady))
        return "LoRa channel changed";
    if (settings.textChannel == 255 && !settings.reserved[2] && defaultChannelUnavailable)
        return "No LoRa channel";
    if (textConflict)
        return "Standby: other bridge";
    if (textReady && textGuard())
        return "Starting LoRa: 30 s";
    return textReady ? "Ready: Bitchat + LoRa" : "Ready: Bitchat only";
}

// What the operator can do about the current status; empty when nothing is needed.
const char *BitchatBridgeModule::hint() const
{
    if ((nimbleBluetooth && nimbleBluetooth->startupFailed) || !storageReady || resourceError)
        return "Restart the radio";
    if (!settings.enabled)
        return "Menu: System > Bitchat";
    if (!config.bluetooth.enabled || config.power.is_power_saving)
        return "Fix in Meshtastic app";
    if (identityReady && !utc())
        return "Open Bitchat nearby";
    if ((settings.textChannel != 255 && !textReady) || (settings.tunnelChannel != 255 && !tunnelReady))
        return "Choose it again in menu";
    if (settings.textChannel == 255 && !settings.reserved[2] && defaultChannelUnavailable)
        return "Free a channel in app";
    return "";
}

// Four short lines for the menu: the clock the bridge is using and why phone packets were refused.
void BitchatBridgeModule::diagnostics(char *out, size_t size) const
{
    const uint32_t seconds = uint32_t(utc() / 1000);
    const auto count = [this](Result result) { return static_cast<unsigned long>(rejected[static_cast<size_t>(result)]); };
    snprintf(out, size, "UTC %02lu:%02lu:%02lu %s\nrx %lu ok %lu tx %lu\nold %lu sig %lu fmt %lu\nver %lu pol %lu full %lu",
             static_cast<unsigned long>(seconds / 3600 % 24), static_cast<unsigned long>(seconds / 60 % 60),
             static_cast<unsigned long>(seconds % 60),
             trustedUtc()   ? "app"
             : peerClockUtc ? "phone"
                            : "none",
             static_cast<unsigned long>(receivedBle), static_cast<unsigned long>(acceptedBle),
             static_cast<unsigned long>(originatedBle), count(Result::Expired) + count(Result::NoClock),
             count(Result::Unauthenticated), count(Result::Malformed) + count(Result::Oversize) + count(Result::Incomplete),
             count(Result::Unsupported), count(Result::Policy), count(Result::Full));
}

void BitchatBridgeModule::reject(Result result)
{
    if (result != Result::Ok && result != Result::Incomplete)
        ++rejected[static_cast<size_t>(result)];
}
uint32_t BitchatBridgeModule::drops() const
{
    // Duplicates and policy refusals are normal mesh traffic, not lost messages.
    return bleDrops() + lost;
}

bool BitchatBridgeModule::seen(const Digest &id) const
{
    const uint64_t now = utc();
    for (const auto &entry : journal)
        if (entry.utc && std::equal(entry.id.begin(), entry.id.end(), id.begin()) &&
            (entry.utc > now || now - entry.utc < JOURNAL_MS))
            return true;
    return false;
}

bool BitchatBridgeModule::record(const Digest &id)
{
    const uint64_t now = utc();
    if (!storageReady || !now || seen(id))
        return false;
    // A full journal gives up its oldest entry: refusing new messages would silence the bridge.
    size_t slot = 0;
    for (size_t i = 1; i < journal.size(); ++i)
        if (journal[i].utc < journal[slot].utc)
            slot = i;
    auto &entry = journal[slot];
    std::copy_n(id.begin(), entry.id.size(), entry.id.begin());
    entry.utc = now;
    char key[8];
    snprintf(key, sizeof(key), "j%02u", unsigned(slot));
    if (storage.putBytes(key, &entry, sizeof(entry)) == sizeof(entry))
        return true;
    storageReady = false;
    advertisingChanged = true;
    return false;
}

void BitchatBridgeModule::originate(uint8_t type, Bytes payload)
{
    if (!identityReady || !utc()) {
        reject(Result::NoClock);
        return;
    }
    // Two messages in the same instant still need distinct timestamps; a corrected clock must not keep an old lead.
    uint64_t timestamp = utc();
    if (lastOriginTimestamp >= timestamp && lastOriginTimestamp - timestamp < 1000)
        timestamp = lastOriginTimestamp + 1;
    lastOriginTimestamp = timestamp;
    if (encode(type, ownId, nullptr, timestamp, 7, payload, {}, runtime->working) != Result::Ok ||
        signingBytes(runtime->working.bytes(), runtime->canonical) != Result::Ok)
        return;
    std::array<uint8_t, 64> signature;
    Ed25519::sign(signature.data(), identity.data(), signingPublic.data(), runtime->canonical.data.data(),
                  runtime->canonical.size);
    if (encode(type, ownId, nullptr, timestamp, 7, payload, {signature.data(), signature.size()}, runtime->working) != Result::Ok)
        return;
    Digest id;
    packetDigest(crypto, runtime->working.bytes(), id);
    if (runtime->dedup->insert(id, millis(), DEDUP_MS) != Result::Ok) {
        reject(Result::Full);
        return;
    }
    if (!bleBroadcast(runtime->working.bytes()))
        reject(Result::Full);
    else
        ++originatedBle;
}

void BitchatBridgeModule::announce()
{
    if (!utc())
        return;
    std::array<uint8_t, 150> payload{};
    char name[24];
    snprintf(name, sizeof(name), "LoRa bridge %04lx", static_cast<unsigned long>(nodeDB->getNodeNum() & 0xffff));
    size_t length = strlen(name);
    payload[0] = 1;
    payload[1] = length;
    std::copy_n(reinterpret_cast<uint8_t *>(name), length, payload.begin() + 2);
    size_t offset = length + 2;
    payload[offset++] = 2;
    payload[offset++] = 32;
    std::copy(noisePublic.begin(), noisePublic.end(), payload.begin() + offset);
    offset += 32;
    payload[offset++] = 3;
    payload[offset++] = 32;
    std::copy(signingPublic.begin(), signingPublic.end(), payload.begin() + offset);
    offset += 32;
    if (textReady) {
        payload[offset++] = 0x7f;
        payload[offset++] = 22;
        makeGatewayProof(crypto, ownId, nodeDB->getNodeNum(), textKey.data(), payload.data() + offset);
        offset += 22;
    }
    announcedAt = millis();
    originate(1, {payload.data(), offset});
}

bool BitchatBridgeModule::queueRadio(Bytes payload, uint16_t port, uint8_t channel)
{
    if (payload.size > 233)
        return false;
    RadioItem item;
    item.size = payload.size;
    item.port = port;
    item.channel = channel;
    item.queuedAt = millis();
    std::copy_n(payload.data, payload.size, item.payload.begin());
    if (runtime->radioQueue.push(item))
        return true;
    reject(Result::Full);
    return false;
}

void BitchatBridgeModule::queueTunnel(Bytes frame, const Digest &id, bool persistent)
{
    // Larger frames cannot finish within current freshness and airtime limits.
    if (frame.size > 512) {
        reject(Result::Oversize);
        return;
    }
    size_t count = (frame.size + TUNNEL_CHUNK - 1) / TUNNEL_CHUNK;
    if (runtime->radioQueue.size() + count > RadioItem::QUEUE) {
        reject(Result::Full);
        return;
    }
    if (persistent && !record(id))
        return;
    TunnelIdentity envelope;
    envelope.domain = tunnelDomain;
    envelope.origin = nodeDB->getNodeNum();
    envelope.boot = bootId;
    envelope.sequence = ++sequence;
    for (size_t i = 0; i < count; ++i) {
        std::array<uint8_t, TUNNEL_MAX_FRAGMENT> fragment;
        size_t length = 0;
        Result result =
            encodeTunnelFragment(crypto, frame, envelope, tunnelKey.data(), i, fragment.data(), fragment.size(), length);
        if (result != Result::Ok) {
            reject(result);
            return;
        }
        queueRadio({fragment.data(), length}, meshtastic_PortNum_PRIVATE_APP, settings.tunnelChannel);
    }
}

// Returns true when the frame cost a signature check, so the caller can pace the next one.
bool BitchatBridgeModule::handleFrame(Bytes frame, uint16_t source, bool fromTunnel)
{
    const uint32_t now = millis();
    Packet packet;
    Result result = decode(frame, packet);
    if (result != Result::Ok) {
        reject(result);
        return false;
    }
    if (packet.type == 0x20) {
        if (!allocateBuffer(runtime->fragments)) {
            reject(Result::Full);
            ++lost;
            return false;
        }
        result = runtime->fragments->accept(packet, now, runtime->assembled);
        if (result != Result::Ok) {
            reject(result);
            return false;
        }
        frame = runtime->assembled.bytes();
        if (decode(frame, packet) != Result::Ok)
            return false;
    }
    if (packet.sender == ownId || packet.type == 0x21 || packet.replay()) {
        reject(Result::Policy);
        return false;
    }
    if (!fromTunnel)
        adoptPeerClock(frame, packet);
    Digest id;
    result = packetDigest(crypto, frame, id);
    // Every phone relays what it hears, so most arrivals are copies: drop them before the signature check.
    if (result == Result::Ok && runtime->dedup->contains(id, now, DEDUP_MS))
        result = Result::Duplicate;
    if (result != Result::Ok) {
        reject(result);
        return false;
    }
    Peer *peer = nullptr;
    result = runtime->peers->authenticate(crypto, frame, utc(), now, peer);
    if (result == Result::Ok && !peer)
        result = Result::Unauthenticated;
    if (result == Result::Ok)
        result = runtime->dedup->insert(id, now, DEDUP_MS);
    if (result != Result::Ok) {
        reject(result);
        return true;
    }
    ++acceptedBle;
    uint32_t other = 0;
    // Two bridges on one channel would duplicate every text: the higher node number stands by.
    if (!fromTunnel && textReady && packet.type == 1 &&
        gatewayProofNode(crypto, packet, nodeDB->getNodeNum(), textKey.data(), other) && other < nodeDB->getNodeNum()) {
        textConflict = true;
        conflictAt = now;
    }
    if (!peer->traffic.admit(now, 30)) {
        reject(Result::Full);
        ++lost;
        return true;
    }
    if (fromTunnel && packet.type != 1 && !record(id)) {
        reject(Result::Duplicate);
        return true;
    }
    if (relay(frame, runtime->working) == Result::Ok) {
        if (bleBroadcast(runtime->working.bytes(), source))
            ++forwardedBle;
    }
    if (fromTunnel)
        return true;
    const bool publicMessage = packet.type == 2 && !packet.directed();
    if (textReady && publicMessage && !startsWith(packet.payload, TUNNEL_PREFIX) &&
        (!settings.reserved[1] || startsWith(packet.payload, LORA_PREFIX))) {
        exportText(packet, *peer, id);
        return true;
    }
    if (tunnelReady) {
        const bool publicText = publicMessage && !packet.compressed() && startsWith(packet.payload, TUNNEL_PREFIX) &&
                                packet.payload.size <= TUNNEL_TEXT_LIMIT && validUtf8(packet.payload);
        const bool opaque = settings.privateTunnel && (packet.type == 0x10 || packet.type == 0x11 || packet.type == 0x25 ||
                                                       packet.type == 0x24 || packet.type == 0x26 || packet.type == 0x27);
        const bool presence = packet.type == 1 && peer->presence.admit(now, 30000);
        if (presence || ((publicText || opaque) && peer->lora.admit(now, 4, opaque ? 1000 : 10000))) {
            if (relay(frame, runtime->working) == Result::Ok && (presence || !seen(id)))
                queueTunnel(runtime->working.bytes(), id, !presence);
        }
    }
    return true;
}

void BitchatBridgeModule::exportText(const Packet &packet, Peer &peer, const Digest &id)
{
    Bytes body;
    Result result = publicTextForLoRa(packet, body);
    // Text from another bridge already came from LoRa: sending it back would loop.
    if (result == Result::Ok && (peer.announcement.gateway || textConflict))
        result = Result::Policy;
    if (result == Result::Ok && seen(id))
        result = Result::Duplicate;
    if (result != Result::Ok) {
        reject(result);
        return;
    }
    char label[LABEL_LIMIT + 8];
    size_t labelSize = cleanLabel({reinterpret_cast<const uint8_t *>(peer.announcement.nickname.data()),
                                   strnlen(peer.announcement.nickname.data(), peer.announcement.nickname.size())},
                                  label, LABEL_LIMIT - 4);
    // Same rule as the Bitchat app: equal nicknames are told apart by the start of the peer ID.
    if (!labelSize || runtime->peers->nicknameShared(peer))
        labelSize += snprintf(label + labelSize, 6, "#%02x%02x", peer.id[0], peer.id[1]);
    TextPlan plan;
    result = planNativeText(labelSize, body, plan);
    if (result == Result::Ok && (textGuard() || runtime->radioQueue.size() + plan.count > RadioItem::QUEUE ||
                                 !peer.lora.admit(millis(), 6, settings.economy ? 30000 : 6000) || !record(id)))
        result = Result::Full;
    if (result != Result::Ok) {
        reject(result);
        ++lost;
        return;
    }
    for (size_t part = 0; part < plan.count; ++part) {
        NativeText native;
        if (makeNativeText({reinterpret_cast<const uint8_t *>(label), labelSize}, body, plan, part, native) == Result::Ok)
            queueRadio(native.bytes(), meshtastic_PortNum_TEXT_MESSAGE_APP, settings.textChannel);
    }
}

bool BitchatBridgeModule::wantPacket(const meshtastic_MeshPacket *packet)
{
    return settings.enabled && runtime && wasActive &&
           (packet->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
            packet->decoded.portnum == meshtastic_PortNum_PRIVATE_APP);
}

ProcessMessage BitchatBridgeModule::handleReceived(const meshtastic_MeshPacket &packet)
{
    if (!wantPacket(&packet) || !storageReady || !identityReady || !utc() || packet.to != NODENUM_BROADCAST || packet.via_mqtt ||
        packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return ProcessMessage::CONTINUE;
    refreshChannels();
    Bytes payload{packet.decoded.payload.bytes, packet.decoded.payload.size};
    if (textReady && !textConflict && !textGuard() && packet.channel == settings.textChannel &&
        packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        // Bitchat keeps no mailbox on the bridge: without a phone connected there is nobody to deliver to.
        if (!bleSubscribers())
            return ProcessMessage::CONTINUE;
        const NodeNum from = getFrom(&packet);
        std::array<uint8_t, 42> material{};
        material[0] = 'M';
        material[1] = 'T';
        std::copy(settings.textBinding.begin(), settings.textBinding.end(), material.begin() + 2);
        write32(material.data() + 34, from);
        write32(material.data() + 38, packet.id);
        Digest id;
        crypto.hash({material.data(), material.size()}, id);
        if (seen(id)) {
            reject(Result::Duplicate);
            return ProcessMessage::CONTINUE;
        }
        char label[LABEL_LIMIT + 1];
        const auto *node = nodeDB->getMeshNode(from);
        size_t labelSize = 0;
        if (node && node->has_user)
            labelSize = cleanLabel({reinterpret_cast<const uint8_t *>(node->user.long_name),
                                    strnlen(node->user.long_name, sizeof(node->user.long_name))},
                                   label, sizeof(label));
        if (!labelSize)
            labelSize = snprintf(label, sizeof(label), "!%08lx", static_cast<unsigned long>(from));
        const Bytes name{reinterpret_cast<const uint8_t *>(label), labelSize};
        TextPlan plan;
        Result result = planBitchatText(labelSize, payload, plan);
        if (result == Result::Ok && record(id)) {
            // A phone that just connected may not know the bridge key yet; its messages would be discarded.
            if (!Throttle::isWithinTimespanMsAt(announcedAt, 5000, millis()))
                announce();
            for (size_t part = 0; part < plan.count; ++part) {
                NativeText text;
                if (makeBitchatText(name, payload, plan, part, text) == Result::Ok)
                    originate(2, text.bytes());
            }
            ++importedRadio;
        } else {
            reject(result == Result::Ok ? Result::Full : result);
            ++lost;
        }
    } else if (tunnelReady && packet.channel == settings.tunnelChannel &&
               packet.decoded.portnum == meshtastic_PortNum_PRIVATE_APP) {
        TunnelFragment view;
        Result result = decodeTunnelFragment(crypto, payload, tunnelDomain, tunnelKey.data(), view);
        if (result == Result::Ok && view.total > 512)
            result = Result::Oversize;
        if (result == Result::Ok && view.identity.origin != packet.from)
            result = Result::Unauthenticated;
        if (result == Result::Ok && !allocateBuffer(runtime->tunnelFragments))
            result = Result::Full;
        if (result == Result::Ok)
            result = runtime->tunnelFragments->accept(crypto, view, millis(), runtime->assembled);
        if (result == Result::Ok) {
            Packet inner;
            if (decode(runtime->assembled.bytes(), inner) == Result::Ok &&
                (inner.type == 1 ||
                 (inner.type == 2 && !inner.directed() && !inner.compressed() && startsWith(inner.payload, TUNNEL_PREFIX) &&
                  inner.payload.size <= TUNNEL_TEXT_LIMIT && validUtf8(inner.payload)) ||
                 (settings.privateTunnel && (inner.type == 0x10 || inner.type == 0x11 || inner.type == 0x25 ||
                                             inner.type == 0x24 || inner.type == 0x26 || inner.type == 0x27))))
                handleFrame(runtime->assembled.bytes(), NO_LINK, true);
            else
                reject(Result::Policy);
        } else
            reject(result);
    }
    return ProcessMessage::CONTINUE;
}

void BitchatBridgeModule::sendRadio()
{
    auto *item = runtime->radioQueue.front();
    if (!item)
        return;
    if (!Throttle::isWithinTimespanMsAt(item->queuedAt, RADIO_DEADLINE_MS, millis()) ||
        (item->port == meshtastic_PortNum_TEXT_MESSAGE_APP ? (!textReady || textConflict) : !tunnelReady)) {
        reject(Result::Expired);
        ++lost;
        runtime->radioQueue.pop();
        return;
    }
    if (!airTime || !airTime->isTxAllowedAirUtil() || !airTime->isTxAllowedChannelUtil(true) ||
        !runtime->radioGate.admit(millis(), 1500))
        return;
    auto *packet = router->allocForSending();
    if (!packet) {
        reject(Result::Full);
        return;
    }
    packet->to = NODENUM_BROADCAST;
    packet->channel = item->channel;
    packet->decoded.portnum = static_cast<meshtastic_PortNum>(item->port);
    packet->decoded.payload.size = item->size;
    std::copy_n(item->payload.begin(), item->size, packet->decoded.payload.bytes);
    // As with text typed in the Meshtastic app, the radio repeats chat text nobody was heard relaying.
    packet->want_ack = item->port == meshtastic_PortNum_TEXT_MESSAGE_APP && !settings.economy;
    // Chat text travels like text typed in the Meshtastic app; tunnel fragments yield to everything else.
    packet->priority = item->port == meshtastic_PortNum_TEXT_MESSAGE_APP ? meshtastic_MeshPacket_Priority_DEFAULT
                                                                         : meshtastic_MeshPacket_Priority_BACKGROUND;
    uint32_t cost = router->bridgeAirtime(packet);
    if (cost > tokens || router->getQueueStatus().free < 2) {
        packetPool.release(packet);
        return;
    }
    tokens -= cost;
    runtime->radioQueue.pop();
    ++submittedRadio;
    service->sendToMesh(packet, RX_SRC_LOCAL, true);
}

int32_t BitchatBridgeModule::runOnce()
{
    const uint32_t now = millis();
    const bool bluetoothReady = nimbleBluetooth && nimbleBluetooth->isActive() && !nimbleBluetooth->isDeInit;
    if (bluetoothReady && !resourceError)
        applyDefaults();
    if (!Throttle::isWithinTimespanMsAt(defaultChannelAt, 10000, now)) {
        defaultChannelAt = now;
        ensureDefaultChannel();
    }
    if (settings.enabled && storageReady && config.bluetooth.enabled && !config.power.is_power_saving && bluetoothReady &&
        !resourceError && ensureRuntime() && !identityReady)
        initIdentity();
    if (nativeWindow && !Throttle::isWithinTimespanMsAt(nativeOpenedAt, 300000, now)) {
        nativeWindow = false;
        advertisingChanged = true;
    }
    const bool active = settings.enabled && storageReady && identityReady && runtime && bluetoothReady &&
                        config.bluetooth.enabled && !config.power.is_power_saving;
    if (!active && runtime)
        releaseRuntime();
    if (active != wasActive) {
        advertisingChanged = true;
        tokens = active ? BUDGET_MS : 0;
        lastBudget = now;
        wasActive = active;
    }
    updateClockWindow();
    if (advertisingChanged) {
        bleConfigure(active, nativeWindow || clockWindow, settings.economy);
        if (nimbleBluetooth && nimbleBluetooth->isActive() && !nimbleBluetooth->isDeInit)
            nimbleBluetooth->refreshBridgeAdvertising();
        advertisingChanged = false;
    }
    if (!active)
        return 1000;
    keepClock(now);
    if (!Throttle::isWithinTimespanMsAt(channelsAt, 1000, now))
        refreshChannels();
    if (textConflict && !Throttle::isWithinTimespanMsAt(conflictAt, CONFLICT_MS, now)) {
        textConflict = false;
        textGuardAt = now;
    }
    const uint32_t elapsed = now - lastBudget;
    if (elapsed >= 1000) {
        // Extra LoRa airtime the bridge allows itself: 10% of the time, or 1% on battery.
        tokens = std::min<uint32_t>(BUDGET_MS, tokens + std::min<uint32_t>(elapsed, 60000) / (settings.economy ? 100 : 10));
        lastBudget = now;
    }
    // Signing costs CPU: announce to a phone that just subscribed, then only while someone listens.
    if (bleNeedsAnnouncement() || (bleSubscribers() && !Throttle::isWithinTimespanMsAt(announcedAt, 20000, now)))
        announce();
    if (runtime->cryptoGate.admit(now, 30))
        for (unsigned frames = 0; frames < 4 && bleReceive(runtime->input); ++frames) {
            ++receivedBle;
            if (handleFrame(runtime->input.frame.bytes(), runtime->input.link, false))
                break;
        }
    blePoll();
    if (runtime->fragments) {
        runtime->fragments->expire(now);
        if (runtime->fragments->empty())
            runtime->fragments.reset();
    }
    if (runtime->tunnelFragments) {
        runtime->tunnelFragments->expire(now);
        if (!tunnelReady || runtime->tunnelFragments->empty())
            runtime->tunnelFragments.reset();
    }
    sendRadio();
    return 20;
}

#if HAS_SCREEN
static void displayCount(uint32_t value, char out[8])
{
    const char *suffix = "";
    if (value >= 1000000000) {
        value /= 1000000000;
        suffix = "G";
    } else if (value >= 1000000) {
        value /= 1000000;
        suffix = "M";
    } else if (value >= 100000) {
        value /= 1000;
        suffix = "k";
    }
    snprintf(out, 8, "%lu%s", static_cast<unsigned long>(value), suffix);
}

void BitchatBridgeModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const int16_t row = FONT_HEIGHT_SMALL - 1;
    display->drawString(x, y, "Bitchat bridge");
    display->drawString(x, y + row, status());
    char line[60];
    snprintf(line, sizeof(line), "Phones %u  People %u", unsigned(bleSubscribers()),
             unsigned(runtime ? runtime->peers->size() : 0));
    display->drawString(x, y + 2 * row, line);
    char tx[8], rx[8], missed[8];
    displayCount(submittedRadio, tx);
    displayCount(importedRadio, rx);
    displayCount(drops(), missed);
    snprintf(line, sizeof(line), "LoRa out %s  in %s", tx, rx);
    display->drawString(x, y + 3 * row, line);
    if (hint()[0])
        snprintf(line, sizeof(line), "%s", hint());
    else if (drops())
        snprintf(line, sizeof(line), "Not delivered %s", missed);
    else
        snprintf(line, sizeof(line), "Menu: System > Bitchat");
    display->drawString(x, y + 4 * row, line);
}
#endif
#endif

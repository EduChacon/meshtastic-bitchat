#ifdef MESHTASTIC_ENABLE_BITCHAT
#include "BitchatBridgeModule.h"
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

BitchatBridgeModule::BitchatBridgeModule() : MeshModule("bitchat"), OSThread("bitchat")
{
    storageReady = storage.begin("bitchat-v1", false);
    if (storageReady && storage.isKey("settings")) {
        if (storage.getBytesLength("settings") != sizeof(settings) ||
            storage.getBytes("settings", &settings, sizeof(settings)) != sizeof(settings) || settings.magic != 0x42433031 ||
            settings.enabled > 1 || settings.economy > 1 || settings.privateTunnel > 1) {
            settings = Settings{};
            storageReady = false;
        }
    }
    for (size_t i = 0; storageReady && i < journal.size(); ++i) {
        char key[8];
        snprintf(key, sizeof(key), "j%02u", unsigned(i));
        auto &entry = journal[i];
        if (storage.isKey(key) &&
            (storage.getBytesLength(key) != sizeof(entry) || storage.getBytes(key, &entry, sizeof(entry)) != sizeof(entry)))
            storageReady = false;
    }
    bootId = esp_random();
    refreshChannels();
    bleConfigure(false, false, settings.economy);
    setIntervalFromNow(1000);
}

uint64_t BitchatBridgeModule::utc() const
{
    if (!lastSetFromPhoneNtpOrGps && getRTCQuality() != RTCQualityDevice)
        return 0;
    return uint64_t(getValidTime(RTCQualityDevice)) * 1000;
}

bool BitchatBridgeModule::persist()
{
    if (!storageReady || storage.putBytes("settings", &settings, sizeof(settings)) != sizeof(settings)) {
        storageReady = false;
        settings.enabled = false;
        advertisingChanged = true;
        return false;
    }
    advertisingChanged = true;
    return true;
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
    tokens = value ? 12000 : 0;
    lastBudget = millis();
    return persist();
}

bool BitchatBridgeModule::ensureRuntime()
{
    if (runtime)
        return true;
    if (!nimbleBluetooth || !nimbleBluetooth->isActive() || nimbleBluetooth->isDeInit)
        return false;
    const size_t bleSize = bleBufferSize();
    if (memoryAvailable(sizeof(Runtime) + bleSize, std::max(sizeof(Runtime), bleSize))) {
        runtime.reset(new (std::nothrow) Runtime);
        if (runtime && bleAllocateBuffers() && memoryAvailable(0, 8192)) {
            bootDiagnostic("bridge-buffers-ready");
            return true;
        }
    }
    releaseRuntime();
    resourceError = true;
    bootDiagnostic("bridge-memory-refused");
    return false;
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

bool BitchatBridgeModule::eligibleChannel(uint8_t index) const
{
    if (index >= channels.getNumChannels() || index == channels.getPrimaryIndex())
        return false;
    const auto &channel = channels.getByIndex(index);
    return channel.role == meshtastic_Channel_Role_SECONDARY && channel.settings.psk.size == 32 &&
           channel.settings.name[0] != 0 && !channel.settings.uplink_enabled && !channel.settings.downlink_enabled &&
           !channels.isDefaultChannel(index);
}

bool BitchatBridgeModule::binding(uint8_t index, Digest &out) const
{
    if (!eligibleChannel(index))
        return false;
    const auto &s = channels.getByIndex(index).settings;
    SHA256 sha;
    static const char purpose[] = "meshtastic-bitchat/channel/v1";
    sha.update(purpose, sizeof(purpose));
    sha.update(s.name, strnlen(s.name, sizeof(s.name)));
    sha.update(s.psk.bytes, 32);
    sha.finalize(out.data(), out.size());
    return true;
}

bool BitchatBridgeModule::setChannel(bool tunnel, uint8_t index)
{
    Digest fingerprint{};
    if (index != 255 && (!binding(index, fingerprint) || index == (tunnel ? settings.textChannel : settings.tunnelChannel)))
        return false;
    (tunnel ? settings.tunnelChannel : settings.textChannel) = index;
    (tunnel ? settings.tunnelBinding : settings.textBinding) = fingerprint;
    if (!tunnel) {
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
    textReady = binding(settings.textChannel, current) && current == settings.textBinding;
    tunnelReady = binding(settings.tunnelChannel, current) && current == settings.tunnelBinding;
    if (settings.textChannel == settings.tunnelChannel) {
        textReady = false;
        tunnelReady = false;
    }
    if (textReady) {
        const auto &s = channels.getByIndex(settings.textChannel).settings;
        static const char info[] = "meshtastic-bitchat/text/v1";
        hkdf<SHA256>(textKey.data(), textKey.size(), s.psk.bytes, 32, settings.textBinding.data(), 32, info, sizeof(info) - 1);
    }
    if (tunnelReady) {
        const auto &s = channels.getByIndex(settings.tunnelChannel).settings;
        static const char info[] = "meshtastic-bitchat/tunnel/v1";
        hkdf<SHA256>(tunnelKey.data(), tunnelKey.size(), s.psk.bytes, 32, settings.tunnelBinding.data(), 32, info,
                     sizeof(info) - 1);
        std::copy_n(settings.tunnelBinding.begin(), tunnelDomain.size(), tunnelDomain.begin());
    }
}

void BitchatBridgeModule::openNativeWindow()
{
    nativeWindow = true;
    nativeOpenedAt = millis();
    advertisingChanged = true;
}

const char *BitchatBridgeModule::status() const
{
    if (!storageReady)
        return "Storage error: stopped";
    if (resourceError)
        return "Low memory: bridge OFF";
    if (!settings.enabled)
        return "OFF - stock BLE";
    if (!config.bluetooth.enabled || config.power.is_power_saving)
        return "Enable BLE; sleep OFF";
    if (!identityReady)
        return "Identity unavailable";
    if (!utc())
        return "Open Meshtastic: clock";
    if (nativeWindow)
        return "Meshtastic window 5m";
    if (textConflict)
        return "B conflict: stop overlap";
    if (textReady && Throttle::isWithinTimespanMsAt(textGuardAt, 30000, millis()))
        return "B listening: wait 30s";
    if ((settings.textChannel != 255 && !textReady) || (settings.tunnelChannel != 255 && !tunnelReady))
        return "Channel changed: blocked";
    return settings.economy ? "BC economy (available)" : "BC fixed (available)";
}

void BitchatBridgeModule::reject(Result result)
{
    if (result != Result::Ok && result != Result::Incomplete)
        ++rejected[static_cast<size_t>(result)];
}
uint32_t BitchatBridgeModule::drops() const
{
    uint32_t count = bleDrops();
    for (auto value : rejected)
        count += value;
    return count;
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
    if (!storageReady || !utc() || seen(id))
        return false;
    for (size_t i = 0; i < journal.size(); ++i) {
        auto &entry = journal[i];
        if (!entry.utc || (utc() >= entry.utc && utc() - entry.utc >= JOURNAL_MS)) {
            std::copy_n(id.begin(), entry.id.size(), entry.id.begin());
            entry.utc = utc();
            char key[8];
            snprintf(key, sizeof(key), "j%02u", unsigned(i));
            if (storage.putBytes(key, &entry, sizeof(entry)) == sizeof(entry))
                return true;
            storageReady = false;
            advertisingChanged = true;
            return false;
        }
    }
    reject(Result::Full);
    return false;
}

void BitchatBridgeModule::originate(uint8_t type, Bytes payload)
{
    if (!identityReady || !utc()) {
        reject(Result::NoClock);
        return;
    }
    uint64_t timestamp = std::max(utc(), lastOriginTimestamp + 1);
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
    if (runtime->dedup.insert(id, millis(), DEDUP_MS) != Result::Ok) {
        reject(Result::Full);
        return;
    }
    if (!bleBroadcast(runtime->working.bytes()))
        reject(Result::Full);
}

void BitchatBridgeModule::announce()
{
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
    if (runtime->radioQueue.size() + count > 4) {
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

void BitchatBridgeModule::handleFrame(Bytes frame, uint16_t source, bool fromTunnel)
{
    Packet packet;
    Result result = decode(frame, packet);
    if (result != Result::Ok) {
        reject(result);
        return;
    }
    if (packet.type == 0x20) {
        result = runtime->fragments.accept(packet, millis(), runtime->assembled);
        if (result != Result::Ok) {
            reject(result);
            return;
        }
        frame = runtime->assembled.bytes();
        if (decode(frame, packet) != Result::Ok)
            return;
    }
    if (packet.sender == ownId || packet.type == 0x21 || packet.replay()) {
        reject(Result::Policy);
        return;
    }
    Peer *peer = nullptr;
    result = runtime->peers.authenticate(crypto, frame, utc(), millis(), peer);
    if (result != Result::Ok || !peer) {
        reject(result);
        return;
    }
    Digest id;
    result = packetDigest(crypto, frame, id);
    if (result == Result::Ok)
        result = runtime->dedup.insert(id, millis(), DEDUP_MS);
    if (result != Result::Ok) {
        reject(result);
        return;
    }
    if (!fromTunnel && textReady && packet.type == 1 &&
        containsGatewayProof(crypto, packet, nodeDB->getNodeNum(), textKey.data())) {
        textConflict = true;
    }
    if (!peer->traffic.admit(millis(), 30)) {
        reject(Result::Full);
        return;
    }
    if (fromTunnel && packet.type != 1 && !record(id)) {
        reject(Result::Duplicate);
        return;
    }
    if (relay(frame, runtime->working) == Result::Ok) {
        if (bleBroadcast(runtime->working.bytes(), source))
            ++forwardedBle;
    }
    if (fromTunnel)
        return;
    Bytes body;
    Result textResult = publicTextForLoRa(packet, body);
    if (textReady && packet.type == 2 && !packet.directed() && startsWith(packet.payload, "!lora ")) {
        if (textResult != Result::Ok || textConflict || Throttle::isWithinTimespanMsAt(textGuardAt, 30000, millis())) {
            reject(textResult == Result::Ok ? Result::Policy : textResult);
            return;
        }
        if (!peer->lora.admit(millis(), settings.economy ? 120000 : 30000) || seen(id)) {
            reject(Result::Duplicate);
            return;
        }
        NativeText native;
        result = makeNativeText(crypto, packet.sender, body, id, textKey.data(), native);
        if (result == Result::Ok && runtime->radioQueue.size() < 4 && record(id))
            queueRadio(native.bytes(), meshtastic_PortNum_TEXT_MESSAGE_APP, settings.textChannel);
        else
            reject(result == Result::Ok ? Result::Full : result);
        return;
    }
    if (tunnelReady) {
        const bool publicText = packet.type == 2 && !packet.directed() && !packet.compressed() &&
                                startsWith(packet.payload, "!bctun ") && packet.payload.size <= TEXT_LIMIT + 7 &&
                                validUtf8(packet.payload);
        const bool opaque = settings.privateTunnel && (packet.type == 0x10 || packet.type == 0x11 || packet.type == 0x25 ||
                                                       packet.type == 0x24 || packet.type == 0x26 || packet.type == 0x27);
        const bool presence = packet.type == 1 && peer->presence.admit(millis(), 30000);
        if (presence || ((publicText || opaque) && peer->lora.admit(millis(), opaque ? 1000 : 30000))) {
            if (relay(frame, runtime->working) == Result::Ok && (presence || !seen(id)))
                queueTunnel(runtime->working.bytes(), id, !presence);
        }
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
    if (textReady && !textConflict && !Throttle::isWithinTimespanMsAt(textGuardAt, 30000, millis()) &&
        packet.channel == settings.textChannel && packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        std::array<uint8_t, 42> material{};
        material[0] = 'M';
        material[1] = 'T';
        std::copy(settings.textBinding.begin(), settings.textBinding.end(), material.begin() + 2);
        write32(material.data() + 34, packet.from);
        write32(material.data() + 38, packet.id);
        Digest id;
        crypto.hash({material.data(), material.size()}, id);
        std::array<uint8_t, 16> tag;
        if (readBridgeTag(crypto, payload, textKey.data(), tag))
            std::copy(tag.begin(), tag.end(), id.begin());
        if (seen(id)) {
            reject(Result::Duplicate);
            return ProcessMessage::CONTINUE;
        }
        NativeText text;
        Result result = makeBitchatText(packet.from, payload, text);
        if (result == Result::Ok && record(id)) {
            announce();
            originate(2, text.bytes());
            ++importedRadio;
        } else
            reject(result == Result::Ok ? Result::Full : result);
    } else if (tunnelReady && packet.channel == settings.tunnelChannel &&
               packet.decoded.portnum == meshtastic_PortNum_PRIVATE_APP) {
        TunnelFragment view;
        Result result = decodeTunnelFragment(crypto, payload, tunnelDomain, tunnelKey.data(), view);
        if (result == Result::Ok && view.total > 512)
            result = Result::Oversize;
        if (result == Result::Ok && view.identity.origin != packet.from)
            result = Result::Unauthenticated;
        if (result == Result::Ok)
            result = runtime->tunnelFragments.accept(crypto, view, millis(), runtime->assembled);
        if (result == Result::Ok) {
            Packet inner;
            if (decode(runtime->assembled.bytes(), inner) == Result::Ok &&
                (inner.type == 1 ||
                 (inner.type == 2 && !inner.directed() && !inner.compressed() && startsWith(inner.payload, "!bctun ") &&
                  inner.payload.size <= TEXT_LIMIT + 7 && validUtf8(inner.payload)) ||
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
    if (!Throttle::isWithinTimespanMsAt(item->queuedAt, 45000, millis()) ||
        (item->port == meshtastic_PortNum_TEXT_MESSAGE_APP ? (!textReady || textConflict) : !tunnelReady)) {
        reject(Result::Expired);
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
    packet->want_ack = false;
    packet->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
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
        tokens = active ? 12000 : 0;
        lastBudget = now;
        wasActive = active;
    }
    if (advertisingChanged) {
        bleConfigure(active, nativeWindow, settings.economy);
        if (nimbleBluetooth && nimbleBluetooth->isActive() && !nimbleBluetooth->isDeInit)
            nimbleBluetooth->refreshBridgeAdvertising();
        advertisingChanged = false;
    }
    if (!active)
        return 1000;
    refreshChannels();
    const uint32_t elapsed = now - lastBudget;
    if (elapsed >= 1000) {
        tokens = std::min<uint32_t>(12000, tokens + std::min<uint32_t>(elapsed, 60000) / (settings.economy ? 100 : 20));
        lastBudget = now;
    }
    if (bleNeedsAnnouncement() || runtime->announceGate.admit(now, 20000))
        announce();
    if (runtime->cryptoGate.admit(now, 30) && bleReceive(runtime->input))
        handleFrame(runtime->input.frame.bytes(), runtime->input.link, false);
    blePoll();
    runtime->fragments.expire(now);
    runtime->tunnelFragments.expire(now);
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
    display->drawString(x, y, "Bitchat bridge - EXP");
    display->drawString(x, y + row, status());
    char line[60];
    snprintf(line, sizeof(line), "BLE%u P%u D%lu", unsigned(bleSubscribers()), unsigned(runtime ? runtime->peers.size() : 0),
             static_cast<unsigned long>(drops()));
    display->drawString(x, y + 2 * row, line);
    char tx[8], rx[8];
    displayCount(submittedRadio, tx);
    displayCount(importedRadio, rx);
    snprintf(line, sizeof(line), "TX%s RX%s Q%u", tx, rx, unsigned(runtime ? runtime->radioQueue.size() : 0));
    display->drawString(x, y + 3 * row, line);
    display->drawString(x, y + 4 * row, "Menu > System > Bitchat");
}
#endif
#endif

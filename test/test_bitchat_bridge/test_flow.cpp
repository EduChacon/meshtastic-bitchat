// End-to-end host test: the production bridge module and BLE adapter, three simulated phones and a simulated radio.
#include "HeapModel.h"
#include "PowerStatus.h"
#include "bridge/BitchatBridgeModule.h"
#include "bridge/BridgeDefaults.h"
#include "cashu_vectors.h"
#include "gcs_reference.h"
#include <Curve25519.h>
#include <HKDF.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace bitchat;
BridgeCrypto crypto;
static size_t checks = 0;
static void check(bool passed, int line, const char *expression)
{
    ++checks;
    if (!passed) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        std::exit(1);
    }
}
#define CHECK(expression) check((expression), __LINE__, #expression)

struct Bridge : BitchatBridgeModule {
    using BitchatBridgeModule::handleReceived;
    using BitchatBridgeModule::runOnce;
};
static Bridge *bridge;
static NimBLEServer server;
static uint64_t stamp = 0, forcedStamp = 0;

static uint64_t nowUtc()
{
    return uint64_t(fakeEpoch + millis() / 1000) * 1000;
}
static void run(uint32_t ms)
{
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 20) {
        fakeNow += 20;
        bridge->runOnce();
    }
}
static std::string text(Bytes bytes)
{
    return {reinterpret_cast<const char *>(bytes.data), bytes.size};
}

struct Phone {
    std::array<uint8_t, 32> signingKey{}, signingPublic{}, noiseKey{}, noisePublic{};
    PeerId id{};
    std::string nickname;
    uint16_t handle = NO_LINK;
    Phone(const char *name, uint8_t seed, uint16_t link = NO_LINK) : nickname(name), handle(link)
    {
        for (size_t i = 0; i < 32; ++i) {
            signingKey[i] = uint8_t(seed * 31 + i);
            noiseKey[i] = uint8_t(seed * 17 + 3 * i);
        }
        noiseKey[0] &= 0xf8;
        noiseKey[31] = (noiseKey[31] & 0x7f) | 0x40;
        Ed25519::derivePublicKey(signingPublic.data(), signingKey.data());
        Curve25519::eval(noisePublic.data(), noiseKey.data(), nullptr);
        Digest hash;
        crypto.hash({noisePublic.data(), noisePublic.size()}, hash);
        std::copy_n(hash.begin(), id.size(), id.begin());
    }
    Frame packet(uint8_t type, Bytes payload, uint8_t ttl = 7) const
    {
        Frame unsignedFrame, canonical, frame;
        stamp = forcedStamp ? forcedStamp : std::max(stamp + 1, nowUtc());
        CHECK(encode(type, id, nullptr, stamp, ttl, payload, {}, unsignedFrame) == Result::Ok);
        CHECK(signingBytes(unsignedFrame.bytes(), canonical) == Result::Ok);
        std::array<uint8_t, 64> signature;
        Ed25519::sign(signature.data(), signingKey.data(), signingPublic.data(), canonical.data.data(), canonical.size);
        CHECK(encode(type, id, nullptr, stamp, ttl, payload, {signature.data(), signature.size()}, frame) == Result::Ok);
        return frame;
    }
    Frame announce(Bytes extra = {}, uint8_t ttl = 7) const
    {
        std::vector<uint8_t> payload{1, uint8_t(nickname.size())};
        payload.insert(payload.end(), nickname.begin(), nickname.end());
        payload.insert(payload.end(), {2, 32});
        payload.insert(payload.end(), noisePublic.begin(), noisePublic.end());
        payload.insert(payload.end(), {3, 32});
        payload.insert(payload.end(), signingPublic.begin(), signingPublic.end());
        payload.insert(payload.end(), extra.data, extra.data + extra.size);
        return packet(1, {payload.data(), payload.size()}, ttl);
    }
    Frame say(const std::string &message) const
    {
        return packet(2, {reinterpret_cast<const uint8_t *>(message.data()), message.size()});
    }
    // A long message as the app sends it, compressed (here as stored DEFLATE blocks, which any inflater reads).
    Frame sayCompressed(const std::string &message) const
    {
        std::vector<uint8_t> payload{uint8_t(message.size() >> 8), uint8_t(message.size())};
        for (size_t at = 0; at < message.size();) {
            const size_t n = std::min<size_t>(message.size() - at, 1000);
            const bool last = at + n == message.size();
            payload.insert(payload.end(), {uint8_t(last), uint8_t(n), uint8_t(n >> 8), uint8_t(~n), uint8_t(~n >> 8)});
            payload.insert(payload.end(), message.begin() + at, message.begin() + at + n);
            at += n;
        }
        Frame unsignedFrame, canonical, frame;
        stamp = std::max(stamp + 1, nowUtc());
        CHECK(encode(2, id, nullptr, stamp, 7, {payload.data(), payload.size()}, {}, unsignedFrame) == Result::Ok);
        unsignedFrame.data[11] |= 4;
        CHECK(signingBytes(unsignedFrame.bytes(), canonical) == Result::Ok);
        std::array<uint8_t, 64> signature;
        Ed25519::sign(signature.data(), signingKey.data(), signingPublic.data(), canonical.data.data(), canonical.size);
        CHECK(encode(2, id, nullptr, stamp, 7, {payload.data(), payload.size()}, {signature.data(), signature.size()}, frame) ==
              Result::Ok);
        frame.data[11] |= 4;
        return frame;
    }
    // One write per frame, split at the link MTU as the app does for a stream.
    void write(const Frame &frame, uint16_t link = NO_LINK) const
    {
        auto &c = server.service.characteristic;
        ble_gap_conn_desc desc{link == NO_LINK ? handle : link};
        c.value.assign(reinterpret_cast<const char *>(frame.data.data()), frame.size);
        c.callbacks->onWrite(&c, &desc);
    }
    void connect() const
    {
        auto &c = server.service.characteristic;
        ble_gap_conn_desc desc{handle};
        bleConnect(handle);
        c.callbacks->onSubscribe(&c, &desc, 1);
    }
};

// Frames the bridge notified to one phone, reassembled the way the app's stream assembler does.
static std::vector<std::vector<uint8_t>> received(uint16_t handle)
{
    std::vector<uint8_t> stream;
    for (const auto &part : fakeSent)
        if (part.first == handle)
            stream.insert(stream.end(), part.second.begin(), part.second.end());
    std::vector<std::vector<uint8_t>> frames;
    size_t offset = 0, length = 0;
    while (offset < stream.size()) {
        CHECK(wireFrameLength({stream.data() + offset, stream.size() - offset}, length) == Result::Ok);
        CHECK(length <= stream.size() - offset);
        frames.emplace_back(stream.begin() + offset, stream.begin() + offset + length);
        offset += length;
    }
    return frames;
}
static std::vector<std::string> chat(uint16_t handle, const PeerId *from = nullptr)
{
    std::vector<std::string> messages;
    for (const auto &frame : received(handle)) {
        Packet packet;
        CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok);
        if (packet.type == 2 && (!from || packet.sender == *from))
            messages.push_back(text(packet.payload));
    }
    return messages;
}
static std::vector<std::string> radio()
{
    std::vector<std::string> messages;
    for (const auto &packet : fakeRadio)
        if (packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP)
            messages.push_back(text({packet.decoded.payload.bytes, packet.decoded.payload.size}));
    return messages;
}
static std::string app(size_t index)
{
    const auto &packet = fakeApp.at(index);
    return text({packet.decoded.payload.bytes, packet.decoded.payload.size});
}
// A notification from the phone's own characteristic, as the stack reports it to the bridge.
static void notifiedBy(uint16_t link, uint16_t attribute, const Frame &frame)
{
    os_mbuf buffer{{frame.data.begin(), frame.data.begin() + frame.size}};
    ble_gap_event event{};
    event.type = BLE_GAP_EVENT_NOTIFY_RX;
    event.notify_rx.om = &buffer;
    event.notify_rx.attr_handle = attribute;
    event.notify_rx.conn_handle = link;
    CHECK(fakeListener != nullptr);
    fakeListener->fn(&event, fakeListener->arg);
}
// Frames the bridge wrote to the phone's own characteristic, one whole frame per write.
static std::vector<Packet> written(uint16_t link, uint8_t type)
{
    std::vector<Packet> packets;
    for (const auto &write : fakeRemoteWrites) {
        Packet packet;
        if (write.first != link * 1000 + 42)
            continue;
        CHECK(decode({write.second.data(), write.second.size()}, packet) == Result::Ok);
        if (packet.type == type)
            packets.push_back(packet);
    }
    return packets;
}
static void fromLoRa(NodeNum from, const std::string &message, uint8_t channel = 1, float snr = 0, int32_t rssi = 0)
{
    static uint32_t id = 5000;
    meshtastic_MeshPacket packet;
    packet.from = from;
    packet.rx_snr = snr;
    packet.rx_rssi = rssi;
    packet.id = ++id;
    packet.channel = channel;
    packet.decoded.payload.size = uint16_t(message.size());
    memcpy(packet.decoded.payload.bytes, message.data(), message.size());
    bridge->handleReceived(packet);
}

static std::string token(const char *name)
{
    for (const auto &c : TOKENS)
        if (std::string(c.name) == name)
            return c.text;
    CHECK(false);
    return {};
}
// Payment datagrams the bridge sent on the air.
static std::vector<meshtastic_MeshPacket> payPackets()
{
    std::vector<meshtastic_MeshPacket> out;
    for (const auto &p : fakeRadio)
        if (p.decoded.portnum == meshtastic_PortNum_PRIVATE_APP && payDatagram({p.decoded.payload.bytes, p.decoded.payload.size}))
            out.push_back(p);
    return out;
}
static void payFrom(NodeNum from, const PayDatagram &d, NodeNum to = NODENUM_BROADCAST, bool pki = false, uint8_t channel = 1)
{
    static uint32_t id = 90000;
    meshtastic_MeshPacket packet;
    packet.from = from;
    packet.to = to;
    packet.id = ++id;
    packet.channel = channel;
    packet.pki_encrypted = pki;
    packet.decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    packet.decoded.payload.size = uint16_t(d.size);
    memcpy(packet.decoded.payload.bytes, d.data.data(), d.size);
    bridge->handleReceived(packet);
}

// Cashu over LoRa: the payer's frames go whole to the other bridges, and come out whole there.
static void cashuTunnel(const Phone &ana, const Phone &bia, const PeerId &bridgeId)
{
    const auto lastSaid = [&]() {
        const auto lines = chat(bia.handle, &bridgeId);
        return lines.empty() ? std::string() : lines.back();
    };
    const std::string locked = token("B p2pk"), open = token("B plain");
    fakeRadio.clear();
    fakeSent.clear();
    run(30000);
    const Frame announce = ana.announce();
    ana.write(announce);
    run(200);
    const Frame payment = ana.sayCompressed(locked);
    ana.write(payment);
    run(30000);
    CHECK(radio().empty());
    CHECK(lastSaid().rfind("[bridge] Levando 10 sat de ana pela LoRa (", 0) == 0);
    auto parts = payPackets();
    CHECK(parts.size() == payParts(4 + announce.size + payment.size) && parts.size() > 1);
    PayReceiver far;
    PayHeard heard;
    unsigned complete = 0;
    for (const auto &p : parts) {
        CHECK(p.to == NODENUM_BROADCAST && p.channel == 1 && p.decoded.payload.size <= PAY_DATAGRAM);
        if (far.accept({p.decoded.payload.bytes, p.decoded.payload.size}, 0, heard) && heard.complete) {
            ++complete;
            CHECK(heard.announce.size == announce.size && !memcmp(heard.announce.data, announce.data.data(), announce.size));
            CHECK(heard.message.size == payment.size && !memcmp(heard.message.data, payment.data.data(), payment.size));
        }
    }
    CHECK(complete == 1);
    // A broadcast answer proves nothing: told as unchecked, after the wait for a better one.
    PayDatagram ack;
    uint8_t key[8]{};
    write32(key, 0x0000beef);
    makePayAck(heard.id, PayOutcome::Delivered, 2, key, nullptr, ack);
    payFrom(0x0000beef, ack);
    run(5000);
    CHECK(lastSaid().rfind("[bridge] Levando", 0) == 0);
    // A direct message Meshtastic decrypted with the sender's key names that node at once.
    strcpy(fakeNodeDB.nodes[0x00005ead].user.long_name, "Casa do Pai");
    write32(key, 0x00005ead);
    makePayAck(heard.id, PayOutcome::Delivered, 2, key, nullptr, ack);
    payFrom(0x00005ead, ack, fakeNodeDB.number, true);
    run(300);
    CHECK(lastSaid() == "[bridge] ✓ 10 sat entregue à ponte Casa do Pai (2 cel.). Confirme com quem recebe.");

    // A token anyone may spend stays here: the channel's key is public.
    fakeRadio.clear();
    run(4000);
    ana.write(ana.announce());
    run(200);
    ana.write(ana.sayCompressed(open));
    run(5000);
    CHECK(payPackets().empty() && radio().empty());
    CHECK(lastSaid() == "[bridge] Token sem trava P2PK: num canal aberto outros poderiam gastá-lo. Não foi pela LoRa.");

    // Only an unchecked answer: told so after the wait.
    fakeRadio.clear();
    run(30000);
    ana.write(ana.announce());
    run(200);
    ana.write(ana.sayCompressed(token("B usd")));
    run(30000);
    parts = payPackets();
    CHECK(!parts.empty());
    write32(key, 0x0000beef);
    makePayAck(read32(parts[0].decoded.payload.bytes + 3), PayOutcome::Delivered, 1, key, nullptr, ack);
    payFrom(0x0000beef, ack);
    run(16000);
    CHECK(lastSaid() == "[bridge] ✓? 150 usd entregue, diz a ponte !beef (não conferida). Confirme com quem recebe.");

    // The other end: a payer this radio never met, heard only through LoRa from node 0x4444.
    Phone rui("rui", 9);
    fakeSent.clear();
    fakeRadio.clear();
    fakeNodeDB.nodes[0x4444].user.public_key.size = 32;
    const Frame ruiAnnounce = rui.announce(), ruiPayment = rui.sayCompressed(locked);
    PaySender sender;
    CHECK(sender.start(0x55667788, ruiAnnounce.bytes(), ruiPayment.bytes(), 0));
    std::vector<PayDatagram> ruiParts;
    PayDatagram d;
    while (sender.next(d))
        ruiParts.push_back(d);
    for (size_t i = 1; i < ruiParts.size(); ++i)
        payFrom(0x4444, ruiParts[i]);
    run(1000);
    for (const auto &frame : received(bia.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok && p.sender != rui.id);
    }
    // Quiet for a while, the bridge asks for what it lacks, on the channel.
    run(PAY_QUIET_MS + 3000);
    parts = payPackets();
    CHECK(parts.size() == 1);
    PayHeard want;
    CHECK(readPayAnswer({parts[0].decoded.payload.bytes, parts[0].decoded.payload.size}, want) && want.kind == PayKind::Want &&
          want.missing == 1 && parts[0].to == NODENUM_BROADCAST);
    fakeRadio.clear();
    payFrom(0x4444, ruiParts[0]);
    run(3000);
    std::vector<Packet> shown;
    for (const auto &frame : received(bia.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        if (p.sender == rui.id)
            shown.push_back(p);
    }
    CHECK(shown.size() == 2 && shown[0].type == 1 && shown[1].type == 2 && shown[1].compressed() && shown[1].ttl == 6);
    CHECK(lastSaid() == "[bridge] 10 sat de rui chegaram pela LoRa. Toque no token para resgatar.");
    // The answer goes to the payer's bridge as a direct message, which Meshtastic encrypts with PKI.
    parts = payPackets();
    CHECK(parts.size() == 1 && parts[0].to == 0x4444);
    PayHeard told;
    CHECK(readPayAnswer({parts[0].decoded.payload.bytes, parts[0].decoded.payload.size}, told) && told.kind == PayKind::Ack &&
          told.id == 0x55667788 && told.outcome == PayOutcome::Delivered && told.node == fakeNodeDB.number);
    // Without the payer's key, a broadcast.
    Phone eva("eva", 10);
    const Frame evaAnnounce = eva.announce(), evaPayment = eva.sayCompressed(locked);
    PaySender other;
    CHECK(other.start(0x99, evaAnnounce.bytes(), evaPayment.bytes(), 0));
    fakeRadio.clear();
    while (other.next(d))
        payFrom(0x7777, d);
    run(3000);
    parts = payPackets();
    CHECK(parts.size() == 1 && parts[0].to == NODENUM_BROADCAST);
    // A token piece without its name, from LoRa text, does not reach Bitchat.
    fakeSent.clear();
    fromLoRa(0x1234, open.substr(40, 120));
    fromLoRa(0x1234, "pague com " + open.substr(0, 100));
    run(1000);
    CHECK(chat(bia.handle, &bridgeId).empty());
    fakeRadio.clear();
    fakeSent.clear();
}

int main()
{
    fakeFree = 200 * 1024;
    fakeLargest = 64 * 1024;
    fakeNow = 1000;
    auto &primary = channels.items[0];
    for (unsigned i = 0; i < channels.getNumChannels(); ++i)
        channels.items[i].index = i;
    primary.role = meshtastic_Channel_Role_PRIMARY;
    strcpy(primary.settings.name, "Private");
    primary.settings.psk.size = 32;
    strcpy(fakeNodeDB.nodes[0x1234].user.long_name, "R\xc3\xa1"
                                                    "dio Sul");

    bleAttach(&server);
    bridge = new Bridge;
    run(1000);
    // First boot: local relay plus the dedicated public channel, with no menu step.
    CHECK(bridge->enabled() && bridge->textChannel() == 1 && !bridge->prefixOnly());
    CHECK(!strcmp(channels.items[1].settings.name, PUBLIC_CHANNEL_NAME));
    // The wait is a countdown, not a fixed label.
    const std::string waiting = bridge->status();
    CHECK(waiting.rfind("Starting LoRa: ", 0) == 0);
    run(5000);
    CHECK(std::string(bridge->status()).rfind("Starting LoRa: ", 0) == 0 && std::string(bridge->status()) != waiting);
    run(31000);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa" && !bridge->hint()[0]);
    // The scan response names the radio in the 11 characters left beside the Meshtastic service.
    CHECK(bleAdvertise() && fakeScanName == "Mesh_1a2b");

    Phone ana("ana", 1, 11), bia("bia", 2, 12), caio("caio", 3, 13);
    for (const auto *phone : {&ana, &bia, &caio})
        phone->connect();
    run(100);
    // A subscription alone says nothing about the phone: the bridge stays quiet until the phone's writes
    // arrive or its own Bitchat service is found. These phones write, as an app whose writes work does.
    for (const auto *phone : {&ana, &bia, &caio})
        CHECK(received(phone->handle).empty());
    for (const auto *phone : {&ana, &bia, &caio})
        phone->write(phone->announce());
    run(400);
    // Each phone then learns the bridge identity, besides the other phones relayed to it.
    PeerId bridgeId{};
    Announcement bridgeIdentity;
    for (const auto *phone : {&ana, &bia, &caio}) {
        size_t own = 0;
        for (const auto &frame : received(phone->handle)) {
            Packet packet;
            Announcement identity;
            CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok && packet.type == 1);
            CHECK(decodeAnnouncement(packet.payload, identity) == Result::Ok);
            if (!identity.gateway)
                continue;
            // One notification at the 185-byte MTU common on iPhones, and a payload the app never compresses.
            CHECK(frame.size() <= 182 && packet.payload.size < 100);
            bridgeId = packet.sender;
            bridgeIdentity = identity;
            ++own;
        }
        CHECK(own >= 1);
    }

    // A plain message, no prefix: relayed to the other phones and sent once to LoRa under the nickname.
    fakeSent.clear();
    const Frame hello = ana.say("oi pessoal");
    ana.write(hello);
    run(200);
    CHECK(chat(bia.handle) == std::vector<std::string>{"oi pessoal"} && chat(caio.handle) == chat(bia.handle));
    CHECK(chat(ana.handle).empty());
    // The other phones relay what they heard back to the bridge: copies cost nothing and count as nothing.
    const uint32_t lostBefore = bridge->drops();
    Frame copy = hello;
    --copy.data[2];
    bia.write(copy, bia.handle);
    caio.write(copy, caio.handle);
    run(2000);
    CHECK(radio() == std::vector<std::string>{"ana: oi pessoal"});
    CHECK(fakeRadio[0].channel == 1 && fakeRadio[0].priority == meshtastic_MeshPacket_Priority_DEFAULT);
    // Like text typed in the Meshtastic app: the radio repeats it when nobody is heard relaying.
    CHECK(fakeRadio[0].to == NODENUM_BROADCAST && fakeRadio[0].want_ack);
    CHECK(chat(bia.handle).size() == 1 && bridge->drops() == lostBefore);

    char details[128];
    bridge->diagnostics(details, sizeof(details));
    CHECK(std::string(details).find(" app\nrx 6 ok 4 tx ") != std::string::npos);
    CHECK(std::string(details).find("old 0 sig 0 fmt 0\nver 0 pol 0 full 0") != std::string::npos);

    // A conversation: quick replies from everyone, the old prefix still accepted and hidden.
    fakeRadio.clear();
    bia.write(bia.say("tudo bem?"));
    caio.write(caio.say("!lora cheguei"));
    ana.write(ana.say("vamos"));
    ana.write(ana.say("agora"));
    ana.write(ana.say("j\xc3\xa1"));
    run(12000);
    // Phones take turns, so the order between people may differ; each person's own order is kept.
    auto talk = radio();
    CHECK(talk.size() == 5 && std::count(talk.begin(), talk.end(), "bia: tudo bem?") == 1);
    CHECK(std::count(talk.begin(), talk.end(), "caio: cheguei") == 1);
    talk.erase(std::remove_if(talk.begin(), talk.end(), [](const std::string &m) { return m.rfind("ana: ", 0) != 0; }),
               talk.end());
    CHECK(talk == (std::vector<std::string>{"ana: vamos", "ana: agora", "ana: j\xc3\xa1"}));
    CHECK(bridge->drops() == lostBefore);

    // Long text is split, never truncated; text for the tunnel or already imported is never exported.
    fakeRadio.clear();
    std::string longText;
    while (longText.size() < 420)
        longText += "mensagem longa ";
    bia.write(bia.say(longText));
    bia.write(bia.say("!bctun teste"));
    bia.write(bia.say("[MT x] eco"));
    run(12000);
    CHECK(radio().size() == 3 && radio()[0].rfind("bia (1/3): mensagem", 0) == 0 && radio()[2].rfind("bia (3/3): ", 0) == 0);
    std::string joined;
    for (const auto &part : radio())
        joined += part.substr(11);
    CHECK(joined == longText);

    // Optional mode for a busy mesh: only text typed with the prefix leaves by LoRa.
    fakeRadio.clear();
    CHECK(bridge->setPrefixOnly(true));
    caio.write(caio.say("fica aqui"));
    caio.write(caio.say("!lora vai"));
    run(4000);
    CHECK(radio() == std::vector<std::string>{"caio: vai"});
    CHECK(bridge->setPrefixOnly(false));

    // LoRa to Bitchat: a burst from the Meshtastic side reaches every phone, signed by the bridge.
    fakeSent.clear();
    for (int i = 1; i <= 5; ++i) {
        fromLoRa(0x1234, "resposta " + std::to_string(i));
        run(300);
    }
    fromLoRa(0x9999, "sem nome");
    fromLoRa(0x1234, "outro canal", 2);
    run(500);
    for (const auto *phone : {&ana, &bia, &caio}) {
        const auto messages = chat(phone->handle, &bridgeId);
        CHECK(messages.size() == 6 &&
              messages[0] == "[MT R\xc3\xa1"
                             "dio Sul] resposta 1" &&
              messages[4].back() == '5');
        CHECK(messages[5] == "[MT !00009999] sem nome");
        for (const auto &frame : received(phone->handle)) {
            Packet packet;
            Frame canonical;
            CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok && packet.sender == bridgeId);
            CHECK(signingBytes({frame.data(), frame.size()}, canonical) == Result::Ok);
            CHECK(crypto.verify(canonical.bytes(), packet.signature, bridgeIdentity.signingKey.data()));
        }
    }
    CHECK(bridge->drops() == lostBefore);

    // A full-length Meshtastic message arrives as several short signed messages, each below the size at
    // which the app would compress before verifying, and nothing is lost.
    fakeSent.clear();
    std::string fullLength;
    while (fullLength.size() < 190)
        fullLength += "texto comprido ";
    fromLoRa(0x1234, fullLength);
    run(1500);
    std::string rebuilt;
    for (const auto &message : chat(ana.handle, &bridgeId)) {
        CHECK(message.size() < 100 && message.rfind("[MT R\xc3\xa1"
                                                    "dio Sul ",
                                                    0) == 0);
        rebuilt += message.substr(message.find("] ") + 2);
    }
    CHECK(rebuilt == fullLength && chat(caio.handle, &bridgeId).size() == chat(ana.handle, &bridgeId).size());
    for (const auto &frame : received(bia.handle)) {
        Packet packet;
        Frame canonical;
        CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok && packet.payload.size < 100);
        CHECK(signingBytes({frame.data(), frame.size()}, canonical) == Result::Ok);
        CHECK(crypto.verify(canonical.bytes(), packet.signature, bridgeIdentity.signingKey.data()));
    }
    CHECK(bridge->drops() == lostBefore);

    // More texts than the persistent journal holds: the oldest entry gives way, nothing is refused.
    fakeSent.clear();
    for (int i = 0; i < 70; ++i) {
        fromLoRa(0x1234, "n" + std::to_string(i));
        run(200);
    }
    CHECK(chat(ana.handle, &bridgeId).size() == 70 && bridge->drops() == lostBefore);

    // A distant person with the same nickname, heard through another phone: labels stay distinct.
    Phone twin("ana", 4);
    bia.write(twin.announce({}, 6), bia.handle);
    run(200);
    fakeRadio.clear();
    ana.write(ana.say("sou eu"));
    run(3000);
    char expected[32];
    snprintf(expected, sizeof(expected), "ana#%02x%02x: sou eu", ana.id[0], ana.id[1]);
    CHECK(radio() == std::vector<std::string>{expected});

    // Another bridge on the same channel. Its texts are never re-exported; the lower node number keeps
    // working and the higher one stands by, then resumes by itself once the other is gone.
    Digest binding, key;
    {
        SHA256 sha;
        static const char purpose[] = "meshtastic-bitchat/channel/v1";
        sha.update(purpose, sizeof(purpose));
        sha.update(PUBLIC_CHANNEL_NAME, strlen(PUBLIC_CHANNEL_NAME));
        sha.update(PUBLIC_CHANNEL_KEY.data(), 32);
        sha.finalize(binding.data(), binding.size());
        static const char info[] = "meshtastic-bitchat/text/v1";
        hkdf<SHA256>(key.data(), key.size(), PUBLIC_CHANNEL_KEY.data(), 32, binding.data(), 32, info, sizeof(info) - 1);
    }
    Phone other("LoRa bridge 2", 5);
    auto proof = [&](NodeNum node) {
        std::array<uint8_t, 2 + GATEWAY_PROOF> tlv{GATEWAY_TLV, GATEWAY_PROOF};
        makeGatewayProof(crypto, other.id, node, key.data(), tlv.data() + 2);
        return tlv;
    };
    fakeRadio.clear();
    auto higher = proof(fakeNodeDB.number + 1);
    caio.write(other.announce({higher.data(), higher.size()}, 6), caio.handle);
    run(200);
    caio.write(other.say("[MT x] importado"), caio.handle);
    caio.write(other.say("texto da outra ponte"), caio.handle);
    ana.write(ana.say("continua"));
    run(3000);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa");
    CHECK(radio().size() == 1 && radio()[0].find("continua") != std::string::npos);
    fakeRadio.clear();
    // What a MeshCore bridge brought to Bitchat crosses to this network once, under the name it had there.
    CHECK(bridge->linked());
    caio.write(other.say("[MC Maria] oi do MeshCore"), caio.handle);
    run(3000);
    CHECK(radio() == std::vector<std::string>{"MC Maria: oi do MeshCore"});
    fakeRadio.clear();
    // Text that already crossed, in either marking, stays where it is.
    caio.write(other.say("[MC MT Node Fixo] de volta"), caio.handle);
    caio.write(other.say("[MC Maria] MT Node Fixo: de volta"), caio.handle);
    caio.write(other.say("[MC ] sem nome"), caio.handle);
    caio.write(other.say("[MC sem fim"), caio.handle);
    run(3000);
    CHECK(radio().empty());
    CHECK(bridge->setLink(false));
    caio.write(other.say("[MC Maria] com a liga\xc3\xa7\xc3\xa3o desligada"), caio.handle);
    run(3000);
    CHECK(radio().empty() && bridge->setLink(true));
    // Heard on LoRa, crossed text reaches the phones under its marked name, whichever node relayed it.
    fakeSent.clear();
    fromLoRa(0x1234, "MC Maria: oi do MeshCore");
    run(300);
    CHECK(chat(bia.handle, &bridgeId) == std::vector<std::string>{"[MT MC Maria] oi do MeshCore"});
    fakeSent.clear();
    auto lower = proof(fakeNodeDB.number - 1);
    caio.write(other.announce({lower.data(), lower.size()}, 6), caio.handle);
    run(200);
    CHECK(std::string(bridge->status()) == "Standby: other bridge");
    ana.write(ana.say("em espera"));
    fromLoRa(0x1234, "em espera");
    fakeSent.clear();
    run(3000);
    CHECK(radio().empty() && chat(bia.handle, &bridgeId).empty());
    run(90000);
    CHECK(std::string(bridge->status()).rfind("Starting LoRa: ", 0) == 0);
    run(31000);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa");
    ana.write(ana.announce());
    run(200);
    ana.write(ana.say("voltou"));
    run(3000);
    CHECK(radio().size() == 1 && radio()[0].find("voltou") != std::string::npos);

    // A crowded place: the table is full of people who left minutes ago; newcomers still get in.
    std::vector<Phone> crowd;
    for (uint8_t i = 0; i < 30; ++i)
        crowd.emplace_back(("p" + std::to_string(i)).c_str(), uint8_t(40 + i));
    for (size_t i = 0; i < 24; ++i) {
        bia.write(crowd[i].announce({}, 6), bia.handle);
        run(100);
    }
    fakeRadio.clear();
    bia.write(crowd[25].announce({}, 6), bia.handle);
    run(100);
    bia.write(crowd[25].say("lotado"), bia.handle);
    run(3000);
    CHECK(radio().empty());
    run(PeerTable::STALE_MS);
    for (const auto *phone : {&ana, &bia, &caio})
        phone->write(phone->announce());
    bia.write(crowd[26].announce({}, 6), bia.handle);
    run(300);
    bia.write(crowd[26].say("cheguei depois"), bia.handle);
    ana.write(ana.say("ainda aqui"));
    run(5000);
    talk = radio();
    CHECK(talk.size() == 2 && std::count(talk.begin(), talk.end(), "p26: cheguei depois") == 1);

    // Airtime is bounded: a flood from one phone is cut by its own allowance, not by silencing the others.
    fakeRadio.clear();
    const uint32_t before = bridge->drops();
    for (int i = 0; i < 12; ++i) {
        caio.write(caio.say("flood " + std::to_string(i)));
        run(60);
    }
    bia.write(bia.say("posso falar"));
    run(20000);
    size_t floods = 0, others = 0;
    for (const auto &message : radio())
        (message.rfind("caio: flood", 0) == 0 ? floods : others)++;
    CHECK(floods == 6 && others == 1 && bridge->drops() == before + 6);

    // Self-test typed in the Meshtastic app attached to this radio (sender 0):
    // answered in that app only, in any letter case, and neither sent by LoRa nor
    // shown in Bitchat.
    run(20000);
    fakeRadio.clear();
    fakeSent.clear();
    fromLoRa(0, "!Bridge ");
    run(100);
    CHECK(fakeApp.size() == 3 && fakeRadio.empty() && fakeApp[0].channel == 1 && fakeApp[0].to == NODENUM_BROADCAST);
    CHECK(app(0).rfind("[bridge] Ready: Bitchat + LoRa\nBluetooth 3, Bitchat 3, people ", 0) == 0);
    CHECK(app(0).find("\nLoRa Bitchat out ") != std::string::npos && app(0).find("\nUTC ") != std::string::npos);
    CHECK(app(0).size() <= 200 && app(2).rfind("[bridge] Bitchat link OK", 0) == 0);
    // The second answer describes each Bluetooth link as the radio sees it.
    CHECK(app(1).rfind("[bridge] con ", 0) == 0 &&
          app(1).find("\nlink 11 mtu 185 itv 24 enc 0 bond 0 sub 1 wr 1") != std::string::npos);
    CHECK(app(1).size() <= 200);
    // The same word arriving from another radio is not answered and not imported.
    fromLoRa(0x1234, "!bridge");
    run(100);
    CHECK(fakeApp.size() == 3 && chat(ana.handle).empty());
    // Asked from Bitchat: the signed answer reaches the phones, proving both
    // directions; nothing goes to LoRa.
    ana.write(ana.say("!ponte"));
    run(3000);
    CHECK(radio().empty() && chat(ana.handle, &bridgeId).size() == 1 && chat(bia.handle, &bridgeId).size() == 1);
    CHECK(chat(ana.handle, &bridgeId)[0].rfind("[bridge] Ready: Bitchat + LoRa. Phones 3, people ", 0) == 0);
    CHECK(chat(ana.handle, &bridgeId)[0].size() < 100 && chat(bia.handle, &ana.id) == std::vector<std::string>{"!ponte"});
    // Several people asking at once get one answer; a reset is not accepted from
    // Bitchat.
    bia.write(bia.say("!bridge reset"));
    caio.write(caio.say("!bridge"));
    run(3000);
    CHECK(chat(ana.handle, &bridgeId).size() == 1 && fakeServiceChanges == 0 && radio().empty());
    // The guide is answered in Bitchat too, in the language of the word used, and not more than twice a minute.
    {
        const size_t before = chat(bia.handle, &bridgeId).size();
        ana.write(ana.say("!ponte guia"));
        run(3000);
        auto guide = chat(bia.handle, &bridgeId);
        CHECK(guide.size() == before + GUIDE_LINES && guide[before].rfind("[bridge] Guia 1/4:", 0) == 0 &&
              guide.back().rfind("[bridge] 4/4: aqui também: !ponte quem", 0) == 0 && radio().empty());
        for (const auto &line : guide)
            CHECK(line.size() <= BITCHAT_TEXT_LIMIT);
        bia.write(bia.say("!bridge guide"));
        run(3000);
        CHECK(chat(ana.handle, &bridgeId).size() == before + GUIDE_LINES);
        run(30000);
        bia.write(bia.say("!bridge guide "));
        run(3000);
        guide = chat(ana.handle, &bridgeId);
        CHECK(guide.size() == before + 2 * GUIDE_LINES && guide[before + GUIDE_LINES].rfind("[bridge] Guide 1/4:", 0) == 0 &&
              radio().empty());
    }
    // Payments, after the counts above: they bring people from afar.
    cashuTunnel(ana, bia, bridgeId);
    // The three-line self-test again, which the next check counts on.
    fakeApp.clear();
    fromLoRa(0, "!bridge");
    run(100);
    // Reset from the owner's app: phones are told to read the services again,
    // then every link is dropped.
    const size_t dropsBefore = server.disconnected.size();
    fromLoRa(0, "!ponte reset");
    run(100);
    CHECK(fakeApp.size() == 4 && app(3).find("restarts in 3 s") != std::string::npos);
    CHECK(fakeServiceChanges == 1 && server.disconnected.size() == dropsBefore);
    run(3100);
    CHECK(fakeServiceChanges == 1 && server.disconnected.size() == dropsBefore + 3);
    run(5000);
    CHECK(server.disconnected.size() == dropsBefore + 3);

    // No phone connected: nothing is sent or written to flash for nobody. The text is signed and kept in
    // memory, and the phone that arrives later asks for what it lacks, the way the app does.
    for (const auto *phone : {&ana, &bia, &caio})
        bleDisconnect(phone->handle);
    const unsigned writes = Preferences::writes;
    fakeSent.clear();
    for (const char *text : {"primeira", "segunda", "terceira", "quarta"}) {
        fromLoRa(0x1234, text);
        run(300);
    }
    run(25000);
    CHECK(fakeSent.empty() && Preferences::writes == writes);
    fakeApp.clear();
    fromLoRa(0, "!bridge", 1);
    run(1100);
    // The table was already full of earlier text: these four are its newest.
    CHECK(app(0).find("\nSaver off, kept " + std::to_string(BITCHAT_HISTORY_ENTRIES) + "\n") != std::string::npos);
    {
        Phone eva("eva", 9, 31);
        eva.connect();
        run(300);
        eva.write(eva.announce());
        run(600);
        // Nothing is pushed to a phone that did not ask.
        CHECK(blePhones() == 1 && chat(eva.handle, &bridgeId).empty());
        const auto held = [&]() {
            std::vector<SyncId> ids;
            for (const auto &frame : received(eva.handle)) {
                Packet packet;
                CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok);
                if (packet.type != 2 || !(packet.sender == bridgeId))
                    continue;
                // An answer to a request: for this phone only, and marked so the app accepts its age.
                CHECK(packet.ttl == 0 && packet.replay() && packet.signedPacket());
                SyncId id;
                syncId(crypto, packet, id);
                ids.push_back(id);
            }
            return ids;
        };
        const std::string from = "[MT R\xc3\xa1"
                                 "dio Sul] ";
        auto wire = syncRequest(crypto, {});
        eva.write(eva.packet(0x21, {wire.data(), wire.size()}, 0));
        run(600);
        CHECK((chat(eva.handle, &bridgeId) == std::vector<std::string>{from + "quarta", from + "terceira", from + "segunda"}));
        const auto first = held();
        CHECK(first.size() == 3);
        // Asked again at once, nothing more; five seconds later, holding those three, it gets the last.
        fakeSent.clear();
        wire = syncRequest(crypto, first);
        eva.write(eva.packet(0x21, {wire.data(), wire.size()}, 0));
        run(600);
        CHECK(chat(eva.handle, &bridgeId).empty());
        run(5000);
        eva.write(eva.packet(0x21, {wire.data(), wire.size()}, 0));
        run(600);
        {
            const auto more = chat(eva.handle, &bridgeId);
            // the rest of the three are older messages the phones wrote, kept under their own names
            CHECK(!more.empty() && more[0] == from + "primeira" && chat(eva.handle).size() == 3);
        }
        // Asking for another kind of packet gets nothing; live text arrives as before and is kept too.
        fakeSent.clear();
        run(5000);
        wire = syncRequest(crypto, {}, 7, 0, 1);
        eva.write(eva.packet(0x21, {wire.data(), wire.size()}, 0));
        run(600);
        CHECK(chat(eva.handle, &bridgeId).empty());
        fromLoRa(0x1234, "ao vivo");
        run(300);
        CHECK((chat(eva.handle, &bridgeId) == std::vector<std::string>{from + "ao vivo"}));
        fakeApp.clear();
        fromLoRa(0, "!bridge", 1);
        run(1100);
        CHECK(app(0).find("\nSaver off, kept " + std::to_string(BITCHAT_HISTORY_ENTRIES) + "\n") != std::string::npos);
        // Who spoke on LoRa, the latest first: asked from Bitchat or from the app, in either language.
        run(5000);
        fakeSent.clear();
        eva.write(eva.say("!ponte quem"));
        run(600);
        {
            const auto who = chat(eva.handle, &bridgeId);
            CHECK(who.size() == 1 && who[0].size() <= BITCHAT_TEXT_LIMIT &&
                  who[0].rfind("[bridge] No LoRa, \xc3\xbaltimas 6 h: R\xc3\xa1"
                               "dio Sul",
                               0) == 0);
        }
        fakeApp.clear();
        fromLoRa(0, "!bridge who", 1);
        run(1100);
        CHECK(fakeApp.size() == 1 && app(0).rfind("[bridge] On LoRa, last 6 h: R\xc3\xa1"
                                                  "dio Sul",
                                                  0) == 0);
        // What a phone wrote is kept too, under its own signature, for the phone that arrives after it left.
        run(5000);
        eva.write(eva.say("volto as 18h"));
        run(600);
        bleDisconnect(eva.handle);
        run(1000);
        {
            Phone fia("fia", 10, 32);
            fia.connect();
            run(300);
            fia.write(fia.announce());
            run(600);
            fakeSent.clear();
            const auto wire = syncRequest(crypto, {});
            fia.write(fia.packet(0x21, {wire.data(), wire.size()}, 0));
            run(600);
            bool found = false;
            for (const auto &frame : received(fia.handle)) {
                Packet packet;
                CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok);
                if (packet.type != 2 || !(packet.sender == eva.id))
                    continue;
                Frame canonical;
                CHECK(packet.ttl == 0 && packet.replay() && signingBytes({frame.data(), frame.size()}, canonical) == Result::Ok);
                CHECK(crypto.verify(canonical.bytes(), packet.signature, eva.signingPublic.data()));
                found = text(packet.payload) == "volto as 18h";
            }
            CHECK(found);
            bleDisconnect(fia.handle);
            run(1000);
        }
        fakeSent.clear();
    }
    // A bonded phone's old subscription returns by itself when the Meshtastic app
    // reconnects. That is not a Bitchat phone: it is not counted, nothing is
    // imported for it, and the self-test says so.
    // Every link is asked for a larger packet size the moment it connects, once.
    const size_t asked = fakeMtuRequests.size();
    Phone ghost("ghost", 20, 15);
    ghost.connect();
    run(4000);
    CHECK(fakeMtuRequests.size() == asked + 1 && fakeMtuRequests.back() == 15);
    CHECK(bleConnections() == 1 && bleSubscribers() == 1 && blePhones() == 0);
    fromLoRa(0x1234, "para ninguem");
    fakeApp.clear();
    fromLoRa(0, "!bridge");
    run(3000);
    CHECK(chat(ghost.handle).empty() && fakeApp.size() == 3);
    CHECK(app(0).find("\nBluetooth 1, Bitchat 0, people ") != std::string::npos);
    CHECK(app(2).find("No Bitchat app has talked to this radio") != std::string::npos && app(2).size() <= 200);
    CHECK(app(1).find("\nlink 15 mtu 185 itv 24 enc 0 bond 0 sub 1 wr 0") != std::string::npos);
    // Once the app writes its announcement, it counts. A current app announces neighbours, capabilities
    // and a bridge cell too, which no longer fits one write at a 185-byte MTU: it arrives as two fragments.
    const size_t peopleBefore = app(0).find("people ");
    std::vector<uint8_t> extra{4, 16};
    extra.insert(extra.end(), 16, 0x5a);
    extra.insert(extra.end(), {5, 2, 0x01, 0x03, 6, 5, '6', 'g', 'y', 'f', '4'});
    const Frame whole = ghost.announce({extra.data(), extra.size()});
    CHECK(whole.size > 182);
    for (size_t offset = 0, index = 0; offset < whole.size; offset += 140, ++index) {
        std::vector<uint8_t> part{9, 8, 7, 6, 5, 4, 3, 2, 0, uint8_t(index), 0, uint8_t((whole.size + 139) / 140), 1};
        part.insert(part.end(), whole.data.begin() + offset, whole.data.begin() + std::min<size_t>(offset + 140, whole.size));
        Frame fragment;
        CHECK(encode(0x20, ghost.id, nullptr, stamp, 7, {part.data(), part.size()}, {}, fragment) == Result::Ok);
        CHECK(fragment.size <= 182);
        ghost.write(fragment);
    }
    run(400);
    CHECK(blePhones() == 1);
    fakeApp.clear();
    fromLoRa(0, "!bridge");
    run(100);
    CHECK(app(0).find("Bitchat 1, people ") != std::string::npos && peopleBefore != std::string::npos);
    CHECK(app(2).rfind("[bridge] Bitchat link OK", 0) == 0);
    ghost.write(ghost.say("cheguei em partes"));
    run(3000);
    CHECK(radio().back() == "ghost: cheguei em partes");
    bleDisconnect(ghost.handle);
    CHECK(blePhones() == 0 && bleConnections() == 0);

    // The case reported from a real iPhone: the app subscribes and then never writes. A Bitchat phone
    // publishes the same service, so the bridge subscribes to it over the same link. From then on the
    // phone's packets arrive as its notifications and the bridge's leave as writes to it.
    fakeRemoteService = true;
    fakeSent.clear();
    fakeRadio.clear();
    Phone iris("iris", 21, 16);
    iris.connect();
    run(200);
    const std::vector<uint8_t> littleEndian{0x5c, 0x4b, 0x3a, 0x2c, 0x1d, 0x8e, 0x3f, 0x9b,
                                            0x5a, 0x4c, 0x9e, 0x4a, 0x2d, 0x5e, 0x7b, 0xf4};
    CHECK(fakeRemoteUuids == littleEndian);
    CHECK(fakeRemoteSubscriptions == (std::vector<std::pair<uint16_t, uint16_t>>{{16, 44}}));
    // The bridge introduces itself by one write of a whole frame, and sends nothing by notification:
    // an announcement notified first would tie the phone to the writes it is not making.
    CHECK(received(iris.handle).empty() && written(iris.handle, 1).size() == 1);
    CHECK(written(iris.handle, 1)[0].sender == bridgeId && fakeRemoteWrites[0].second.size() <= 182);
    CHECK(blePhones() == 0);
    notifiedBy(iris.handle, 42, iris.announce());
    run(200);
    CHECK(blePhones() == 1);
    notifiedBy(iris.handle, 42, iris.say("pelo caminho novo"));
    run(3000);
    CHECK(radio().back() == "iris: pelo caminho novo");
    fromLoRa(0x1234, "resposta");
    run(3000);
    CHECK(written(iris.handle, 2).size() == 1 && text(written(iris.handle, 2)[0].payload).find("resposta") != std::string::npos);
    CHECK(received(iris.handle).empty());
    // A notification from any other attribute of the phone is not Bitchat traffic.
    const uint32_t before16 = bridge->drops();
    notifiedBy(iris.handle, 43, iris.say("outro atributo"));
    notifiedBy(99, 42, iris.say("outra conexao"));
    run(3000);
    CHECK(radio().back() == "iris: pelo caminho novo" && bridge->drops() == before16);
    fakeApp.clear();
    fromLoRa(0, "!bridge");
    run(100);
    CHECK(app(1).find(" in 2 out ") != std::string::npos);
    CHECK(app(1).find("\nlink 16 mtu 185 itv 24 enc 0 bond 0 sub 1 wr 1 path 9") != std::string::npos);
    bleDisconnect(iris.handle);
    // Each step of the search can fail, on an older app or another kind of phone. The bridge then
    // works as before: once the phone's writes arrive, it announces by notification.
    for (int refusal = 1; refusal <= 3; ++refusal) {
        fakeRemoteRefusal = refusal;
        fakeSent.clear();
        fakeRemoteWrites.clear();
        Phone old("old", uint8_t(30 + refusal), 17);
        old.connect();
        run(200);
        CHECK(received(old.handle).empty() && fakeRemoteWrites.empty());
        old.write(old.announce());
        run(200);
        CHECK(blePhones() == 1 && received(old.handle).size() == 1 && fakeRemoteWrites.empty());
        bleDisconnect(old.handle);
    }
    fakeRemoteRefusal = 0;
    fakeRemoteService = false;
    CHECK(blePhones() == 0 && bleConnections() == 0);
    // The app can start long after the link exists, as when the Meshtastic app holds the connection and
    // an old subscription was restored. The silent link is searched again until the service shows up.
    fakeSent.clear();
    fakeRemoteWrites.clear();
    fakeRemoteSubscriptions.clear();
    Phone late("late", 40, 18);
    late.connect();
    run(5000);
    CHECK(received(late.handle).empty() && fakeRemoteWrites.empty() && fakeRemoteSubscriptions.empty());
    fakeRemoteService = true;
    run(11000);
    CHECK(fakeRemoteSubscriptions.size() == 1 && written(late.handle, 1).size() >= 1 && received(late.handle).empty());
    // Closing and opening the app on the same link starts the search again from nothing.
    {
        auto &c = server.service.characteristic;
        ble_gap_conn_desc desc{late.handle};
        c.callbacks->onSubscribe(&c, &desc, 0);
        run(200);
        fakeRemoteWrites.clear();
        fromLoRa(0x1234, "sem ninguem");
        run(3000);
        CHECK(fakeRemoteWrites.empty() && received(late.handle).empty());
        c.callbacks->onSubscribe(&c, &desc, 1);
        run(200);
        CHECK(fakeRemoteSubscriptions.size() == 2 && written(late.handle, 1).size() == 1);
    }
    fakeRemoteService = false;
    bleDisconnect(late.handle);

    // Settings survive a restart.
    CHECK(bridge->setPrefixOnly(true) && bridge->setEconomy(true));
    delete bridge;
    bridge = new Bridge;
    CHECK(bridge->prefixOnly() && bridge->economy() && bridge->enabled() && bridge->textChannel() == 1);
    CHECK(bridge->setPrefixOnly(false) && bridge->setEconomy(false));
    delete bridge;

    // Power was cut and the radio has no clock. No app is needed to recover: the first phone that
    // connects supplies the time, and both apps can find the radio while it waits.
    const uint64_t lastTrusted = nowUtc();
    lastSetFromPhoneNtpOrGps = false;
    fakeNow += 6 * 3600 * 1000;
    fakeRadio.clear();
    fakeSent.clear();
    bridge = new Bridge;
    const unsigned advertising = fakeBluetooth.refreshes;
    run(61000);
    CHECK(std::string(bridge->status()) == "Clock not set" && std::string(bridge->hint()) == "Open Bitchat nearby");
    CHECK(fakeBluetooth.refreshes >= advertising + 3);
    Phone dora("dora", 9, 14);
    dora.connect();
    run(200);
    CHECK(received(dora.handle).empty());
    // A recording from before the outage cannot set the clock back.
    forcedStamp = lastTrusted - 3600000;
    dora.write(dora.announce());
    forcedStamp = 0;
    run(200);
    CHECK(std::string(bridge->status()) == "Clock not set");
    dora.write(dora.announce());
    run(200);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa");
    CHECK(received(dora.handle).size() == 1);
    dora.write(dora.say("voltei sozinha"));
    fromLoRa(0x1234, "bem-vinda");
    run(3000);
    CHECK(radio() == std::vector<std::string>{"dora: voltei sozinha"});
    CHECK(chat(dora.handle).size() == 1 && chat(dora.handle)[0].find("bem-vinda") != std::string::npos);
    // Once set, the time only moves forward: an older announcement is ignored, a replayed message is stale.
    forcedStamp = nowUtc() - 600000;
    const Frame old = dora.say("mensagem antiga");
    dora.write(dora.announce());
    dora.write(old);
    forcedStamp = 0;
    run(3000);
    CHECK(radio().size() == 1 && std::string(bridge->status()) == "Ready: Bitchat + LoRa");
    // When the Meshtastic app sets the time again, that source takes over.
    lastSetFromPhoneNtpOrGps = true;
    dora.write(dora.say("hora confiavel"));
    run(3000);
    CHECK(radio().size() == 2);
    {
        // The newer commands: battery, radios heard, the house note, the prefix, help requests and range tests.
        const auto contains = [](const std::vector<std::string> &lines, const std::string &wanted) {
            return std::find(lines.begin(), lines.end(), wanted) != lines.end();
        };
        fakeRadio.clear();
        fakePower.battery = true;
        fakePower.millivolts = 3920;
        fakePower.percent = 72;
        fakeApp.clear();
        fromLoRa(0, "!ponte bateria", 1);
        run(1100);
        CHECK(fakeApp.size() == 1 && app(0) == "[bridge] Bateria: 3,92 V, cerca de 72%.");
        const uint32_t clock = getValidTime(RTCQualityDevice);
        auto &casa = fakeNodeDB.nodes[0x1111];
        strcpy(casa.user.long_name, "Casa Ana");
        casa.last_heard = clock - 300;
        casa.has_hops_away = true;
        casa.snr = 6.5f;
        auto &sul = fakeNodeDB.nodes[0x2222];
        strcpy(sul.user.long_name, "Sul");
        sul.last_heard = clock - 4000;
        sul.has_hops_away = true;
        sul.hops_away = 2;
        auto &velho = fakeNodeDB.nodes[0x3333];
        strcpy(velho.user.long_name, "Velho");
        velho.last_heard = clock - 90000;
        auto &internet = fakeNodeDB.nodes[0x4444];
        strcpy(internet.user.long_name, "Pela internet");
        internet.last_heard = clock - 10;
        internet.via_mqtt = true;
        fakeApp.clear();
        fromLoRa(0, "!bridge radios", 1);
        run(1100);
        CHECK(fakeApp.size() == 1 && app(0) == "[bridge] Radios heard: Casa Ana (direct, 5 min, 6.5 dB), Sul (2 hops, 1 h)");
        fakeSent.clear();
        dora.write(dora.say("!ponte radios"));
        run(1100);
        // Who the radio hears is not told to strangers in Bitchat; the Portuguese list stays with the app.
        CHECK((chat(dora.handle) == std::vector<std::string>{"[bridge] R\xc3\xa1"
                                                             "dios: s\xc3\xb3 no aplicativo do pr\xc3\xb3prio r\xc3\xa1"
                                                             "dio, por privacidade."}));
        fakeApp.clear();
        fromLoRa(0, "!ponte radios", 1);
        run(1100);
        CHECK(fakeApp.size() == 1 && app(0) == "[bridge] R\xc3\xa1"
                                               "dios ouvidos: Casa Ana (direto, 5 min, 6,5 dB), Sul (2 saltos, 1 h)");
        CHECK(radio().empty());

        // The house note, left in the app and given to a phone when it announces itself.
        fakeApp.clear();
        fromLoRa(0, "!ponte recado Jantar \xc3\xa0s 20h", 1);
        run(1100);
        CHECK(fakeApp.size() == 1 && app(0).rfind("[bridge] Recado guardado", 0) == 0);
        fakeApp.clear();
        fromLoRa(0, "!bridge note", 1);
        run(1100);
        CHECK(fakeApp.size() == 1 && app(0) == "[bridge] Note from Casa Sul: Jantar \xc3\xa0s 20h");
        fakeSent.clear();
        dora.write(dora.announce());
        run(1100);
        const std::string note = "[bridge] Recado de Casa Sul: Jantar \xc3\xa0s 20h";
        CHECK(contains(chat(dora.handle), note));
        for (const auto &frame : received(dora.handle)) {
            Packet packet;
            CHECK(decode({frame.data(), frame.size()}, packet) == Result::Ok);
            if (packet.type == 2 && text(packet.payload) == note)
                CHECK(packet.ttl == 1);
        }
        run(30000);
        fakeSent.clear();
        dora.write(dora.announce());
        run(1100);
        CHECK(!contains(chat(dora.handle), note));
        fakeSent.clear();
        dora.write(dora.say("!bridge note off"));
        run(1100);
        CHECK((chat(dora.handle) == std::vector<std::string>{"[bridge] Note cleared."}));
        CHECK(Preferences::data.count("note") && Preferences::data["note"][1] == 0);

        // Prefix: only text marked with !lora goes to LoRa.
        fakeApp.clear();
        fromLoRa(0, "!bridge prefix on", 1);
        run(1100);
        CHECK(bridge->prefixOnly() && fakeApp.size() == 1 && app(0).rfind("[bridge] Prefix on: only #mesh text", 0) == 0);
        dora.write(dora.say("fica em casa"));
        run(3000);
        CHECK(radio().empty());
        dora.write(dora.say("!lora vai longe"));
        run(3000);
        CHECK(radio() == std::vector<std::string>{"dora: vai longe"});
        fromLoRa(0, "!ponte prefix off", 1);
        run(1100);
        CHECK(!bridge->prefixOnly() && app(1).rfind("[bridge] Prefixo desligado", 0) == 0);
        fakeRadio.clear();

        // A help request: the position only where the channel shares positions.
        auto &self = fakeNodeDB.nodes[fakeNodeDB.number];
        strcpy(self.user.long_name, "Casa Sul");
        self.has_position = true;
        self.position.latitude_i = -235505200;
        self.position.longitude_i = -466333100;
        run(5000);
        fakeSent.clear();
        dora.write(dora.say("!ponte sos ca\xc3\xad na trilha"));
        run(3000);
        CHECK(radio() == std::vector<std::string>{"dora: SOS: ca\xc3\xad na trilha"});
        CHECK(chat(dora.handle).size() == 1 && chat(dora.handle)[0].rfind("[bridge] SOS enviado ao LoRa, sem posi", 0) == 0);
        fakeRadio.clear();
        run(6000);
        fakeSent.clear();
        dora.write(dora.say("!bridge sos"));
        run(3000);
        CHECK(radio().empty() &&
              chat(dora.handle) == std::vector<std::string>{"[bridge] SOS sent a moment ago; it can be repeated in 5 min."});
        run(300000);
        channels.items[1].settings.has_module_settings = true;
        channels.items[1].settings.module_settings.position_precision = 32;
        fakeApp.clear();
        fromLoRa(0, "!bridge sos", 1);
        run(3000);
        CHECK(radio() == std::vector<std::string>{"SOS: help needed (-23.55052, -46.63331)"});
        CHECK(fakeApp.size() == 1 && app(0) == "[bridge] SOS sent to LoRa, with the radio's position.");
        fakeRadio.clear();
        // with less precision the point moves to the middle of a larger square
        run(300000);
        channels.items[1].settings.module_settings.position_precision = 13;
        fromLoRa(0, "!bridge sos", 1);
        run(3000);
        CHECK(radio().size() == 1 && radio()[0] != "SOS: help needed (-23.55052, -46.63331)" &&
              radio()[0].rfind("SOS: help needed (-23.", 0) == 0);
        channels.items[1].settings.has_module_settings = false;
        fakeRadio.clear();

        // Range test: this radio's probe, and another bridge's answer with how it heard it.
        fakeApp.clear();
        fromLoRa(0, "!ponte teste", 1);
        run(3000);
        CHECK(radio() == std::vector<std::string>{"#ponte-teste 1007"});
        CHECK(fakeApp.size() == 1 && app(0).rfind("[bridge] Teste 1007 enviado ao LoRa", 0) == 0);
        fakeSent.clear();
        fromLoRa(0x1111, "#ponte-teste 1007 ouvido -4.5 dB", 1, -6.0f, -100);
        run(1100);
        CHECK(fakeApp.size() == 2 && app(1) == "[bridge] Teste 1007: Casa Ana ouviu com -4,5 dB; a volta chegou com -6,0 dB.");
        CHECK(chat(dora.handle).empty());
        run(121000);
        CHECK(fakeApp.size() == 2);
        fakeRadio.clear();
        fromLoRa(0x2222, "#ponte-teste 4321", 1, 5.5f, -80);
        run(3000);
        fromLoRa(0x2222, "#ponte-teste 4321", 1, 5.5f, -80);
        run(3000);
        CHECK(radio() == std::vector<std::string>{"#ponte-teste 4321 ouvido 5.5 dB"});
        CHECK(chat(dora.handle).empty());
        fakeRadio.clear();
        // A relayed probe: the signal is of the last hop, so it is not reported.
        run(21000);
        {
            meshtastic_MeshPacket relayed;
            relayed.from = 0x2222;
            relayed.id = 77001;
            relayed.channel = 1;
            relayed.rx_snr = 9.0f;
            relayed.rx_rssi = -70;
            relayed.hop_start = 3;
            relayed.hop_limit = 2;
            const std::string probe = "#ponte-teste 4322";
            relayed.decoded.payload.size = uint16_t(probe.size());
            memcpy(relayed.decoded.payload.bytes, probe.data(), probe.size());
            bridge->handleReceived(relayed);
        }
        run(3000);
        CHECK(radio() == std::vector<std::string>{"#ponte-teste 4322 ouvido ? dB"});
        fakeRadio.clear();
        // A help request typed on another radio is that person's text, and reaches Bitchat.
        fakeSent.clear();
        fromLoRa(0x2222, "!ponte sos preciso de ajuda", 1);
        run(1100);
        CHECK(contains(chat(dora.handle), "[MT Sul] !ponte sos preciso de ajuda") && radio().empty());
        fakeApp.clear();
        // Asked from Bitchat, with nobody answering.
        run(30000);
        fakeSent.clear();
        dora.write(dora.say("!bridge test"));
        run(3000);
        CHECK(radio() == std::vector<std::string>{"#ponte-teste 1007"});
        run(121000);
        {
            const auto report = chat(dora.handle);
            CHECK(report.size() == 2 && report[1] == "[bridge] Test 1007: no other bridge answered within 2 min.");
        }
        fakeRadio.clear();

        // Low battery is told once, and again only after a charge.
        fakeSent.clear();
        fakePower.millivolts = 3400;
        run(61000);
        const std::string low = "[bridge] Bateria baixa / Battery low: 3,40 V";
        CHECK(chat(dora.handle) == std::vector<std::string>{low});
        run(61000);
        CHECK(chat(dora.handle).size() == 1);
        fakePower.millivolts = 3800;
        run(61000);
        fakePower.millivolts = 3400;
        run(61000);
        CHECK((chat(dora.handle) == std::vector<std::string>{low, low}));
        fakePower.battery = false;
        fakeApp.clear();
        fromLoRa(0, "!bridge battery", 1);
        run(1100);
        CHECK(fakeApp.size() == 1 && app(0) == "[bridge] Battery: this radio does not report it.");
        for (NodeNum gone : {NodeNum(0x1111), NodeNum(0x2222), NodeNum(0x3333), NodeNum(0x4444), fakeNodeDB.number})
            fakeNodeDB.nodes.erase(gone);
        fakeRadio.clear();
        fakeApp.clear();
    }
    delete bridge;

    // The channel list was wiped, as after a configuration reset: the default channel returns by
    // itself. Choosing Bluetooth only afterwards is respected.
    auto wipe = [] {
        channels.items[1] = meshtastic_Channel{};
        channels.items[1].index = 1;
    };
    bridge = new Bridge;
    run(1000);
    wipe();
    run(2000);
    CHECK(std::string(bridge->status()) == "LoRa channel changed");
    run(12000);
    CHECK(!strcmp(channels.items[1].settings.name, PUBLIC_CHANNEL_NAME) && bridge->textChannel() == 1);
    CHECK(std::string(bridge->status()).rfind("Starting LoRa: ", 0) == 0);
    CHECK(bridge->startBasicMode());
    wipe();
    run(15000);
    CHECK(channels.items[1].role == meshtastic_Channel_Role_DISABLED);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat only");
    // The self-test answers even with the bridge turned off.
    CHECK(bridge->setEnabled(false));
    fakeApp.clear();
    fromLoRa(0, "!bridge", 0);
    run(1100);
    CHECK(fakeApp.size() == 3 && fakeApp[0].channel == 0 && app(0).rfind("[bridge] Bridge is off\n", 0) == 0);
    CHECK(app(2) == "[bridge] To turn it on, send: !bridge on");
    // Radios without a screen: the choices of the menu, typed in the app attached to the radio. The
    // same words coming from another radio change nothing and get no answer.
    fakeApp.clear();
    const size_t onAir = radio().size();
    fromLoRa(0x1234, "!bridge on", 0);
    run(1100);
    CHECK(fakeApp.empty() && !bridge->enabled());
    // Nor does a packet on the air that claims this radio's own node number.
    fromLoRa(fakeNodeDB.number, "!bridge on", 0);
    run(1100);
    CHECK(fakeApp.empty() && !bridge->enabled());
    fromLoRa(0, "!bridge on", 0);
    run(1100);
    CHECK(bridge->enabled() && fakeApp.size() == 1 && app(0) == "[bridge] Bridge turned on. Send !bridge to see its state.");
    CHECK(std::string(bridge->status()) == "Ready: Bitchat only");
    // LoRa is taken from the channel where the command is typed, when that channel may be used.
    fromLoRa(0, "!bridge lora", 0);
    run(1100);
    CHECK(bridge->textChannel() == 255 && fakeApp.size() == 2 &&
          app(1).rfind("[bridge] Not changed: send !bridge lora in", 0) == 0);
    channels.items[1].role = meshtastic_Channel_Role_SECONDARY;
    strcpy(channels.items[1].settings.name, "Grupo");
    channels.items[1].settings.psk.size = 32;
    memset(channels.items[1].settings.psk.bytes, 7, 32);
    fromLoRa(0, "!Bridge LoRa", 1);
    run(1100);
    CHECK(bridge->textChannel() == 1 && fakeApp.size() == 3 && app(2) == "[bridge] Bitchat + LoRa on this channel.");
    CHECK(fakeApp[2].channel == 1 && bridge->enabled());
    fromLoRa(0, "!ponte local", 1);
    run(1100);
    CHECK(bridge->textChannel() == 255 && bridge->enabled() && app(3).rfind("[bridge] Bitchat only:", 0) == 0);
    fromLoRa(0, "!ponte off", 1);
    run(1100);
    CHECK(!bridge->enabled() && app(4).rfind("[bridge] Bridge turned off.", 0) == 0);
    fromLoRa(0, "!bridge help", 1);
    run(1100);
    CHECK(fakeApp.size() == 7 && app(5).rfind("[bridge] !bridge shows the state.", 0) == 0 && app(5).size() <= 200 &&
          app(5).find("!ponte quem, guia") != std::string::npos && app(5).back() == '.');
    // The newer commands follow in a second message.
    CHECK(app(6).rfind("[bridge] Also: prefix on", 0) == 0 && app(6).size() <= 200 &&
          app(6).find("sos <text>") != std::string::npos && app(6).find("!ponte recado") != std::string::npos &&
          app(6).back() == '.');
    fakeApp.pop_back();
    fromLoRa(0, "!bridge link off", 1);
    run(1100);
    CHECK(!bridge->linked() && app(6).rfind("[bridge] Link off", 0) == 0);
    fromLoRa(0, "!Ponte Link On", 1);
    run(1100);
    CHECK(bridge->linked() && app(7).rfind("[bridge] Link on", 0) == 0);
    // The screen menu's battery saver can be chosen by text, for radios without a screen. It shows in the
    // state and is kept.
    CHECK(!bridge->economy());
    fromLoRa(0, "!bridge economy on", 1);
    run(1100);
    CHECK(bridge->economy() && app(8).rfind("[bridge] Battery saver on", 0) == 0 && app(8).size() <= 200);
    fromLoRa(0, "!bridge", 1);
    run(1100);
    CHECK(fakeApp.size() == 12 && app(9).find("\nSaver on, kept ") != std::string::npos);
    fromLoRa(0, "!Ponte Economy Off", 1);
    run(1100);
    CHECK(!bridge->economy() && app(12) == "[bridge] Battery saver off.");
    fromLoRa(0, "!bridge", 1);
    run(1100);
    CHECK(app(13).find("\nSaver off, kept ") != std::string::npos);
    // The guide in the app attached to the radio, in either language.
    {
        const size_t before = fakeApp.size();
        fromLoRa(0, "!bridge guide", 1);
        run(1100);
        CHECK(fakeApp.size() == before + GUIDE_LINES && app(before).rfind("[bridge] Guide 1/4:", 0) == 0 &&
              app(before + 3).find("!bridge help the rest") != std::string::npos);
        fromLoRa(0, "!Ponte Guia", 1);
        run(1100);
        CHECK(fakeApp.size() == before + 2 * GUIDE_LINES && app(before + GUIDE_LINES).rfind("[bridge] Guia 1/4:", 0) == 0 &&
              app(before + 2 * GUIDE_LINES - 1).find("!bridge help o resto") != std::string::npos);
    }
    CHECK(radio().size() == onAir);
    delete bridge;
    bleDetach();
    CHECK(liveBytes == 0);
    std::cout << "PASS: " << checks
              << " end-to-end checks; group relay, duplicates, plain and prefixed export, splitting, LoRa import, "
                 "second bridge, crowding, airtime (host fakes, no hardware)\n";
}

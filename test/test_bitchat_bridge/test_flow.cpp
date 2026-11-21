// End-to-end host test: the production bridge module and BLE adapter, three simulated phones and a simulated radio.
#include "HeapModel.h"
#include "PowerStatus.h"
#include "bridge/BitchatBridgeModule.h"
#include "bridge/BridgeDefaults.h"
#include "bridge/BridgeNoise.h"
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

    // A refusal a known node sent directly is not undone by an unchecked "delivered" from anyone else.
    fakeRadio.clear();
    run(30000);
    ana.write(ana.announce());
    run(200);
    ana.write(ana.sayCompressed(locked));
    run(30000);
    parts = payPackets();
    CHECK(!parts.empty());
    {
        const uint32_t id = read32(parts[0].decoded.payload.bytes + 3);
        write32(key, 0x00005ead);
        makePayAck(id, PayOutcome::NoPhones, 0, key, nullptr, ack);
        payFrom(0x00005ead, ack, fakeNodeDB.number, true);
        write32(key, 0x0000beef);
        makePayAck(id, PayOutcome::Delivered, 3, key, nullptr, ack);
        payFrom(0x0000beef, ack);
        run(16000);
        CHECK(lastSaid() == "[bridge] ✗ 10 sat: ponte Casa do Pai sem celular perto. O token segue com você.");
    }

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
    // and also to everyone, in case the payer's bridge does not know this node's key yet.
    CHECK(parts.size() == 2 && parts[0].to == 0x4444 && parts[1].to == NODENUM_BROADCAST);
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

// A private chat as the apps hold one: Noise XX, the phone's side done with the same Noise code (checked against an
// independent implementation in test_noise.cpp), unsigned packets addressed to the other side.
struct PrivatePhone {
    const Phone &phone;
    PeerId bridge;
    NoiseHandshake handshake;
    NoiseSender send;
    NoiseReceiver receive;
    Frame directed(uint8_t type, Bytes payload, bool signedFrame = false) const
    {
        Frame unsignedFrame, canonical, frame;
        stamp = std::max(stamp + 1, nowUtc());
        CHECK(encode(type, phone.id, &bridge, stamp, 7, payload, {}, unsignedFrame, true) == Result::Ok);
        if (!signedFrame)
            return unsignedFrame;
        // As Android sends them: signed.
        CHECK(encode(type, phone.id, &bridge, stamp, 7, payload, {}, unsignedFrame) == Result::Ok);
        CHECK(signingBytes(unsignedFrame.bytes(), canonical) == Result::Ok);
        std::array<uint8_t, 64> signature;
        Ed25519::sign(signature.data(), phone.signingKey.data(), phone.signingPublic.data(), canonical.data.data(),
                      canonical.size);
        CHECK(encode(type, phone.id, &bridge, stamp, 7, payload, {signature.data(), signature.size()}, frame, true) ==
              Result::Ok);
        return frame;
    }
    // The packets the bridge addressed to this phone, of one type, since the last clear.
    std::vector<std::vector<uint8_t>> fromBridge(uint8_t type) const
    {
        std::vector<std::vector<uint8_t>> out;
        for (const auto &frame : received(phone.handle)) {
            Packet p;
            CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
            if (p.type == type && p.sender == bridge && p.directed() && p.recipient == phone.id) {
                CHECK(!p.signedPacket() && p.ttl == 7);
                out.emplace_back(p.payload.data, p.payload.data + p.payload.size);
            }
        }
        return out;
    }
    void open(uint8_t seed, bool signedFrames = false)
    {
        uint8_t e[32];
        for (int i = 0; i < 32; ++i)
            e[i] = uint8_t(seed + 5 * i);
        handshake.start(true, e);
        uint8_t m1[NOISE_MESSAGE1];
        CHECK(handshake.writeMessage1(m1));
        fakeSent.clear();
        phone.write(directed(0x10, {m1, sizeof(m1)}, signedFrames));
        run(100);
        const auto answers = fromBridge(0x10);
        CHECK(answers.size() == 1 && answers[0].size() == NOISE_MESSAGE2);
        CHECK(handshake.readMessage2({answers[0].data(), answers[0].size()}));
        PeerId proved;
        noisePeerId(handshake.remoteStatic(), proved);
        CHECK(proved == bridge);
        uint8_t m3[NOISE_MESSAGE3];
        CHECK(handshake.writeMessage3(phone.noiseKey.data(), phone.noisePublic.data(), m3));
        phone.write(directed(0x10, {m3, sizeof(m3)}, signedFrames));
        handshake.split(send, receive);
        run(100);
    }
    // The phone answers a handshake the bridge started.
    void answer(const std::vector<uint8_t> &m1, uint8_t seed)
    {
        uint8_t e[32];
        for (int i = 0; i < 32; ++i)
            e[i] = uint8_t(seed + i);
        handshake.start(false, e);
        CHECK(handshake.readMessage1({m1.data(), m1.size()}));
        uint8_t m2[NOISE_MESSAGE2];
        CHECK(handshake.writeMessage2(phone.noiseKey.data(), phone.noisePublic.data(), m2));
        fakeSent.clear();
        phone.write(directed(0x10, {m2, sizeof(m2)}));
        run(100);
        const auto third = fromBridge(0x10);
        CHECK(third.size() == 1 && third[0].size() == NOISE_MESSAGE3);
        CHECK(handshake.readMessage3({third[0].data(), third[0].size()}));
        handshake.split(send, receive);
    }
    void message(const std::string &id, const std::string &content)
    {
        std::vector<uint8_t> plain{1, 0, uint8_t(id.size())};
        plain.insert(plain.end(), id.begin(), id.end());
        plain.insert(plain.end(), {1, uint8_t(content.size())});
        plain.insert(plain.end(), content.begin(), content.end());
        std::vector<uint8_t> sealed(plain.size() + NOISE_TRANSPORT_OVERHEAD);
        CHECK(noiseEncrypt(send, {plain.data(), plain.size()}, sealed.data()));
        phone.write(directed(0x11, {sealed.data(), sealed.size()}));
    }
    // What the bridge said in this chat since the last clear: delivered IDs, and the texts it sent.
    void read(std::vector<std::string> &delivered, std::vector<std::string> &said)
    {
        for (const auto &wire : fromBridge(0x11)) {
            std::vector<uint8_t> plain(wire.size());
            CHECK(noiseDecrypt(receive, {wire.data(), wire.size()}, plain.data()));
            const size_t size = wire.size() - NOISE_TRANSPORT_OVERHEAD;
            if (plain[0] == 3) {
                delivered.emplace_back(plain.begin() + 1, plain.begin() + size);
                continue;
            }
            CHECK(plain[0] == 1 && size > 41 && plain[1] == 0 && plain[2] == 36 && plain[3 + 36] == 1);
            const size_t length = plain[3 + 37];
            CHECK(3 + 38 + length == size && length <= 255);
            said.emplace_back(plain.begin() + 41, plain.begin() + 41 + length);
            ids.emplace_back(plain.begin() + 3, plain.begin() + 3 + 36);
        }
    }
    // The IDs of the texts read, in order: the phone confirms them with "delivered".
    std::vector<std::string> ids;
    void delivered(const std::string &id)
    {
        std::vector<uint8_t> plain{3};
        plain.insert(plain.end(), id.begin(), id.end());
        std::vector<uint8_t> sealed(plain.size() + NOISE_TRANSPORT_OVERHEAD);
        CHECK(noiseEncrypt(send, {plain.data(), plain.size()}, sealed.data()));
        phone.write(directed(0x11, {sealed.data(), sealed.size()}));
    }
};

static void directFromLoRa(NodeNum from, const std::string &message, bool pki = true, uint32_t emoji = 0)
{
    static uint32_t id = 9000;
    meshtastic_MeshPacket packet;
    packet.from = from;
    packet.to = fakeNodeDB.number;
    packet.id = ++id;
    packet.channel = 0;
    packet.pki_encrypted = pki;
    packet.decoded.emoji = emoji;
    packet.decoded.payload.size = uint16_t(message.size());
    memcpy(packet.decoded.payload.bytes, message.data(), message.size());
    bridge->handleReceived(packet);
}

static void privateChat(const Phone &ana, const Phone &bia, const Phone &caio, const PeerId &bridgeId)
{
    fakePower.battery = true;
    fakePower.millivolts = 3920;
    fakePower.percent = 72;
    const auto publicBefore = chat(bia.handle, &bridgeId).size();
    const auto radioBefore = fakeRadio.size();
    PrivatePhone chatAna{ana, bridgeId};
    chatAna.open(40);
    std::vector<std::string> delivered, said;
    const auto clear = [&]() {
        delivered.clear();
        said.clear();
        fakeSent.clear();
    };
    // The command word alone is enough; the answer comes in the chat, delivered first.
    clear();
    chatAna.message("ID-1", "bateria");
    run(200);
    chatAna.read(delivered, said);
    CHECK(delivered == std::vector<std::string>{"ID-1"});
    CHECK(said == std::vector<std::string>{"Bateria: 3,92 V, cerca de 72%."});
    // Nothing of it in public, nothing on LoRa, and the private packets were not passed on to other phones.
    CHECK(chat(bia.handle, &bridgeId).size() == publicBefore && fakeRadio.size() == radioBefore);
    for (const auto &frame : received(bia.handle)) {
        Packet p;
        CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
        CHECK(p.sender != ana.id || (p.type != 0x10 && p.type != 0x11));
    }
    // The app sends a message again until it is told it was delivered: delivered again, answered once.
    clear();
    run(1100);
    chatAna.message("ID-1", "bateria");
    run(200);
    chatAna.read(delivered, said);
    CHECK(delivered == std::vector<std::string>{"ID-1"} && said.empty());
    // The written command, the status, the help for anything else, and the guide in four messages.
    clear();
    run(10000);
    chatAna.message("ID-2", "!bridge battery");
    run(1100);
    chatAna.message("ID-3", "ponte");
    run(1100);
    chatAna.message("ID-4", "oi, tudo bem?");
    run(5000);
    chatAna.message("ID-5", "guia");
    run(200);
    chatAna.read(delivered, said);
    CHECK((delivered == std::vector<std::string>{"ID-2", "ID-3", "ID-4", "ID-5"}));
    CHECK(said.size() == 3 + GUIDE_LINES);
    CHECK(said[0] == "Battery: 3.92 V, about 72%.");
    CHECK(said[1].rfind("Ready: Bitchat + LoRa. Phones ", 0) == 0);
    CHECK(said[2].rfind("Ponte LoRa. Escreva: ponte (estado), bateria", 0) == 0 && said[2].find("@contato") != std::string::npos);
    for (size_t i = 0; i < GUIDE_LINES; ++i)
        CHECK(said[3 + i] == guideLine(true, false, GuideMode::AllText, i));
    // The public chat's spacing is not touched: someone asking there right after is answered.
    clear();
    bia.write(bia.say("!ponte bateria"));
    run(200);
    CHECK(chat(bia.handle, &bridgeId).back() == "[bridge] Bateria: 3,92 V, cerca de 72%.");

    // Junk under Ana's ID while she is talking changes nothing.
    const auto junk = [&](int count) {
        for (int i = 0; i < count; ++i) {
            uint8_t bytes[40];
            for (int j = 0; j < 40; ++j)
                bytes[j] = uint8_t(i * 50 + j + count);
            ana.write(chatAna.directed(0x11, {bytes, sizeof(bytes)}));
            run(100);
        }
    };
    clear();
    run(5000);
    chatAna.message("ID-J", "bateria");
    run(200);
    junk(4);
    CHECK(chatAna.fromBridge(0x10).empty());
    chatAna.read(delivered, said);
    CHECK(delivered == std::vector<std::string>{"ID-J"} && said.size() == 1);
    // Three forged messages with nothing genuine for ten seconds: the bridge starts a new handshake, which the
    // phone answers; the new session replaces the old one.
    clear();
    run(11000);
    junk(3);
    {
        const auto restart = chatAna.fromBridge(0x10);
        CHECK(restart.size() == 1 && restart[0].size() == NOISE_MESSAGE1);
        PrivatePhone again{ana, bridgeId};
        again.answer(restart[0], 90);
        clear();
        run(1100);
        again.message("ID-8", "bateria");
        run(200);
        again.read(delivered, said);
        CHECK(delivered == std::vector<std::string>{"ID-8"} && said.size() == 1);
        chatAna.send = again.send;
        chatAna.receive = again.receive;
    }

    // A handshake whose static key is not the one the sender's ID stands for: no session; the bridge keeps waiting
    // for a genuine third message, then starts again.
    {
        PrivatePhone impostor{caio, bridgeId};
        uint8_t e[32];
        for (int i = 0; i < 32; ++i)
            e[i] = uint8_t(7 * i + 1);
        impostor.handshake.start(true, e);
        uint8_t m1[NOISE_MESSAGE1];
        CHECK(impostor.handshake.writeMessage1(m1));
        clear();
        caio.write(impostor.directed(0x10, {m1, sizeof(m1)}, true));
        run(100);
        const auto answers = impostor.fromBridge(0x10);
        CHECK(answers.size() == 1 && impostor.handshake.readMessage2({answers[0].data(), answers[0].size()}));
        uint8_t m3[NOISE_MESSAGE3];
        CHECK(impostor.handshake.writeMessage3(ana.noiseKey.data(), ana.noisePublic.data(), m3));
        caio.write(impostor.directed(0x10, {m3, sizeof(m3)}, true));
        impostor.handshake.split(impostor.send, impostor.receive);
        run(100);
        clear();
        impostor.message("ID-X", "bateria");
        run(200);
        CHECK(impostor.fromBridge(0x11).empty() && impostor.fromBridge(0x10).empty());
        run(21000);
        impostor.message("ID-X2", "bateria");
        run(200);
        CHECK(impostor.fromBridge(0x11).empty() && impostor.fromBridge(0x10).size() == 1);
    }

    // Signed packets, as Android sends them; bytes under Bia's ID between the bridge's second message and her
    // third do not end the handshake.
    PrivatePhone chatBia{bia, bridgeId};
    {
        run(11000);
        uint8_t e[32];
        for (int i = 0; i < 32; ++i)
            e[i] = uint8_t(77 + 5 * i);
        chatBia.handshake.start(true, e);
        uint8_t m1[NOISE_MESSAGE1];
        CHECK(chatBia.handshake.writeMessage1(m1));
        clear();
        bia.write(chatBia.directed(0x10, {m1, sizeof(m1)}, true));
        run(100);
        const auto answers = chatBia.fromBridge(0x10);
        CHECK(answers.size() == 1 && chatBia.handshake.readMessage2({answers[0].data(), answers[0].size()}));
        uint8_t m3[NOISE_MESSAGE3], forged[NOISE_MESSAGE3];
        CHECK(chatBia.handshake.writeMessage3(bia.noiseKey.data(), bia.noisePublic.data(), m3));
        for (size_t i = 0; i < sizeof(forged); ++i)
            forged[i] = uint8_t(i * 3);
        bia.write(chatBia.directed(0x10, {forged, sizeof(forged)}));
        run(100);
        bia.write(chatBia.directed(0x10, {m3, sizeof(m3)}, true));
        chatBia.handshake.split(chatBia.send, chatBia.receive);
        run(100);
        clear();
        chatBia.message("B-1", "teste");
        run(200);
        chatBia.read(delivered, said);
        CHECK(delivered == std::vector<std::string>{"B-1"} && said.size() == 1 && said[0].rfind("Teste ", 0) == 0);
        // The range test asked in private reports there, not in the public chat.
        clear();
        run(130000);
        chatBia.read(delivered, said);
        CHECK(said.size() == 1 && said[0].find("nenhuma outra ponte respondeu") != std::string::npos);
        for (const auto &line : chat(bia.handle, &bridgeId))
            CHECK(line.find("nenhuma outra ponte respondeu") == std::string::npos);
    }

    // Stage 2: "@name text" to a Meshtastic node the owner marked as favourite, and its answer back to the phone.
    auto &norte = fakeNodeDB.nodes[0x7777];
    strcpy(norte.user.long_name, "Casa Norte");
    strcpy(norte.user.short_name, "CN");
    norte.user.public_key.size = 32;
    norte.is_favorite = true;
    auto &semChave = fakeNodeDB.nodes[0x7778];
    strcpy(semChave.user.long_name, "Sem Chave");
    semChave.is_favorite = true;
    for (NodeNum twin : {0x7781u, 0x7782u}) {
        strcpy(fakeNodeDB.nodes[twin].user.long_name, "Gemeo");
        fakeNodeDB.nodes[twin].user.public_key.size = 32;
        fakeNodeDB.nodes[twin].is_favorite = true;
    }
    auto &estranho = fakeNodeDB.nodes[0x7783];
    strcpy(estranho.user.long_name, "Estranho");
    estranho.user.public_key.size = 32;
    const auto lastDirect = [&]() -> const meshtastic_MeshPacket * {
        for (auto it = fakeRadio.rbegin(); it != fakeRadio.rend(); ++it)
            if (it->to != NODENUM_BROADCAST)
                return &*it;
        return nullptr;
    };
    const auto sentText = [](const meshtastic_MeshPacket *p) {
        return p ? text({p->decoded.payload.bytes, p->decoded.payload.size}) : std::string();
    };
    const size_t radioNow = fakeRadio.size();
    clear();
    run(25000);
    chatAna.message("D-1", "@Casa Norte oi do bitchat");
    run(200);
    chatAna.read(delivered, said);
    CHECK(fakeRadio.size() == radioNow + 1 && lastDirect() && lastDirect()->to == 0x7777 && lastDirect()->channel == 0 &&
          lastDirect()->want_ack && lastDirect()->pki_encrypted && sentText(lastDirect()) == "[Bitchat ana] oi do bitchat");
    CHECK(said == std::vector<std::string>{"Enviado a Casa Norte pelo LoRa."});
    // The short name, with a colon; the second within 20 s is still allowed, the third is not.
    clear();
    chatAna.message("D-2", "@cn: tudo bem?");
    run(200);
    chatAna.message("D-3", "@Casa Norte de novo");
    run(200);
    chatAna.read(delivered, said);
    CHECK(sentText(lastDirect()) == "[Bitchat ana] tudo bem?" && fakeRadio.size() == radioNow + 2);
    CHECK((said ==
           std::vector<std::string>{"Enviado a Casa Norte pelo LoRa.",
                                    "Aguarde um pouco: limite de mensagens pelo LoRa. / Please wait: LoRa message limit."}));
    // Refusals, none of them on the air. A node that is not a favourite gets the same answer as an unknown name: the
    // chat does not tell which nodes the radio knows.
    clear();
    run(25000);
    for (const char *line : {"@Ninguem oi", "@Estranho oi", "@Gemeo oi", "@Sem Chave oi", "@Casa Norte"}) {
        chatAna.message(std::string("R-") + line, line);
        run(1000);
    }
    const std::string longText(190, 'x');
    chatAna.message("R-long", "@Casa Norte " + longText);
    run(200);
    chatAna.read(delivered, said);
    CHECK(fakeRadio.size() == radioNow + 2 && said.size() == 6);
    CHECK(said[0] == "N\xc3\xa3o \xc3\xa9 poss\xc3\xadvel mandar a \"Ninguem\": s\xc3\xb3 aos favoritos do r\xc3\xa1"
                     "dio. / Only the radio's favourites.");
    CHECK(said[1] == "N\xc3\xa3o \xc3\xa9 poss\xc3\xadvel mandar a \"Estranho\": s\xc3\xb3 aos favoritos do r\xc3\xa1"
                     "dio. / Only the radio's favourites.");
    CHECK(said[2].rfind("H\xc3\xa1 mais de um favorito \"Gemeo\"", 0) == 0);
    CHECK(said[3].rfind("Sem Chave ainda n\xc3\xa3o tem chave p\xc3\xba"
                        "blica",
                        0) == 0);
    CHECK(said[4].rfind("Escreva o texto depois do nome", 0) == 0);
    CHECK(said[5].rfind("Mensagem longa demais: at\xc3\xa9 ", 0) == 0);
    // Line breaks become spaces: the label stays in front.
    clear();
    run(25000);
    chatAna.message("D-4", "@Casa Norte linha um\nlinha dois");
    run(200);
    CHECK(sentText(lastDirect()) == "[Bitchat ana] linha um linha dois");
    // The node answers by direct message: it comes to Ana's chat, and stays kept until Ana says she has it.
    clear();
    run(15000);
    chatAna.message("P-1", "ponte");
    run(200);
    clear();
    directFromLoRa(0x7777, "tudo certo\npor aqui");
    run(200);
    chatAna.read(delivered, said);
    CHECK(said == std::vector<std::string>{"[MT Casa Norte] tudo certo por aqui"});
    const std::string kept = chatAna.ids.back();
    // Not confirmed: sent again, under the same ID, when the phone is next heard after half a minute.
    clear();
    run(31000);
    chatAna.message("P-2", "ponte");
    run(200);
    chatAna.ids.clear();
    chatAna.read(delivered, said);
    CHECK(said.size() == 2 && said[1] == "[MT Casa Norte] tudo certo por aqui" && chatAna.ids[1] == kept);
    // Confirmed: never again.
    chatAna.delivered(kept);
    run(200);
    clear();
    run(31000);
    chatAna.message("P-3", "ponte");
    run(200);
    chatAna.read(delivered, said);
    CHECK(said.size() == 1);
    // A retry of the same text is passed on once; a tapback, a message not encrypted to this node, and anything
    // from a node that is not a favourite are not passed on at all.
    clear();
    directFromLoRa(0x7777, "de novo?");
    directFromLoRa(0x7777, "de novo?");
    directFromLoRa(0x7777, "\xf0\x9f\x91\x8d", true, 1);
    directFromLoRa(0x7777, "sem PKI", false);
    directFromLoRa(0x7783, "@ana oi do estranho");
    run(200);
    chatAna.read(delivered, said);
    CHECK(said == std::vector<std::string>{"[MT Casa Norte] de novo?"});
    for (const auto &id : chatAna.ids)
        chatAna.delivered(id);
    run(200);

    // Ana walks away, her session still open on the bridge: the answer waits, and goes when she is back.
    clear();
    run(35000);
    directFromLoRa(0x7777, "voltou?");
    run(200);
    CHECK(chatAna.fromBridge(0x11).empty());
    ana.write(ana.announce());
    run(300);
    chatAna.ids.clear();
    chatAna.read(delivered, said);
    CHECK(said == std::vector<std::string>{"[MT Casa Norte] voltou?"});
    chatAna.delivered(chatAna.ids.back());
    run(200);
    // Half an hour after Ana last wrote to it, the node's messages are the owner's own again.
    clear();
    run(31 * 60 * 1000);
    ana.write(ana.announce());
    run(300);
    directFromLoRa(0x7777, "e agora?");
    run(200);
    chatAna.read(delivered, said);
    CHECK(said.empty());

    // "@bia" from the node: Bia has no chat with the bridge now, so it waits; when Bia announces herself, the bridge
    // opens the chat and delivers it.
    PrivatePhone mailBia{bia, bridgeId};
    {
        clear();
        directFromLoRa(0x7777, "@bia passa aqui depois");
        run(200);
        CHECK(mailBia.fromBridge(0x11).empty());
        clear();
        run(61000);
        bia.write(bia.announce());
        run(300);
        const auto opening = mailBia.fromBridge(0x10);
        CHECK(opening.size() == 1 && opening[0].size() == NOISE_MESSAGE1);
        mailBia.answer(opening[0], 120);
        run(200);
        mailBia.read(delivered, said);
        CHECK(said == std::vector<std::string>{"[MT Casa Norte] passa aqui depois"});
        mailBia.delivered(mailBia.ids.back());
        run(200);
    }
    // A frame under Bia's ID on another link does not move her answers there.
    clear();
    {
        uint8_t junk[40] = {9, 9, 9};
        caio.write(mailBia.directed(0x11, {junk, sizeof(junk)}));
        run(200);
        directFromLoRa(0x7777, "@bia ainda por a\xc3\xad?");
        run(200);
        size_t toCaio = 0;
        for (const auto &frame : received(caio.handle)) {
            Packet p;
            CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
            toCaio += p.type == 0x11 && p.recipient == bia.id;
        }
        CHECK(toCaio == 0);
        mailBia.read(delivered, said);
        CHECK(said == std::vector<std::string>{"[MT Casa Norte] ainda por a\xc3\xad?"});
        mailBia.delivered(mailBia.ids.back());
        run(200);
    }
    // A phone announcing itself with Bia's nickname: "@bia" names neither, "@bia#xxxx" the right one.
    {
        Phone twin("bia", 77);
        caio.write(twin.announce({}, 6), caio.handle);
        run(300);
        clear();
        directFromLoRa(0x7777, "@bia qual das duas?");
        run(200);
        mailBia.read(delivered, said);
        size_t toTwin = 0;
        for (const auto &frame : received(caio.handle)) {
            Packet p;
            CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
            toTwin += (p.type == 0x10 || p.type == 0x11) && p.recipient == twin.id;
        }
        CHECK(said.empty());
        CHECK(toTwin == 0);
        char label[16];
        snprintf(label, sizeof(label), "@bia#%02x%02x", bia.id[0], bia.id[1]);
        directFromLoRa(0x7777, std::string(label) + " esta mesmo");
        run(200);
        mailBia.read(delivered, said);
        CHECK(said == std::vector<std::string>{"[MT Casa Norte] esta mesmo"});
        mailBia.delivered(mailBia.ids.back());
        run(200);
    }
    // Phones heard through Bia's link (all three Bluetooth links are taken) try to take the chat slots: junk
    // ciphertext, then a real opener. They get unfinished slots only, which give way to each other; Caio's chat, idle
    // for over two minutes, stays.
    {
        PrivatePhone chatCaio{caio, bridgeId};
        chatCaio.open(150);
        clear();
        run(130000);
        bia.write(bia.announce());
        run(200);
        std::vector<Phone> strangers;
        for (uint8_t i = 0; i < 3; ++i)
            strangers.emplace_back("x", uint8_t(90 + i));
        for (const auto &stranger : strangers)
            bia.write(stranger.announce({}, 6), bia.handle);
        run(300);
        for (size_t i = 0; i < 2; ++i) {
            PrivatePhone fake{strangers[i], bridgeId};
            uint8_t junk[40] = {1, 2, 3};
            bia.write(fake.directed(0x11, {junk, sizeof(junk)}), bia.handle);
            run(4000);
        }
        clear();
        {
            PrivatePhone opener{strangers[2], bridgeId};
            uint8_t e[32];
            for (int i = 0; i < 32; ++i)
                e[i] = uint8_t(7 + 3 * i);
            opener.handshake.start(true, e);
            uint8_t m1[NOISE_MESSAGE1];
            CHECK(opener.handshake.writeMessage1(m1));
            bia.write(opener.directed(0x10, {m1, sizeof(m1)}), bia.handle);
            run(200);
            size_t answered = 0;
            for (const auto &frame : received(bia.handle)) {
                Packet p;
                CHECK(decode({frame.data(), frame.size()}, p) == Result::Ok);
                answered += p.type == 0x10 && p.recipient == strangers[2].id && p.payload.size == NOISE_MESSAGE2;
            }
            CHECK(answered == 1);
        }
        clear();
        chatCaio.message("C-1", "ponte");
        run(200);
        chatCaio.read(delivered, said);
        CHECK(delivered == std::vector<std::string>{"C-1"} && said.size() == 1);
        // Bia's chat, in use, stayed as well.
        mailBia.message("C-2", "ponte");
        run(200);
        mailBia.read(delivered, said);
        CHECK(said.size() == 2);
    }
    // The owner turns it off: nothing goes, nothing comes, nothing kept is delivered later; then on again.
    clear();
    run(10000);
    directFromLoRa(0x7777, "@bia guardada");
    run(200);
    fakeApp.clear();
    fromLoRa(0, "!ponte privado off", 1);
    run(1100);
    CHECK(fakeApp.size() == 1 && app(0) == "[bridge] Mensagens privadas pelo LoRa desligadas.");
    clear();
    mailBia.message("M-1", "@Casa Norte oi");
    run(200);
    directFromLoRa(0x7777, "@bia ainda?");
    run(200);
    mailBia.read(delivered, said);
    CHECK(said == std::vector<std::string>{"Mensagens privadas pelo LoRa est\xc3\xa3o desligadas neste r\xc3\xa1"
                                           "dio. / Direct messages over LoRa are off here."});
    fromLoRa(0, "!bridge dm on", 1);
    run(1100);
    CHECK(fakeApp.size() == 2 && app(1).rfind("[bridge] Direct messages on", 0) == 0);
    clear();
    run(31000);
    bia.write(bia.announce());
    mailBia.message("M-2", "ponte");
    run(300);
    mailBia.read(delivered, said);
    for (const auto &line : said)
        CHECK(line.find("guardada") == std::string::npos && line.find("ainda?") == std::string::npos);
    // In ham mode nothing goes over LoRa encrypted, so nothing goes.
    owner.is_licensed = true;
    clear();
    run(25000);
    mailBia.message("H-1", "@Casa Norte oi");
    run(200);
    mailBia.read(delivered, said);
    CHECK(said.size() == 1 && said[0].rfind("R\xc3\xa1"
                                            "dio em modo radioamador",
                                            0) == 0);
    owner.is_licensed = false;
    // Nothing of stage 2 reached the public chat.
    for (const auto &line : chat(ana.handle, &bridgeId))
        CHECK(line.find("Casa Norte") == std::string::npos && line.find("Bitchat ana") == std::string::npos);
    fakePower.battery = false;
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
    run(200);
    // The other bridge carries Ana's text: the standby is real, and holds.
    fromLoRa(0x1234, "em espera");
    fakeSent.clear();
    run(3000);
    CHECK(radio().empty() && chat(bia.handle, &bridgeId).empty());
    run(60000);
    CHECK(std::string(bridge->status()) == "Standby: other bridge");
    run(30000);
    CHECK(std::string(bridge->status()).rfind("Starting LoRa: ", 0) == 0);
    run(31000);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa");
    ana.write(ana.announce());
    run(200);
    ana.write(ana.say("voltou"));
    run(3000);
    CHECK(radio().size() == 1 && radio()[0].find("voltou") != std::string::npos);
    fakeRadio.clear();
    // Anyone with the key of a public channel can write the proof. A "bridge" that never carries the text holds
    // this one back a minute at most: then it takes the text back, and believes no such claim for half an hour.
    caio.write(other.announce({lower.data(), lower.size()}, 6), caio.handle);
    run(200);
    CHECK(std::string(bridge->status()) == "Standby: other bridge");
    ana.write(ana.say("ninguem leva"));
    run(3000);
    CHECK(radio().empty());
    run(60000);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa");
    ana.write(ana.say("agora vai"));
    run(3000);
    CHECK(radio().size() == 1 && radio()[0].find("agora vai") != std::string::npos);
    caio.write(other.announce({lower.data(), lower.size()}, 6), caio.handle);
    run(200);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa");
    fakeRadio.clear();

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
    {
        const auto radioKept = fakeRadio;
        privateChat(ana, bia, caio, bridgeId);
        fakeRadio = radioKept;
        for (const auto *phone : {&ana, &bia, &caio}) {
            phone->connect();
            phone->write(phone->announce());
        }
        run(1000);
    }
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
    // Once set, one link is one vote: an older announcement replayed by another device does not take the time
    // back against Dora's, and a replayed message is stale.
    Phone eco("eco", 10, 15);
    eco.connect();
    run(200);
    forcedStamp = nowUtc() - 600000;
    const Frame old = dora.say("mensagem antiga");
    const Frame oldAnnounce = dora.announce();
    forcedStamp = 0;
    dora.write(oldAnnounce, eco.handle);
    dora.write(old, eco.handle);
    run(3000);
    CHECK(radio().size() == 1 && std::string(bridge->status()) == "Ready: Bitchat + LoRa");
    // A device on another link announcing a year ahead does not carry the time off against Dora's either.
    const size_t radioMark = fakeRadio.size();
    {
        Phone far("far", 11, 16);
        far.connect();
        run(200);
        forcedStamp = nowUtc() + (365ULL * 86400 + 3 * 3600) * 1000;
        far.write(far.announce());
        forcedStamp = 0;
        stamp = 0; // the phones' own clocks again
        run(200);
        dora.write(dora.announce());
        dora.write(dora.say("hora de sempre"));
        run(3000);
        CHECK(radio().size() == 2 && radio()[1].find("hora de sempre") != std::string::npos);
        fakeRadio.erase(fakeRadio.begin() + radioMark, fakeRadio.end());
    }
    // A phone's ID is the hash of its public noise key, so a device may announce that ID with a signing key of its
    // own. The real phone, announcing itself again over its own link, takes the place back, and its text goes.
    {
        Phone gil("gil", 12, 17), squat("gil", 13, 16);
        squat.noiseKey = gil.noiseKey;
        squat.noisePublic = gil.noisePublic;
        squat.id = gil.id;
        // (Three Bluetooth links at most: Eco leaves.)
        bleDisconnect(eco.handle);
        gil.connect();
        run(200);
        squat.write(squat.announce());
        run(200);
        gil.write(gil.announce());
        gil.write(gil.say("sou o gil"));
        run(3000);
        CHECK(radio().size() == 2 && radio()[1].find("sou o gil") != std::string::npos);
        fakeRadio.erase(fakeRadio.begin() + radioMark, fakeRadio.end());
    }
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
    CHECK(fakeApp.size() == 8 && app(5).rfind("[bridge] !bridge shows the state.", 0) == 0 && app(5).size() <= 200 &&
          app(5).find("!ponte quem, guia") != std::string::npos && app(5).back() == '.');
    // The newer commands follow in a second message.
    CHECK(app(6).rfind("[bridge] Also: prefix on", 0) == 0 && app(6).size() <= 200 &&
          app(6).find("sos <text>") != std::string::npos && app(6).find("!ponte recado") != std::string::npos &&
          app(6).back() == '.');
    // And the private messages in a third.
    CHECK(app(7).rfind("[bridge] dm on, dm off", 0) == 0 && app(7).size() <= 200 && app(7).back() == '.');
    fakeApp.pop_back();
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

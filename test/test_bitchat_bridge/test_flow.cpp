// End-to-end host test: the production bridge module and BLE adapter, three simulated phones and a simulated radio.
#include "HeapModel.h"
#include "bridge/BitchatBridgeModule.h"
#include "bridge/BridgeDefaults.h"
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
static void fromLoRa(NodeNum from, const std::string &message, uint8_t channel = 1)
{
    static uint32_t id = 5000;
    meshtastic_MeshPacket packet;
    packet.from = from;
    packet.id = ++id;
    packet.channel = channel;
    packet.decoded.payload.size = uint16_t(message.size());
    memcpy(packet.decoded.payload.bytes, message.data(), message.size());
    bridge->handleReceived(packet);
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

    // No phone connected: nothing is signed, queued or written to flash for nobody.
    for (const auto *phone : {&ana, &bia, &caio})
        bleDisconnect(phone->handle);
    const unsigned writes = Preferences::writes;
    fakeSent.clear();
    fromLoRa(0x1234, "ninguem ouvindo");
    run(25000);
    CHECK(fakeSent.empty() && Preferences::writes == writes);
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
    CHECK(fakeApp.size() == 6 && app(5).rfind("[bridge] !bridge shows the state.", 0) == 0 && app(5).size() <= 200);
    fromLoRa(0, "!bridge link off", 1);
    run(1100);
    CHECK(!bridge->linked() && app(6).rfind("[bridge] Link off", 0) == 0);
    fromLoRa(0, "!Ponte Link On", 1);
    run(1100);
    CHECK(bridge->linked() && app(7).rfind("[bridge] Link on", 0) == 0);
    CHECK(radio().size() == onAir);
    delete bridge;
    bleDetach();
    CHECK(liveBytes == 0);
    std::cout << "PASS: " << checks
              << " end-to-end checks; group relay, duplicates, plain and prefixed export, splitting, LoRa import, "
                 "second bridge, crowding, airtime (host fakes, no hardware)\n";
}

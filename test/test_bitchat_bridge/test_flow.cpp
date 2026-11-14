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
    CHECK(std::string(bridge->status()) == "Starting LoRa: 30 s");
    run(31000);
    CHECK(std::string(bridge->status()) == "Ready: Bitchat + LoRa" && !bridge->hint()[0]);

    Phone ana("ana", 1, 11), bia("bia", 2, 12), caio("caio", 3, 13);
    for (const auto *phone : {&ana, &bia, &caio})
        phone->connect();
    run(100);
    // Each phone learns the bridge identity as soon as it subscribes.
    Announcement bridgeIdentity;
    PeerId bridgeId{};
    for (const auto *phone : {&ana, &bia, &caio}) {
        const auto frames = received(phone->handle);
        CHECK(frames.size() == 1);
        Packet packet;
        CHECK(decode({frames[0].data(), frames[0].size()}, packet) == Result::Ok && packet.type == 1);
        CHECK(decodeAnnouncement(packet.payload, bridgeIdentity) == Result::Ok && bridgeIdentity.gateway);
        bridgeId = packet.sender;
    }
    for (const auto *phone : {&ana, &bia, &caio})
        phone->write(phone->announce());
    run(400);

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
        std::array<uint8_t, 24> tlv{GATEWAY_TLV, 22};
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
    CHECK(std::string(bridge->status()) == "Starting LoRa: 30 s");
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

    // No phone connected: nothing is signed, queued or written to flash for nobody.
    for (const auto *phone : {&ana, &bia, &caio})
        bleDisconnect(phone->handle);
    const unsigned writes = Preferences::writes;
    fakeSent.clear();
    fromLoRa(0x1234, "ninguem ouvindo");
    run(25000);
    CHECK(fakeSent.empty() && Preferences::writes == writes);

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
    bleDetach();
    CHECK(liveBytes == 0);
    std::cout << "PASS: " << checks
              << " end-to-end checks; group relay, duplicates, plain and prefixed export, splitting, LoRa import, "
                 "second bridge, crowding, airtime (host fakes, no hardware)\n";
}

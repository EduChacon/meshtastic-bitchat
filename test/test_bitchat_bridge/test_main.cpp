#include "bridge/BridgeCodec.h"
#include "bridge/BridgeCrypto.h"
#include "bridge/BridgeState.h"
#include "bridge/BridgeText.h"
#include "bridge/BridgeTunnel.h"
#include "vectors.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace bitchat;
static BridgeCrypto crypto;
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

static std::vector<uint8_t> unhex(const char *text)
{
    const size_t length = std::strlen(text);
    CHECK(length % 2 == 0);
    std::vector<uint8_t> bytes(length / 2);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = uint8_t(std::stoul(std::string(text + 2 * i, 2), nullptr, 16));
    return bytes;
}
static Bytes utf8(const char *value)
{
    return {reinterpret_cast<const uint8_t *>(value), std::strlen(value)};
}
static std::string string(const NativeText &value)
{
    return {reinterpret_cast<const char *>(value.data.data()), value.size};
}
static Bytes bytes(const std::vector<uint8_t> &data)
{
    return {data.data(), data.size()};
}

static void officialVectors()
{
    for (const auto &v : VECTORS) {
        auto wire = unhex(v.wire);
        auto canonical = unhex(v.canonical);
        auto key = unhex(v.key);
        Packet packet;
        Frame output;
        CHECK(decode(bytes(wire), packet) == Result::Ok);
        size_t expectedLength = 0;
        CHECK(wireFrameLength(bytes(wire), expectedLength) == Result::Ok);
        CHECK(expectedLength == wire.size());
        CHECK(signingBytes(bytes(wire), output) == Result::Ok);
        if (!equalConstantTime(output.bytes(), bytes(canonical)))
            std::cerr << "Canonical mismatch: " << v.name << '\n';
        CHECK(equalConstantTime(output.bytes(), bytes(canonical)));
        if (packet.signedPacket()) {
            CHECK(crypto.verify(output.bytes(), packet.signature, key.data()));
            wire[2] = 2;
            CHECK(signingBytes(bytes(wire), output) == Result::Ok);
            CHECK(crypto.verify(output.bytes(), packet.signature, key.data()));
            wire[3] ^= 1;
            CHECK(signingBytes(bytes(wire), output) == Result::Ok);
            CHECK(!crypto.verify(output.bytes(), packet.signature, key.data()));
        }
    }
}

static void signatureRejections()
{
    auto wire = unhex(VECTORS[0].wire);
    auto key = unhex(VECTORS[0].key);
    Packet packet;
    Frame canonical;
    CHECK(decode(bytes(wire), packet) == Result::Ok);
    CHECK(signingBytes(bytes(wire), canonical) == Result::Ok);
    std::array<uint8_t, 64> signature;
    std::copy_n(packet.signature.data, 64, signature.begin());
    const auto order = unhex("edd3f55c1a631258d69cf7a2def9de1400000000000000000000000000000010");
    unsigned carry = 0;
    for (size_t i = 0; i < 32; ++i) {
        carry += signature[32 + i] + order[i];
        signature[32 + i] = carry & 255;
        carry >>= 8;
    }
    CHECK(!crypto.verify(canonical.bytes(), {signature.data(), signature.size()}, key.data()));
    std::array<uint8_t, 32> identity{};
    identity[0] = 1;
    signature.fill(0);
    signature[0] = 1;
    CHECK(!crypto.verify(canonical.bytes(), {signature.data(), signature.size()}, identity.data()));
    CHECK(!crypto.verify(canonical.bytes(), {signature.data(), 63}, key.data()));
    for (const char *point : {"0000000000000000000000000000000000000000000000000000000000000000",
                              "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
                              "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
                              "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
                              "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a"}) {
        const auto lowOrder = unhex(point);
        CHECK(!crypto.verify(canonical.bytes(), packet.signature, lowOrder.data()));
    }
}

static void identitiesAndText()
{
    auto announce = unhex(VECTORS[0].wire);
    auto text = unhex(VECTORS[2].wire);
    PeerTable peers;
    Peer *peer = nullptr;
    CHECK(peers.authenticate(crypto, bytes(text), 1795000000000ULL, 1, peer) == Result::Unauthenticated);
    CHECK(peers.announce(crypto, bytes(announce), 0, 1, peer) == Result::NoClock);
    CHECK(peers.announce(crypto, bytes(announce), 1795000000000ULL, 1, peer) == Result::Ok);
    CHECK(peers.size() == 1);
    CHECK(peers.authenticate(crypto, bytes(text), 1795000000000ULL, 2, peer) == Result::Ok);
    CHECK(peers.authenticate(crypto, bytes(text), 1795000200000ULL, 2, peer) == Result::Expired);
    Packet p;
    CHECK(decode(bytes(text), p) == Result::Ok);
    Bytes body;
    CHECK(publicTextForLoRa(p, body) == Result::Ok);
    p.flags |= 1;
    CHECK(publicTextForLoRa(p, body) == Result::Policy);
    p.flags &= uint8_t(~1);
    CHECK(publicTextForLoRa(p, body) == Result::Ok);
    // The prefix is optional; tunnel text and text another bridge imported never go to LoRa.
    Packet typed = p;
    typed.payload = utf8("plain chat");
    CHECK(publicTextForLoRa(typed, body) == Result::Ok && body.size == 10);
    typed.payload = utf8("!lora hello");
    CHECK(publicTextForLoRa(typed, body) == Result::Ok && body.size == 5 && body.data[0] == 'h');
    typed.payload = utf8("!lora ");
    CHECK(publicTextForLoRa(typed, body) == Result::Malformed);
    typed.payload = utf8("!bctun hello");
    CHECK(publicTextForLoRa(typed, body) == Result::Policy);
    typed.payload = utf8("[MT Ana] hello");
    CHECK(publicTextForLoRa(typed, body) == Result::Policy);
    typed.payload = utf8("ok");
    typed.flags &= uint8_t(~2);
    CHECK(publicTextForLoRa(typed, body) == Result::Policy);
    std::array<uint8_t, 32> key{};
    key.fill(0x55);
    const uint8_t malformed[] = {0xf0, 0x80, 0x80, 0x80};
    CHECK(!validUtf8({malformed, sizeof(malformed)}));
    const uint8_t emoji[] = {0xf0, 0x9f, 0x8c, 0x8e};
    CHECK(validUtf8({emoji, sizeof(emoji)}));
    CHECK(!validUtf8({emoji, 3}));
    const uint8_t surrogate[] = {0xed, 0xa0, 0x80};
    CHECK(!validUtf8({surrogate, sizeof(surrogate)}));

    char label[LABEL_LIMIT + 1];
    CHECK(cleanLabel(utf8("Ana"), label, sizeof(label)) == 3 && !std::strcmp(label, "Ana"));
    CHECK(cleanLabel(utf8(" [MT x]:\n Jo\xc3\xa3o  "), label, sizeof(label)) == 11 && !std::strcmp(label, " MT x Jo\xc3\xa3o"));
    CHECK(cleanLabel({malformed, sizeof(malformed)}, label, sizeof(label)) == 0);
    CHECK(cleanLabel(utf8("\xc3\xa3\xc3\xa3\xc3\xa3"), label, 6) == 4);
    std::string longName(80, 'n');
    CHECK(cleanLabel(utf8(longName.c_str()), label, sizeof(label)) == LABEL_LIMIT);

    NativeText native;
    TextPlan plan;
    CHECK(planNativeText(3, utf8("oi"), plan) == Result::Ok && plan.count == 1);
    CHECK(makeNativeText(utf8("Ana"), utf8("oi"), plan, 0, native) == Result::Ok && string(native) == "Ana: oi");
    CHECK(makeNativeText(utf8("Ana"), utf8("oi"), plan, 1, native) == Result::Malformed);
    CHECK(planNativeText(0, utf8("oi"), plan) == Result::Malformed);
    CHECK(planNativeText(3, {malformed, sizeof(malformed)}, plan) == Result::Malformed);
    std::string exact(LORA_TEXT_LIMIT - 5, 'x');
    CHECK(planNativeText(3, utf8(exact.c_str()), plan) == Result::Ok && plan.count == 1);
    CHECK(makeNativeText(utf8("Ana"), utf8(exact.c_str()), plan, 0, native) == Result::Ok && native.size == LORA_TEXT_LIMIT);
    // Longer text is split on spaces and never inside a character; nothing is truncated.
    std::string longText;
    while (longText.size() < 400)
        longText += "ol\xc3\xa1 mundo ";
    CHECK(planNativeText(3, utf8(longText.c_str()), plan) == Result::Ok && plan.count == 3);
    std::string joined;
    for (size_t part = 0; part < plan.count; ++part) {
        CHECK(makeNativeText(utf8("Ana"), utf8(longText.c_str()), plan, part, native) == Result::Ok);
        CHECK(native.size <= LORA_TEXT_LIMIT && validUtf8(native.bytes()));
        const std::string head = "Ana (" + std::to_string(part + 1) + "/3): ";
        CHECK(string(native).compare(0, head.size(), head) == 0);
        if (part + 1 < plan.count)
            CHECK(string(native).back() == ' ');
        joined += string(native).substr(head.size());
    }
    CHECK(joined == longText);
    std::string unbroken(3 * (LORA_TEXT_LIMIT - 11), 'y');
    CHECK(planNativeText(3, utf8(unbroken.c_str()), plan) == Result::Ok && plan.count == 3);
    unbroken += 'y';
    CHECK(planNativeText(3, utf8(unbroken.c_str()), plan) == Result::Oversize);

    CHECK(makeBitchatText(utf8("Ana"), utf8("oi"), native) == Result::Ok && string(native) == "[MT Ana] oi");
    typed.flags |= 2;
    typed.payload = native.bytes();
    CHECK(publicTextForLoRa(typed, body) == Result::Policy);
    CHECK(makeBitchatText(utf8(""), utf8("oi"), native) == Result::Malformed);
    std::string full(233, 'z');
    CHECK(makeBitchatText(utf8(longName.substr(0, LABEL_LIMIT).c_str()), utf8(full.c_str()), native) == Result::Ok);

    std::array<uint8_t, 24> proof;
    proof[0] = GATEWAY_TLV;
    proof[1] = 22;
    makeGatewayProof(crypto, p.sender, 17, key.data(), proof.data() + 2);
    Packet owner = p;
    owner.type = 1;
    owner.payload = {proof.data(), proof.size()};
    uint32_t node = 0;
    CHECK(gatewayProofNode(crypto, owner, 18, key.data(), node) && node == 17);
    CHECK(!gatewayProofNode(crypto, owner, 17, key.data(), node));
    proof[20] ^= 1;
    CHECK(!gatewayProofNode(crypto, owner, 18, key.data(), node));
    proof[1] = 255;
    CHECK(!gatewayProofNode(crypto, owner, 18, key.data(), node));
}

static void queuesAndTime()
{
    BoundedQueue<int, 2> queue;
    CHECK(queue.push(1) && queue.push(2));
    CHECK(!queue.push(3));
    CHECK(*queue.front() == 1);
    queue.pop();
    CHECK(queue.push(3));
    CHECK(*queue.front() == 2);
    queue.pop();
    CHECK(*queue.front() == 3);
    queue.pop();
    CHECK(queue.front() == nullptr);
    RateGate gate;
    CHECK(gate.admit(0xfffffff0U, 50));
    CHECK(!gate.admit(20, 50));
    CHECK(gate.admit(100, 50));
    // A burst is admitted at once, then one message per refill interval, across a millis() wrap.
    RateBucket bucket;
    for (int i = 0; i < 3; ++i)
        CHECK(bucket.admit(0xfffffff0U, 3, 100));
    CHECK(!bucket.admit(0xfffffff0U, 3, 100));
    CHECK(!bucket.admit(50, 3, 100));
    CHECK(bucket.admit(100, 3, 100));
    CHECK(!bucket.admit(150, 3, 100));
    CHECK(bucket.admit(250, 3, 100));
    for (int i = 0; i < 3; ++i)
        CHECK(bucket.admit(100000, 3, 100));
    CHECK(!bucket.admit(100000, 3, 100));
    DedupCache<2> dedup;
    Digest a{}, b{}, c{};
    b[0] = 1;
    c[0] = 2;
    CHECK(dedup.insert(a, 0xfffffff0U, 50) == Result::Ok);
    CHECK(dedup.insert(a, 20, 50) == Result::Duplicate);
    CHECK(dedup.insert(b, 20, 50) == Result::Ok);
    CHECK(dedup.insert(c, 20, 50) == Result::Full);
    CHECK(dedup.insert(c, 100, 50) == Result::Ok);
    CHECK(!fresh(0, 1795000000000ULL));
    CHECK(!fresh(UINT64_MAX, 1));
    CHECK(fresh(120001, 1));
    CHECK(!fresh(120002, 1));
}

static void tunnels()
{
    std::array<uint8_t, 32> key{};
    key.fill(0x75);
    TunnelIdentity identity{{1, 2, 3, 4, 5, 6, 7, 8}, 22, 33, 44};
    auto wire = unhex(VECTORS[0].wire);
    TunnelReassembly reassembly;
    Frame output;
    const size_t count = (wire.size() + TUNNEL_CHUNK - 1) / TUNNEL_CHUNK;
    for (size_t i = count; i-- > 0;) {
        std::array<uint8_t, TUNNEL_MAX_FRAGMENT> encoded;
        size_t length;
        CHECK(encodeTunnelFragment(crypto, bytes(wire), identity, key.data(), uint8_t(i), encoded.data(), encoded.size(),
                                   length) == Result::Ok);
        TunnelFragment fragment;
        CHECK(decodeTunnelFragment(crypto, {encoded.data(), length}, identity.domain, key.data(), fragment) == Result::Ok);
        const auto result = reassembly.accept(crypto, fragment, 10, output);
        CHECK(result == (i == 0 ? Result::Ok : Result::Incomplete));
        if (i != 0)
            CHECK(reassembly.accept(crypto, fragment, 10, output) == Result::Duplicate);
        encoded[length - 1] ^= 1;
        CHECK(decodeTunnelFragment(crypto, {encoded.data(), length}, identity.domain, key.data(), fragment) ==
              Result::Unauthenticated);
    }
    CHECK(equalConstantTime(output.bytes(), bytes(wire)));
    std::array<uint8_t, TUNNEL_MAX_FRAGMENT> encoded;
    size_t length;
    CHECK(encodeTunnelFragment(crypto, bytes(wire), identity, key.data(), 0, encoded.data(), encoded.size(), length) ==
          Result::Ok);
    TunnelFragment fragment;
    CHECK(decodeTunnelFragment(crypto, {encoded.data(), length}, identity.domain, key.data(), fragment) == Result::Ok);
    CHECK(reassembly.accept(crypto, fragment, 100, output) == Result::Incomplete);
    CHECK(reassembly.expire(45100) == 1);
    encoded[41] = 255;
    CHECK(decodeTunnelFragment(crypto, {encoded.data(), length}, identity.domain, key.data(), fragment) == Result::Malformed);
}

static void fragments()
{
    auto wire = unhex(VECTORS[0].wire);
    Packet original;
    CHECK(decode(bytes(wire), original) == Result::Ok);
    BleReassembly assembly;
    Frame out;
    for (int index : {1, 0}) {
        std::vector<uint8_t> payload(13);
        payload[0] = 42;
        write16(payload.data() + 8, uint16_t(index));
        write16(payload.data() + 10, 2);
        payload[12] = original.type;
        const size_t begin = index ? wire.size() / 2 : 0;
        const size_t end = index ? wire.size() : wire.size() / 2;
        payload.insert(payload.end(), wire.begin() + begin, wire.begin() + end);
        Frame fragmentWire;
        CHECK(encode(0x20, original.sender, nullptr, original.timestamp, 5, bytes(payload), {}, fragmentWire) == Result::Ok);
        Packet fragment;
        CHECK(decode(fragmentWire.bytes(), fragment) == Result::Ok);
        CHECK(assembly.accept(fragment, 100, out) == (index ? Result::Incomplete : Result::Ok));
    }
    CHECK(out.size == wire.size());
    CHECK(out.data[2] == 5);
    out.data[2] = wire[2];
    CHECK(equalConstantTime(out.bytes(), bytes(wire)));
}

static void malformedInputs()
{
    auto wire = unhex(VECTORS[0].wire);
    Packet packet;
    for (size_t size = 0; size < wire.size(); ++size)
        CHECK(decode({wire.data(), size}, packet) != Result::Ok);
    auto invalid = wire;
    invalid[11] |= 0x80;
    CHECK(decode(bytes(invalid), packet) == Result::Unsupported);
    invalid = wire;
    invalid[12] = 0xff;
    invalid[13] = 0xff;
    CHECK(decode(bytes(invalid), packet) == Result::Oversize);
    Frame output;
    invalid = wire;
    invalid[1] = 0x21;
    CHECK(relay(bytes(invalid), output) == Result::Policy);
    invalid = wire;
    invalid[11] |= 0x10;
    CHECK(relay(bytes(invalid), output) == Result::Policy);
    invalid = wire;
    invalid[2] = 1;
    CHECK(relay(bytes(invalid), output) == Result::Policy);
    for (const auto &vector : VECTORS) {
        if (std::string(vector.name) != "compressed")
            continue;
        invalid = unhex(vector.wire);
        CHECK(decode(bytes(invalid), packet) == Result::Ok);
        const size_t offset = packet.payload.data - invalid.data();
        invalid[offset] = 0xff;
        invalid[offset + 1] = 0xff;
        CHECK(decode(bytes(invalid), packet) == Result::Oversize);
        invalid[offset] = invalid[offset + 1] = 0;
        CHECK(decode(bytes(invalid), packet) == Result::Oversize);
    }
    std::mt19937 random(0xBC2026);
    for (size_t i = 0; i < 100000; ++i) {
        Frame mutated;
        mutated.size = random() % (MAX_FRAME + 1);
        for (size_t j = 0; j < mutated.size; ++j)
            mutated.data[j] = uint8_t(random());
        if (decode(mutated.bytes(), packet) == Result::Ok)
            CHECK(signingBytes(mutated.bytes(), output) == Result::Ok);
        size_t length = 0;
        wireFrameLength(mutated.bytes(), length);
        validUtf8(mutated.bytes());
    }
}

static void twoGatewaySimulation()
{
    auto announce = unhex(VECTORS[0].wire);
    auto text = unhex(VECTORS[3].wire);
    PeerTable gatewayA, gatewayB;
    Peer *peer = nullptr;
    CHECK(gatewayA.authenticate(crypto, bytes(announce), 1795000000000ULL, 1, peer) == Result::Ok);
    CHECK(gatewayB.authenticate(crypto, bytes(announce), 1795000000000ULL, 1, peer) == Result::Ok);
    CHECK(gatewayA.authenticate(crypto, bytes(text), 1795000000000ULL, 2, peer) == Result::Ok);
    Digest id;
    CHECK(packetDigest(crypto, bytes(text), id) == Result::Ok);
    DedupCache<4> cacheA, cacheB;
    CHECK(cacheA.insert(id, 2, 125000) == Result::Ok);
    Frame outgoing, received, local;
    CHECK(relay(bytes(text), outgoing) == Result::Ok);
    std::array<uint8_t, 32> key{};
    key.fill(0x61);
    TunnelIdentity identity{{8, 7, 6, 5, 4, 3, 2, 1}, 11, 12, 13};
    TunnelReassembly receiver;
    const size_t count = (outgoing.size + TUNNEL_CHUNK - 1) / TUNNEL_CHUNK;
    for (size_t index = count; index-- > 0;) {
        std::array<uint8_t, TUNNEL_MAX_FRAGMENT> envelope;
        size_t size;
        CHECK(encodeTunnelFragment(crypto, outgoing.bytes(), identity, key.data(), index, envelope.data(), envelope.size(),
                                   size) == Result::Ok);
        TunnelFragment fragment;
        auto wrongDomain = identity.domain;
        wrongDomain[0] ^= 1;
        CHECK(decodeTunnelFragment(crypto, {envelope.data(), size}, wrongDomain, key.data(), fragment) != Result::Ok);
        CHECK(decodeTunnelFragment(crypto, {envelope.data(), size}, identity.domain, key.data(), fragment) == Result::Ok);
        CHECK(receiver.accept(crypto, fragment, 3000, received) == (index ? Result::Incomplete : Result::Ok));
    }
    CHECK(gatewayB.authenticate(crypto, received.bytes(), 1795000003000ULL, 3000, peer) == Result::Ok);
    Digest remoteId;
    CHECK(packetDigest(crypto, received.bytes(), remoteId) == Result::Ok);
    CHECK(id == remoteId);
    CHECK(cacheB.insert(remoteId, 3000, 125000) == Result::Ok);
    CHECK(cacheB.insert(remoteId, 3001, 125000) == Result::Duplicate);
    CHECK(relay(received.bytes(), local) == Result::Ok);
    CHECK(local.data[2] == text[2] - 2);
    CHECK(packetDigest(crypto, local.bytes(), remoteId) == Result::Ok);
    CHECK(cacheA.insert(remoteId, 5000, 125000) == Result::Duplicate);
    CHECK(gatewayB.authenticate(crypto, local.bytes(), 1795000200000ULL, 200000, peer) == Result::Expired);
}

int main()
{
    officialVectors();
    signatureRejections();
    identitiesAndText();
    queuesAndTime();
    tunnels();
    fragments();
    twoGatewaySimulation();
    malformedInputs();
    std::cout << "PASS: " << checks
              << " assertions; 19 official-client vectors; 100000 "
                 "malformed-input cases\n";
}

// The bridge's Noise against transcripts of an independent implementation (tools/bitchat/noise_vectors.py), and its
// behaviour with forged, replayed and reordered transport messages.
#include "bridge/BridgeNoise.h"
#include "noise_vectors.h"
#include <Curve25519.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace bitchat;

static int checks = 0, failures = 0;
#define CHECK(x)                                                                                                                 \
    do {                                                                                                                         \
        ++checks;                                                                                                                \
        if (!(x)) {                                                                                                              \
            ++failures;                                                                                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #x);                                                                     \
        }                                                                                                                        \
    } while (0)

static std::vector<uint8_t> hex(const char *text)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; text[i] && text[i + 1]; i += 2) {
        unsigned value = 0;
        std::sscanf(text + i, "%2x", &value);
        out.push_back(uint8_t(value));
    }
    return out;
}

// The bridge keeps its static private key clamped, as X25519 uses it.
static std::vector<uint8_t> clamped(const char *text)
{
    auto key = hex(text);
    key[0] &= 0xf8;
    key[31] = (key[31] & 0x7f) | 0x40;
    return key;
}

static Bytes bytes(const std::vector<uint8_t> &v)
{
    return {v.data(), v.size()};
}

static void vector(const NoiseVector &v)
{
    const auto iStatic = clamped(v.initiatorStatic), rStatic = clamped(v.responderStatic);
    const auto iPublic = hex(v.initiatorPublic), rPublic = hex(v.responderPublic);
    uint8_t derived[32];
    Curve25519::eval(derived, iStatic.data(), nullptr);
    CHECK(std::memcmp(derived, iPublic.data(), 32) == 0);
    Curve25519::eval(derived, rStatic.data(), nullptr);
    CHECK(std::memcmp(derived, rPublic.data(), 32) == 0);

    NoiseHandshake initiator, responder;
    initiator.start(true, hex(v.initiatorEphemeral).data());
    responder.start(false, hex(v.responderEphemeral).data());
    uint8_t m1[NOISE_MESSAGE1], m2[NOISE_MESSAGE2], m3[NOISE_MESSAGE3];
    CHECK(initiator.writeMessage1(m1));
    CHECK(std::vector<uint8_t>(m1, m1 + sizeof(m1)) == hex(v.message1));
    CHECK(responder.readMessage1({m1, sizeof(m1)}));
    CHECK(responder.writeMessage2(rStatic.data(), rPublic.data(), m2));
    CHECK(std::vector<uint8_t>(m2, m2 + sizeof(m2)) == hex(v.message2));
    CHECK(initiator.readMessage2({m2, sizeof(m2)}));
    CHECK(std::memcmp(initiator.remoteStatic(), rPublic.data(), 32) == 0);
    CHECK(initiator.writeMessage3(iStatic.data(), iPublic.data(), m3));
    CHECK(std::vector<uint8_t>(m3, m3 + sizeof(m3)) == hex(v.message3));
    CHECK(responder.readMessage3({m3, sizeof(m3)}));
    CHECK(std::memcmp(responder.remoteStatic(), iPublic.data(), 32) == 0);

    NoiseSender iSend, rSend;
    NoiseReceiver iReceive, rReceive;
    initiator.split(iSend, iReceive);
    responder.split(rSend, rReceive);
    for (size_t t = 0; t < v.transportCount; ++t) {
        const auto &step = v.transport[t];
        const bool fromInitiator = std::strcmp(step.from, "initiator") == 0;
        const auto plain = hex(step.plain), wire = hex(step.wire);
        // The bridge's encryption gives the reference bytes...
        std::vector<uint8_t> sealed(plain.size() + NOISE_TRANSPORT_OVERHEAD);
        CHECK(noiseEncrypt(fromInitiator ? iSend : rSend, bytes(plain), sealed.data()));
        CHECK(sealed == wire);
        // ...and its decryption reads them back.
        std::vector<uint8_t> opened(wire.size() - NOISE_TRANSPORT_OVERHEAD + 1);
        CHECK(noiseDecrypt(fromInitiator ? rReceive : iReceive, bytes(wire), opened.data()));
        CHECK(std::vector<uint8_t>(opened.begin(), opened.begin() + plain.size()) == plain);
        // The same message twice is a replay.
        CHECK(!noiseDecrypt(fromInitiator ? rReceive : iReceive, bytes(wire), opened.data()));
    }
}

// A session of two bridges, then the message rules.
static void transport()
{
    uint8_t aKey[32], bKey[32], aPub[32], bPub[32], ae[32], be[32];
    for (int i = 0; i < 32; ++i) {
        aKey[i] = uint8_t(i * 7 + 1);
        bKey[i] = uint8_t(i * 13 + 5);
        ae[i] = uint8_t(i * 3 + 9);
        be[i] = uint8_t(i * 11 + 2);
    }
    aKey[0] &= 0xf8;
    aKey[31] = (aKey[31] & 0x7f) | 0x40;
    bKey[0] &= 0xf8;
    bKey[31] = (bKey[31] & 0x7f) | 0x40;
    Curve25519::eval(aPub, aKey, nullptr);
    Curve25519::eval(bPub, bKey, nullptr);
    NoiseHandshake a, b;
    a.start(true, ae);
    b.start(false, be);
    uint8_t m1[NOISE_MESSAGE1], m2[NOISE_MESSAGE2], m3[NOISE_MESSAGE3];
    CHECK(a.writeMessage1(m1) && b.readMessage1({m1, sizeof(m1)}) && b.writeMessage2(bKey, bPub, m2));

    // A forged message 2 is refused, and the responder only takes message 1 once it is the responder.
    uint8_t bad[NOISE_MESSAGE2];
    std::memcpy(bad, m2, sizeof(bad));
    bad[60] ^= 1;
    NoiseHandshake copy = a;
    CHECK(!copy.readMessage2({bad, sizeof(bad)}));
    CHECK(!a.readMessage1({m1, sizeof(m1)}));
    CHECK(!a.readMessage2({m2, sizeof(m2) - 1}));
    CHECK(a.readMessage2({m2, sizeof(m2)}) && a.writeMessage3(aKey, aPub, m3));
    uint8_t bad3[NOISE_MESSAGE3];
    std::memcpy(bad3, m3, sizeof(bad3));
    bad3[5] ^= 0x80;
    NoiseHandshake copyB = b;
    CHECK(!copyB.readMessage3({bad3, sizeof(bad3)}));
    CHECK(b.readMessage3({m3, sizeof(m3)}));

    // Each side proved the static key its peer ID stands for.
    PeerId idA, idB, seen;
    noisePeerId(aPub, idA);
    noisePeerId(bPub, idB);
    noisePeerId(b.remoteStatic(), seen);
    CHECK(seen == idA);
    noisePeerId(a.remoteStatic(), seen);
    CHECK(seen == idB);

    // A low-order public key ends the handshake: X25519 gives zero.
    NoiseHandshake weak;
    weak.start(false, be);
    uint8_t zero[NOISE_MESSAGE1] = {};
    CHECK(weak.readMessage1({zero, sizeof(zero)}));
    uint8_t out[NOISE_MESSAGE2];
    CHECK(!weak.writeMessage2(bKey, bPub, out));

    NoiseSender aSend, bSend;
    NoiseReceiver aReceive, bReceive;
    a.split(aSend, aReceive);
    b.split(bSend, bReceive);
    std::vector<std::vector<uint8_t>> sent;
    for (int i = 0; i < 80; ++i) {
        uint8_t plain[3] = {1, uint8_t(i), 9};
        std::vector<uint8_t> wire(sizeof(plain) + NOISE_TRANSPORT_OVERHEAD);
        CHECK(noiseEncrypt(aSend, {plain, sizeof(plain)}, wire.data()));
        sent.push_back(wire);
    }
    uint8_t opened[8];
    // Out of order within the window: accepted once each.
    CHECK(noiseDecrypt(bReceive, bytes(sent[5]), opened) && opened[1] == 5);
    CHECK(noiseDecrypt(bReceive, bytes(sent[2]), opened) && opened[1] == 2);
    CHECK(!noiseDecrypt(bReceive, bytes(sent[2]), opened));
    CHECK(noiseDecrypt(bReceive, bytes(sent[0]), opened) && opened[1] == 0);
    CHECK(noiseDecrypt(bReceive, bytes(sent[70]), opened) && opened[1] == 70);
    // Older than the window: refused, even if never seen.
    CHECK(!noiseDecrypt(bReceive, bytes(sent[6]), opened));
    CHECK(noiseDecrypt(bReceive, bytes(sent[69]), opened));
    // A forged message does not move the window.
    auto forged = sent[79];
    forged[6] ^= 1;
    CHECK(!noiseDecrypt(bReceive, bytes(forged), opened));
    CHECK(bReceive.highest == 70);
    CHECK(noiseDecrypt(bReceive, bytes(sent[79]), opened));
    // A forged nonce does not decrypt either.
    auto renumbered = sent[78];
    renumbered[3] = 77;
    CHECK(!noiseDecrypt(bReceive, bytes(renumbered), opened));
    CHECK(noiseDecrypt(bReceive, bytes(sent[78]), opened));
    // Too short to hold a nonce and a tag.
    CHECK(!noiseDecrypt(bReceive, {sent[0].data(), NOISE_TRANSPORT_OVERHEAD - 1}, opened));
    // The other direction has its own key.
    CHECK(!noiseDecrypt(aReceive, bytes(sent[1]), opened));
    std::vector<uint8_t> reply(4 + NOISE_TRANSPORT_OVERHEAD);
    const uint8_t text[4] = {3, 'a', 'b', 'c'};
    CHECK(noiseEncrypt(bSend, {text, sizeof(text)}, reply.data()));
    CHECK(noiseDecrypt(aReceive, bytes(reply), opened) && std::memcmp(opened, text, 4) == 0);
    // The last nonces: 2^32 - 2 is the final one.
    aSend.nonce = 0xfffffffeU;
    CHECK(noiseEncrypt(aSend, {text, sizeof(text)}, reply.data()));
    CHECK(!noiseEncrypt(aSend, {text, sizeof(text)}, reply.data()));
}

int main()
{
    for (const auto &v : NOISE_VECTORS)
        vector(v);
    transport();
    if (failures) {
        std::printf("noise: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    std::printf("noise: %d checks passed (%zu reference transcripts)\n", checks,
                sizeof(NOISE_VECTORS) / sizeof(NOISE_VECTORS[0]));
    return 0;
}

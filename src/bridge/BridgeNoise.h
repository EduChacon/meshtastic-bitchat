#pragma once

// The private chat of Bitchat: Noise_XX_25519_ChaChaPoly_SHA256 with an empty prologue and empty handshake
// payloads, and the app's transport framing: a 4-byte big-endian nonce before each ciphertext, received by
// that nonce within a sliding window, with no associated data.
#include "BridgeCodec.h"
#include <cstddef>
#include <cstdint>

namespace bitchat
{
constexpr size_t NOISE_KEY = 32;
constexpr size_t NOISE_TAG = 16;
constexpr size_t NOISE_MESSAGE1 = 32;
constexpr size_t NOISE_MESSAGE2 = 96;
constexpr size_t NOISE_MESSAGE3 = 64;
// The nonce before a transport ciphertext, and the tag after it.
constexpr size_t NOISE_TRANSPORT_OVERHEAD = 4 + NOISE_TAG;

struct NoiseSender {
    uint8_t key[NOISE_KEY] = {};
    uint32_t nonce = 0;
};

struct NoiseReceiver {
    uint8_t key[NOISE_KEY] = {};
    uint32_t highest = 0;
    uint64_t window = 0; // bit i: the nonce highest - i was accepted
    bool any = false;
};

// One XX handshake, either side. The static key is the bridge's own and is passed in at each step; the ephemeral
// key comes from the caller, so tests can give a fixed one.
class NoiseHandshake
{
  public:
    void start(bool initiator, const uint8_t ephemeralPrivate[NOISE_KEY]);
    bool initiator() const { return isInitiator; }
    // Initiator: message 1 (e).
    bool writeMessage1(uint8_t out[NOISE_MESSAGE1]);
    // Responder: reads message 1 and writes message 2 (e, ee, s, es).
    bool readMessage1(Bytes in);
    bool writeMessage2(const uint8_t staticPrivate[NOISE_KEY], const uint8_t staticPublic[NOISE_KEY],
                       uint8_t out[NOISE_MESSAGE2]);
    // Initiator: reads message 2 and writes message 3 (s, se).
    bool readMessage2(Bytes in);
    bool writeMessage3(const uint8_t staticPrivate[NOISE_KEY], const uint8_t staticPublic[NOISE_KEY],
                       uint8_t out[NOISE_MESSAGE3]);
    // Responder: reads message 3.
    bool readMessage3(Bytes in);
    // After the last message: the two directions, and the static key the other side proved.
    void split(NoiseSender &sender, NoiseReceiver &receiver);
    const uint8_t *remoteStatic() const { return rs; }
    void wipe();

  private:
    void mixHash(const uint8_t *data, size_t size);
    bool mixKey(const uint8_t *privateKey, const uint8_t *publicKey);
    bool encryptAndHash(const uint8_t *plain, size_t size, uint8_t *out);
    bool decryptAndHash(const uint8_t *in, size_t size, uint8_t *plain);

    uint8_t ck[32] = {}, h[32] = {}, k[32] = {};
    uint64_t n = 0;
    bool hasKey = false, isInitiator = false;
    uint8_t e[NOISE_KEY] = {}, ePublic[NOISE_KEY] = {}, re[NOISE_KEY] = {}, rs[NOISE_KEY] = {};
};

// `out` takes plain.size + NOISE_TRANSPORT_OVERHEAD bytes; false once the nonces run out.
bool noiseEncrypt(NoiseSender &sender, Bytes plain, uint8_t *out);
// `plain` takes in.size - NOISE_TRANSPORT_OVERHEAD bytes; false for a forged, replayed or too old message.
bool noiseDecrypt(NoiseReceiver &receiver, Bytes in, uint8_t *plain);

// Zeroes secrets on the stack in a way the compiler keeps.
void noiseWipe(void *data, size_t size);

// The peer ID a Noise static key stands for: the first 8 bytes of its SHA-256.
void noisePeerId(const uint8_t staticPublic[NOISE_KEY], PeerId &out);
} // namespace bitchat

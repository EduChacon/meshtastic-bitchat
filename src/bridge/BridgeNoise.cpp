#include "BridgeNoise.h"
#include <ChaChaPoly.h>
#include <Curve25519.h>
#include <SHA256.h>
#include <cstring>

namespace bitchat
{
// Exactly 32 bytes: the name is the first handshake hash, unhashed.
static constexpr char PROTOCOL_NAME[] = "Noise_XX_25519_ChaChaPoly_SHA256";
static_assert(sizeof(PROTOCOL_NAME) - 1 == 32, "the protocol name is used as the hash");
// Bitchat stops sending at 2^32 - 2.
static constexpr uint32_t LAST_NONCE = 0xfffffffeU;
static constexpr uint32_t WINDOW = 64;

static void wipeBytes(void *data, size_t size)
{
    volatile uint8_t *p = static_cast<volatile uint8_t *>(data);
    while (size--)
        *p++ = 0;
}

static void hmac(const uint8_t key[32], const uint8_t *a, size_t aSize, const uint8_t *b, size_t bSize, uint8_t out[32])
{
    SHA256 sha;
    sha.resetHMAC(key, 32);
    if (aSize)
        sha.update(a, aSize);
    if (bSize)
        sha.update(b, bSize);
    sha.finalizeHMAC(key, 32, out, 32);
    sha.clear();
}

// Noise's HKDF with two outputs.
static void hkdf2(const uint8_t chainingKey[32], const uint8_t *input, size_t size, uint8_t first[32], uint8_t second[32])
{
    uint8_t temp[32];
    hmac(chainingKey, input, size, nullptr, 0, temp);
    const uint8_t one = 1, two = 2;
    hmac(temp, &one, 1, nullptr, 0, first);
    hmac(temp, first, 32, &two, 1, second);
    wipeBytes(temp, sizeof(temp));
}

// The 96-bit nonce: four zero bytes, then the counter in little-endian order.
static void nonceBytes(uint64_t counter, uint8_t out[12])
{
    std::memset(out, 0, 4);
    for (int i = 0; i < 8; ++i)
        out[4 + i] = uint8_t(counter >> (8 * i));
}

static void seal(const uint8_t key[32], uint64_t counter, const uint8_t *ad, size_t adSize, const uint8_t *plain, size_t size,
                 uint8_t *out)
{
    uint8_t iv[12];
    nonceBytes(counter, iv);
    ChaChaPoly cipher;
    cipher.setKey(key, 32);
    cipher.setIV(iv, sizeof(iv));
    if (adSize)
        cipher.addAuthData(ad, adSize);
    if (size)
        cipher.encrypt(out, plain, size);
    cipher.computeTag(out + size, NOISE_TAG);
    cipher.clear();
}

static bool open(const uint8_t key[32], uint64_t counter, const uint8_t *ad, size_t adSize, const uint8_t *in, size_t size,
                 uint8_t *plain)
{
    if (size < NOISE_TAG)
        return false;
    const size_t plainSize = size - NOISE_TAG;
    uint8_t iv[12];
    nonceBytes(counter, iv);
    ChaChaPoly cipher;
    cipher.setKey(key, 32);
    cipher.setIV(iv, sizeof(iv));
    if (adSize)
        cipher.addAuthData(ad, adSize);
    if (plainSize)
        cipher.decrypt(plain, in, plainSize);
    const bool ok = cipher.checkTag(in + plainSize, NOISE_TAG);
    cipher.clear();
    if (!ok && plainSize)
        wipeBytes(plain, plainSize);
    return ok;
}

// X25519 with a clamped private key; an all-zero result (a low-order public key) fails.
static bool dh(const uint8_t privateKey[32], const uint8_t publicKey[32], uint8_t out[32])
{
    if (!Curve25519::eval(out, privateKey, publicKey))
        return false;
    uint8_t any = 0;
    for (int i = 0; i < 32; ++i)
        any |= out[i];
    return any != 0;
}

void NoiseHandshake::wipe()
{
    wipeBytes(this, sizeof(*this));
}

void NoiseHandshake::start(bool initiator, const uint8_t ephemeralPrivate[NOISE_KEY])
{
    wipe();
    isInitiator = initiator;
    std::memcpy(h, PROTOCOL_NAME, 32);
    std::memcpy(ck, h, 32);
    mixHash(nullptr, 0); // the empty prologue
    std::memcpy(e, ephemeralPrivate, NOISE_KEY);
    e[0] &= 0xf8;
    e[31] = (e[31] & 0x7f) | 0x40;
    Curve25519::eval(ePublic, e, nullptr);
}

void NoiseHandshake::mixHash(const uint8_t *data, size_t size)
{
    SHA256 sha;
    sha.update(h, 32);
    if (size)
        sha.update(data, size);
    sha.finalize(h, 32);
}

bool NoiseHandshake::mixKey(const uint8_t *privateKey, const uint8_t *publicKey)
{
    uint8_t shared[32];
    if (!dh(privateKey, publicKey, shared)) {
        wipeBytes(shared, sizeof(shared));
        return false;
    }
    uint8_t next[32];
    hkdf2(ck, shared, sizeof(shared), next, k);
    std::memcpy(ck, next, 32);
    wipeBytes(shared, sizeof(shared));
    wipeBytes(next, sizeof(next));
    n = 0;
    hasKey = true;
    return true;
}

bool NoiseHandshake::encryptAndHash(const uint8_t *plain, size_t size, uint8_t *out)
{
    if (!hasKey) {
        if (size)
            std::memmove(out, plain, size);
        mixHash(out, size);
        return true;
    }
    seal(k, n++, h, 32, plain, size, out);
    mixHash(out, size + NOISE_TAG);
    return true;
}

bool NoiseHandshake::decryptAndHash(const uint8_t *in, size_t size, uint8_t *plain)
{
    if (!hasKey) {
        if (size)
            std::memmove(plain, in, size);
        mixHash(in, size);
        return true;
    }
    if (!open(k, n, h, 32, in, size, plain))
        return false;
    ++n;
    mixHash(in, size);
    return true;
}

bool NoiseHandshake::writeMessage1(uint8_t out[NOISE_MESSAGE1])
{
    if (!isInitiator)
        return false;
    std::memcpy(out, ePublic, NOISE_KEY);
    mixHash(ePublic, NOISE_KEY);
    return encryptAndHash(nullptr, 0, out + NOISE_KEY); // the empty payload
}

bool NoiseHandshake::readMessage1(Bytes in)
{
    if (isInitiator || in.size != NOISE_MESSAGE1)
        return false;
    std::memcpy(re, in.data, NOISE_KEY);
    mixHash(re, NOISE_KEY);
    return decryptAndHash(nullptr, 0, nullptr);
}

bool NoiseHandshake::writeMessage2(const uint8_t staticPrivate[NOISE_KEY], const uint8_t staticPublic[NOISE_KEY],
                                   uint8_t out[NOISE_MESSAGE2])
{
    if (isInitiator)
        return false;
    std::memcpy(out, ePublic, NOISE_KEY);
    mixHash(ePublic, NOISE_KEY);
    return mixKey(e, re) && encryptAndHash(staticPublic, NOISE_KEY, out + 32) && mixKey(staticPrivate, re) &&
           encryptAndHash(nullptr, 0, out + 32 + NOISE_KEY + NOISE_TAG);
}

bool NoiseHandshake::readMessage2(Bytes in)
{
    if (!isInitiator || in.size != NOISE_MESSAGE2)
        return false;
    std::memcpy(re, in.data, NOISE_KEY);
    mixHash(re, NOISE_KEY);
    return mixKey(e, re) && decryptAndHash(in.data + 32, NOISE_KEY + NOISE_TAG, rs) && mixKey(e, rs) &&
           decryptAndHash(in.data + 32 + NOISE_KEY + NOISE_TAG, NOISE_TAG, nullptr);
}

bool NoiseHandshake::writeMessage3(const uint8_t staticPrivate[NOISE_KEY], const uint8_t staticPublic[NOISE_KEY],
                                   uint8_t out[NOISE_MESSAGE3])
{
    if (!isInitiator)
        return false;
    return encryptAndHash(staticPublic, NOISE_KEY, out) && mixKey(staticPrivate, re) &&
           encryptAndHash(nullptr, 0, out + NOISE_KEY + NOISE_TAG);
}

bool NoiseHandshake::readMessage3(Bytes in)
{
    if (isInitiator || in.size != NOISE_MESSAGE3)
        return false;
    return decryptAndHash(in.data, NOISE_KEY + NOISE_TAG, rs) && mixKey(e, rs) &&
           decryptAndHash(in.data + NOISE_KEY + NOISE_TAG, NOISE_TAG, nullptr);
}

void NoiseHandshake::split(NoiseSender &sender, NoiseReceiver &receiver)
{
    uint8_t first[32], second[32];
    hkdf2(ck, nullptr, 0, first, second);
    sender = {};
    receiver = {};
    // The initiator sends with the first key, the responder with the second.
    std::memcpy(sender.key, isInitiator ? first : second, 32);
    std::memcpy(receiver.key, isInitiator ? second : first, 32);
    wipeBytes(first, sizeof(first));
    wipeBytes(second, sizeof(second));
}

bool noiseEncrypt(NoiseSender &sender, Bytes plain, uint8_t *out)
{
    if (sender.nonce > LAST_NONCE)
        return false;
    const uint32_t nonce = sender.nonce++;
    write32(out, nonce);
    seal(sender.key, nonce, nullptr, 0, plain.data, plain.size, out + 4);
    return true;
}

bool noiseDecrypt(NoiseReceiver &receiver, Bytes in, uint8_t *plain)
{
    if (in.size < NOISE_TRANSPORT_OVERHEAD)
        return false;
    const uint32_t nonce = read32(in.data);
    if (receiver.any) {
        if (nonce <= receiver.highest) {
            const uint32_t behind = receiver.highest - nonce;
            if (behind >= WINDOW || (receiver.window >> behind) & 1)
                return false;
        }
    }
    if (!open(receiver.key, nonce, nullptr, 0, in.data + 4, in.size - 4, plain))
        return false;
    // Only an authentic message moves the window.
    if (!receiver.any) {
        receiver.any = true;
        receiver.highest = nonce;
        receiver.window = 1;
    } else if (nonce > receiver.highest) {
        const uint32_t ahead = nonce - receiver.highest;
        receiver.window = ahead >= WINDOW ? 1 : (receiver.window << ahead) | 1;
        receiver.highest = nonce;
    } else {
        receiver.window |= uint64_t(1) << (receiver.highest - nonce);
    }
    return true;
}

void noiseWipe(void *data, size_t size)
{
    wipeBytes(data, size);
}

void noisePeerId(const uint8_t staticPublic[NOISE_KEY], PeerId &out)
{
    uint8_t hash[32];
    SHA256 sha;
    sha.update(staticPublic, NOISE_KEY);
    sha.finalize(hash, sizeof(hash));
    std::memcpy(out.data(), hash, out.size());
}
} // namespace bitchat

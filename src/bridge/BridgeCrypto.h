#pragma once

#include "BridgeCodec.h"
#include <Ed25519.h>
#include <SHA256.h>
#include <algorithm>

namespace bitchat
{

class BridgeCrypto : public Crypto
{
  public:
    void hash(Bytes input, Digest &output) override
    {
        SHA256 sha;
        sha.update(input.data, input.size);
        sha.finalize(output.data(), output.size());
    }
    void mac(Bytes input, const uint8_t key[32], Digest &output) override
    {
        SHA256 sha;
        sha.resetHMAC(key, 32);
        sha.update(input.data, input.size);
        sha.finalizeHMAC(key, 32, output.data(), output.size());
    }
    bool verify(Bytes input, Bytes signature, const uint8_t publicKey[32]) override
    {
        return signature.size == 64 && canonicalScalar(signature.data + 32) && canonicalPoint(publicKey) &&
               canonicalPoint(signature.data) && Ed25519::verify(signature.data, publicKey, input.data, input.size);
    }

  private:
    static bool lessLittleEndian(const uint8_t *value, const uint8_t *limit)
    {
        for (size_t i = 32; i-- > 0;) {
            if (value[i] != limit[i])
                return value[i] < limit[i];
        }
        return false;
    }
    static bool canonicalScalar(const uint8_t *scalar)
    {
        static constexpr uint8_t ORDER[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                              0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                                              0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};
        return lessLittleEndian(scalar, ORDER);
    }
    static bool canonicalPoint(const uint8_t *encoded)
    {
        std::array<uint8_t, 32> y;
        std::copy_n(encoded, 32, y.begin());
        y[31] &= 0x7f;
        std::array<uint8_t, 32> prime;
        prime.fill(0xff);
        prime[0] = 0xed;
        prime[31] = 0x7f;
        if (!lessLittleEndian(y.data(), prime.data()))
            return false;
        --prime[0];
        if (y == prime)
            return false;
        bool zeroTail = true;
        for (size_t i = 1; i < y.size(); ++i)
            zeroTail &= y[i] == 0;
        if (zeroTail && y[0] <= 1)
            return false;
        // Order-eight encodings, as listed in libsodium's Ed25519 validation.
        static constexpr uint8_t SMALL[2][32] = {
            {0x26, 0xe8, 0x95, 0x8f, 0xc2, 0xb2, 0x27, 0xb0, 0x45, 0xc3, 0xf4, 0x89, 0xf2, 0xef, 0x98, 0xf0,
             0xd5, 0xdf, 0xac, 0x05, 0xd3, 0xc6, 0x33, 0x39, 0xb1, 0x38, 0x02, 0x88, 0x6d, 0x53, 0xfc, 0x05},
            {0xc7, 0x17, 0x6a, 0x70, 0x3d, 0x4d, 0xd8, 0x4f, 0xba, 0x3c, 0x0b, 0x76, 0x0d, 0x10, 0x67, 0x0f,
             0x2a, 0x20, 0x53, 0xfa, 0x2c, 0x39, 0xcc, 0xc6, 0x4e, 0xc7, 0xfd, 0x77, 0x92, 0xac, 0x03, 0x7a}};
        for (const auto &small : SMALL)
            if (std::equal(y.begin(), y.end(), small))
                return false;
        return true;
    }
};

} // namespace bitchat

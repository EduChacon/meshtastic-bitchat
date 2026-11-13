#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace bitchat
{

constexpr size_t MAX_FRAME = 2048;
constexpr size_t MAX_PAYLOAD = 1792;
constexpr size_t PEER_ID_SIZE = 8;
constexpr size_t MAX_ROUTE_HOPS = 16;
constexpr uint64_t FRESHNESS_MS = 120000;
using PeerId = std::array<uint8_t, PEER_ID_SIZE>;
using Digest = std::array<uint8_t, 32>;

enum class Result : uint8_t {
    Ok,
    Incomplete,
    Malformed,
    Oversize,
    Unsupported,
    Expired,
    Duplicate,
    Unauthenticated,
    Full,
    Policy,
    NoClock
};

struct Bytes {
    const uint8_t *data = nullptr;
    size_t size = 0;
};

struct Frame {
    std::array<uint8_t, MAX_FRAME> data{};
    size_t size = 0;
    Bytes bytes() const { return {data.data(), size}; }
};

struct Packet {
    uint8_t version = 0;
    uint8_t type = 0;
    uint8_t ttl = 0;
    uint8_t flags = 0;
    uint64_t timestamp = 0;
    PeerId sender{};
    PeerId recipient{};
    Bytes route{};
    Bytes payload{};
    Bytes signature{};
    size_t naturalSize = 0;
    bool directed() const { return (flags & 1) != 0; }
    bool signedPacket() const { return (flags & 2) != 0; }
    bool compressed() const { return (flags & 4) != 0; }
    bool replay() const { return (flags & 16) != 0; }
};

class Crypto
{
  public:
    virtual ~Crypto() = default;
    virtual void hash(Bytes input, Digest &output) = 0;
    virtual bool verify(Bytes input, Bytes signature, const uint8_t publicKey[32]) = 0;
    virtual void mac(Bytes input, const uint8_t key[32], Digest &output) = 0;
};

uint16_t read16(const uint8_t *p);
uint32_t read32(const uint8_t *p);
uint64_t read64(const uint8_t *p);
void write16(uint8_t *p, uint16_t v);
void write32(uint8_t *p, uint32_t v);
void write64(uint8_t *p, uint64_t v);
bool equalConstantTime(Bytes a, Bytes b);
bool validUtf8(Bytes text);
bool startsWith(Bytes text, const char *prefix);
bool fresh(uint64_t timestamp, uint64_t now);
size_t paddedSize(size_t naturalSize);
Result decode(Bytes input, Packet &packet);
Result wireFrameLength(Bytes prefix, size_t &length);
Result signingBytes(Bytes input, Frame &output);
Result packetDigest(Crypto &crypto, Bytes input, Digest &output);
Result encode(uint8_t type, const PeerId &sender, const PeerId *recipient, uint64_t timestamp, uint8_t ttl, Bytes payload,
              Bytes signature, Frame &output, bool pad = false);
Result relay(Bytes input, Frame &output);

struct Announcement {
    std::array<char, 33> nickname{};
    std::array<uint8_t, 32> noiseKey{};
    std::array<uint8_t, 32> signingKey{};
};

Result decodeAnnouncement(Bytes payload, Announcement &output);

} // namespace bitchat

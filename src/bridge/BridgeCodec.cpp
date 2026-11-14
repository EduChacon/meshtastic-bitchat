#include "BridgeCodec.h"
#include <algorithm>
#include <cstring>

namespace bitchat
{

Result wireFrameLength(Bytes input, size_t &length)
{
    length = 0;
    if (input.size < 14)
        return Result::Incomplete;
    const bool v2 = input.data[0] == 2;
    if ((!v2 && input.data[0] != 1) || (input.data[11] & ~31) || input.data[2] > 7)
        return Result::Malformed;
    const size_t header = v2 ? 16 : 14;
    if (input.size < header)
        return Result::Incomplete;
    const size_t payload = v2 ? read32(input.data + 12) : read16(input.data + 12);
    length = header + 8 + ((input.data[11] & 1) ? 8 : 0);
    if (input.data[11] & 8) {
        if (!v2)
            return Result::Malformed;
        if (input.size <= length)
            return Result::Incomplete;
        if (input.data[length] > MAX_ROUTE_HOPS)
            return Result::Oversize;
        length += 1 + 8 * input.data[length];
    }
    if (payload > MAX_PAYLOAD)
        return Result::Oversize;
    length += payload + ((input.data[11] & 2) ? 64 : 0);
    if (input.data[1] == 0x10 || input.data[1] == 0x11)
        length = paddedSize(length);
    return length > MAX_FRAME ? Result::Oversize : Result::Ok;
}

uint16_t read16(const uint8_t *p)
{
    return (uint16_t(p[0]) << 8) | p[1];
}
uint32_t read32(const uint8_t *p)
{
    return (uint32_t(read16(p)) << 16) | read16(p + 2);
}
uint64_t read64(const uint8_t *p)
{
    return (uint64_t(read32(p)) << 32) | read32(p + 4);
}
void write16(uint8_t *p, uint16_t v)
{
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}
void write32(uint8_t *p, uint32_t v)
{
    write16(p, uint16_t(v >> 16));
    write16(p + 2, uint16_t(v));
}
void write64(uint8_t *p, uint64_t v)
{
    write32(p, uint32_t(v >> 32));
    write32(p + 4, uint32_t(v));
}

bool equalConstantTime(Bytes a, Bytes b)
{
    if (a.size != b.size)
        return false;
    uint8_t difference = 0;
    for (size_t i = 0; i < a.size; ++i)
        difference |= a.data[i] ^ b.data[i];
    return difference == 0;
}

bool validUtf8(Bytes text)
{
    size_t i = 0;
    while (i < text.size) {
        uint32_t c = text.data[i++];
        if (c < 0x80) {
            if (c == 0)
                return false;
            continue;
        }
        size_t extra;
        uint32_t minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            extra = 1;
            minimum = 0x80;
            c &= 0x1f;
        } else if (c >= 0xe0 && c <= 0xef) {
            extra = 2;
            minimum = 0x800;
            c &= 0x0f;
        } else if (c >= 0xf0 && c <= 0xf4) {
            extra = 3;
            minimum = 0x10000;
            c &= 7;
        } else {
            return false;
        }
        if (extra > text.size - i)
            return false;
        while (extra--) {
            uint8_t tail = text.data[i++];
            if ((tail & 0xc0) != 0x80)
                return false;
            c = (c << 6) | (tail & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            return false;
    }
    return true;
}

bool startsWith(Bytes text, const char *prefix)
{
    const size_t length = std::strlen(prefix);
    return text.size >= length && std::memcmp(text.data, prefix, length) == 0;
}

bool fresh(uint64_t timestamp, uint64_t now)
{
    if (now == 0 || timestamp == 0)
        return false;
    return timestamp > now ? timestamp - now <= FRESHNESS_MS : now - timestamp <= FRESHNESS_MS;
}

size_t paddedSize(size_t size)
{
    for (size_t bucket : {256U, 512U, 1024U, 2048U}) {
        if (size <= bucket && bucket - size >= 16)
            return bucket - size <= 255 ? bucket : size;
    }
    return size;
}

Result decode(Bytes input, Packet &p)
{
    p = {};
    if (input.size > MAX_FRAME)
        return Result::Oversize;
    if (!input.data || input.size < 14)
        return Result::Incomplete;
    const auto *b = input.data;
    if (b[0] != 1 && b[0] != 2)
        return Result::Unsupported;
    const size_t header = b[0] == 1 ? 14 : 16;
    if (input.size < header + 8)
        return Result::Incomplete;
    if ((b[11] & 0xe0) || (b[0] == 1 && (b[11] & 8)))
        return Result::Unsupported;
    p.version = b[0];
    p.type = b[1];
    p.ttl = b[2];
    p.timestamp = read64(b + 3);
    p.flags = b[11];
    const size_t payloadSize = p.version == 1 ? read16(b + 12) : read32(b + 12);
    if (payloadSize > MAX_PAYLOAD || p.ttl > 7)
        return Result::Oversize;
    size_t offset = header;
    std::copy_n(b + offset, 8, p.sender.begin());
    offset += 8;
    if (p.directed()) {
        if (input.size - offset < 8)
            return Result::Incomplete;
        std::copy_n(b + offset, 8, p.recipient.begin());
        offset += 8;
    }
    if (p.flags & 8) {
        if (offset >= input.size)
            return Result::Incomplete;
        const size_t count = b[offset];
        if (count == 0 || count > MAX_ROUTE_HOPS)
            return Result::Oversize;
        const size_t routeSize = 1 + count * 8;
        if (routeSize > input.size - offset)
            return Result::Incomplete;
        p.route = {b + offset, routeSize};
        offset += routeSize;
    }
    if (payloadSize > input.size - offset)
        return Result::Incomplete;
    p.payload = {b + offset, payloadSize};
    if (p.compressed()) {
        const size_t prefix = p.version == 1 ? 2 : 4;
        if (payloadSize <= prefix)
            return Result::Malformed;
        const size_t expanded = p.version == 1 ? read16(p.payload.data) : read32(p.payload.data);
        if (!expanded || expanded > MAX_PAYLOAD)
            return Result::Oversize;
    }
    offset += payloadSize;
    if (p.signedPacket()) {
        if (input.size - offset < 64)
            return Result::Incomplete;
        p.signature = {b + offset, 64};
        offset += 64;
    }
    p.naturalSize = offset;
    const size_t padding = input.size - offset;
    if (padding > 255)
        return Result::Malformed;
    for (size_t i = offset; i < input.size; ++i)
        if (b[i] != padding)
            return Result::Malformed;
    return Result::Ok;
}

Result signingBytes(Bytes input, Frame &output)
{
    Packet p;
    const Result result = decode(input, p);
    if (result != Result::Ok)
        return result;
    const size_t unsignedSize = p.naturalSize - p.signature.size;
    const size_t size = paddedSize(unsignedSize);
    if (size > output.data.size())
        return Result::Oversize;
    std::memmove(output.data.data(), input.data, unsignedSize);
    output.data[2] = 0;
    output.data[11] &= uint8_t(~0x12);
    std::fill(output.data.begin() + unsignedSize, output.data.begin() + size, uint8_t(size - unsignedSize));
    output.size = size;
    return Result::Ok;
}

Result packetDigest(Crypto &crypto, Bytes input, Digest &output)
{
    Frame canonical;
    const Result result = signingBytes(input, canonical);
    if (result == Result::Ok)
        crypto.hash(canonical.bytes(), output);
    return result;
}

Result encode(uint8_t type, const PeerId &sender, const PeerId *recipient, uint64_t timestamp, uint8_t ttl, Bytes payload,
              Bytes signature, Frame &output, bool pad)
{
    if (payload.size > MAX_PAYLOAD || (signature.size != 0 && signature.size != 64) || ttl > 7)
        return Result::Oversize;
    const size_t natural = 22 + (recipient ? 8 : 0) + payload.size + signature.size;
    const size_t size = pad ? paddedSize(natural) : natural;
    if (size > MAX_FRAME)
        return Result::Oversize;
    auto *b = output.data.data();
    b[0] = 1;
    b[1] = type;
    b[2] = ttl;
    write64(b + 3, timestamp);
    b[11] = (recipient ? 1 : 0) | (signature.size ? 2 : 0);
    write16(b + 12, uint16_t(payload.size));
    std::copy(sender.begin(), sender.end(), b + 14);
    size_t offset = 22;
    if (recipient) {
        std::copy(recipient->begin(), recipient->end(), b + offset);
        offset += 8;
    }
    if (payload.size)
        std::memcpy(b + offset, payload.data, payload.size);
    offset += payload.size;
    if (signature.size)
        std::memcpy(b + offset, signature.data, signature.size);
    std::fill(b + natural, b + size, uint8_t(size - natural));
    output.size = size;
    return Result::Ok;
}

Result relay(Bytes input, Frame &output)
{
    Packet p;
    const Result result = decode(input, p);
    if (result != Result::Ok)
        return result;
    if (p.type == 0x21 || p.ttl <= 1 || p.replay())
        return Result::Policy;
    std::memmove(output.data.data(), input.data, input.size);
    output.size = input.size;
    --output.data[2];
    return Result::Ok;
}

Result decodeAnnouncement(Bytes payload, Announcement &out)
{
    out = {};
    uint8_t seen = 0;
    size_t offset = 0;
    while (offset < payload.size) {
        if (payload.size - offset < 2)
            return Result::Malformed;
        const uint8_t type = payload.data[offset++];
        const size_t length = payload.data[offset++];
        if (length > payload.size - offset)
            return Result::Malformed;
        const auto *value = payload.data + offset;
        if (type >= 1 && type <= 3) {
            const uint8_t flag = uint8_t(1U << (type - 1));
            if (seen & flag)
                return Result::Malformed;
            seen |= flag;
            if (type == 1) {
                if (!validUtf8({value, length}))
                    return Result::Malformed;
                size_t clipped = std::min(length, out.nickname.size() - 1);
                while (clipped && !validUtf8({value, clipped}))
                    --clipped;
                std::memcpy(out.nickname.data(), value, clipped);
            } else {
                if (length != 32)
                    return Result::Malformed;
                auto &key = type == 2 ? out.noiseKey : out.signingKey;
                std::copy_n(value, 32, key.begin());
            }
        } else if (type == GATEWAY_TLV) {
            out.gateway = true;
        }
        offset += length;
    }
    return seen == 7 ? Result::Ok : Result::Malformed;
}

} // namespace bitchat

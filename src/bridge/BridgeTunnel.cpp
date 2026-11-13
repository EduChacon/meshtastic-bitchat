#include "BridgeTunnel.h"
#include "mesh/Throttle.h"
#include <algorithm>
#include <cstring>

namespace bitchat
{

Result encodeTunnelFragment(Crypto &crypto, Bytes frame, const TunnelIdentity &identity, const uint8_t key[32], uint8_t index,
                            uint8_t *out, size_t capacity, size_t &length)
{
    length = 0;
    if (frame.size < 22 || frame.size > MAX_FRAME)
        return Result::Oversize;
    const size_t count = (frame.size + TUNNEL_CHUNK - 1) / TUNNEL_CHUNK;
    if (index >= count || count > TUNNEL_MAX_FRAGMENTS)
        return Result::Malformed;
    const size_t offset = size_t(index) * TUNNEL_CHUNK;
    const size_t size = std::min(TUNNEL_CHUNK, frame.size - offset);
    if (capacity < TUNNEL_HEADER + size + TUNNEL_TAG)
        return Result::Oversize;
    out[0] = 'B';
    out[1] = 'T';
    out[2] = 1;
    out[3] = 0;
    std::copy(identity.domain.begin(), identity.domain.end(), out + 4);
    write32(out + 12, identity.origin);
    write32(out + 16, identity.boot);
    write32(out + 20, identity.sequence);
    Digest digest;
    crypto.hash(frame, digest);
    std::copy_n(digest.begin(), 16, out + 24);
    out[40] = index;
    out[41] = uint8_t(count);
    write16(out + 42, uint16_t(frame.size));
    out[44] = 1;
    out[45] = 0;
    std::memcpy(out + TUNNEL_HEADER, frame.data + offset, size);
    crypto.mac({out, TUNNEL_HEADER + size}, key, digest);
    std::copy_n(digest.begin(), TUNNEL_TAG, out + TUNNEL_HEADER + size);
    length = TUNNEL_HEADER + size + TUNNEL_TAG;
    return Result::Ok;
}

Result decodeTunnelFragment(Crypto &crypto, Bytes wire, const TunnelDomain &domain, const uint8_t key[32], TunnelFragment &out)
{
    out = {};
    if (wire.size <= TUNNEL_HEADER + TUNNEL_TAG || wire.size > TUNNEL_MAX_FRAGMENT)
        return Result::Oversize;
    const auto *b = wire.data;
    if (b[0] != 'B' || b[1] != 'T' || b[2] != 1 || b[3] != 0 || b[44] != 1 || b[45] != 0)
        return Result::Unsupported;
    if (!equalConstantTime({b + 4, 8}, {domain.data(), domain.size()}))
        return Result::Policy;
    const size_t total = read16(b + 42);
    const size_t count = (total + TUNNEL_CHUNK - 1) / TUNNEL_CHUNK;
    const size_t index = b[40];
    if (total < 22 || total > MAX_FRAME || count != b[41] || count > TUNNEL_MAX_FRAGMENTS || index >= count)
        return Result::Malformed;
    const size_t size = std::min(TUNNEL_CHUNK, total - index * TUNNEL_CHUNK);
    if (wire.size != TUNNEL_HEADER + size + TUNNEL_TAG)
        return Result::Malformed;
    Digest mac;
    crypto.mac({b, wire.size - TUNNEL_TAG}, key, mac);
    if (!equalConstantTime({b + wire.size - TUNNEL_TAG, TUNNEL_TAG}, {mac.data(), TUNNEL_TAG}))
        return Result::Unauthenticated;
    out.identity.domain = domain;
    out.identity.origin = read32(b + 12);
    out.identity.boot = read32(b + 16);
    out.identity.sequence = read32(b + 20);
    std::copy_n(b + 24, 16, out.packetId.begin());
    out.total = uint16_t(total);
    out.index = uint8_t(index);
    out.count = uint8_t(count);
    out.payload = {b + TUNNEL_HEADER, size};
    return Result::Ok;
}

size_t TunnelReassembly::expire(uint32_t now)
{
    size_t expired = 0;
    for (auto &entry : entries)
        if (entry.used && !Throttle::isWithinTimespanMsAt(entry.started, 45000, now)) {
            entry.used = false;
            ++expired;
        }
    return expired;
}

Result TunnelReassembly::accept(Crypto &crypto, const TunnelFragment &p, uint32_t now, Frame &output)
{
    const size_t count = (p.total + TUNNEL_CHUNK - 1) / TUNNEL_CHUNK;
    if (p.total < 22 || p.total > MAX_FRAME || count != p.count || p.index >= count || count > TUNNEL_MAX_FRAGMENTS)
        return Result::Malformed;
    const size_t offset = size_t(p.index) * TUNNEL_CHUNK;
    if (p.payload.size != std::min(TUNNEL_CHUNK, size_t(p.total) - offset))
        return Result::Malformed;
    expire(now);
    Assembly *assembly = nullptr;
    for (auto &entry : entries)
        if (entry.used && entry.identity.domain == p.identity.domain && entry.identity.origin == p.identity.origin &&
            entry.identity.boot == p.identity.boot && entry.identity.sequence == p.identity.sequence) {
            assembly = &entry;
            break;
        }
    if (!assembly) {
        for (auto &entry : entries)
            if (!entry.used) {
                entry = {};
                entry.used = true;
                entry.identity = p.identity;
                entry.packetId = p.packetId;
                entry.total = p.total;
                entry.started = now;
                assembly = &entry;
                break;
            }
    }
    if (!assembly)
        return Result::Full;
    auto &a = *assembly;
    if (a.total != p.total || a.packetId != p.packetId) {
        a.used = false;
        return Result::Malformed;
    }
    const uint16_t bit = uint16_t(1U << p.index);
    if (a.received & bit) {
        if (std::memcmp(a.frame.data.data() + offset, p.payload.data, p.payload.size) != 0) {
            a.used = false;
            return Result::Malformed;
        }
        return Result::Duplicate;
    }
    std::memcpy(a.frame.data.data() + offset, p.payload.data, p.payload.size);
    a.received |= bit;
    if (a.received != uint16_t((1U << count) - 1))
        return Result::Incomplete;
    a.frame.size = a.total;
    Digest digest;
    crypto.hash(a.frame.bytes(), digest);
    a.used = false;
    if (!equalConstantTime({digest.data(), 16}, {a.packetId.data(), a.packetId.size()}))
        return Result::Unauthenticated;
    output = a.frame;
    return Result::Ok;
}

} // namespace bitchat

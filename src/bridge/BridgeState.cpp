#include "BridgeState.h"

namespace bitchat
{

Peer *PeerTable::find(const PeerId &id)
{
    for (auto &entry : entries)
        if (entry.used && entry.id == id)
            return &entry;
    return nullptr;
}

size_t PeerTable::size() const
{
    size_t count = 0;
    for (const auto &entry : entries)
        count += entry.used;
    return count;
}

bool PeerTable::nicknameShared(const Peer &peer) const
{
    for (const auto &entry : entries)
        if (entry.used && &entry != &peer && entry.announcement.nickname == peer.announcement.nickname)
            return true;
    return false;
}

Result PeerTable::announce(Crypto &crypto, Bytes frame, uint64_t nowUtc, uint32_t now, Peer *&peer)
{
    peer = nullptr;
    Packet p;
    Result result = decode(frame, p);
    if (result != Result::Ok)
        return result;
    if (nowUtc == 0)
        return Result::NoClock;
    if (!fresh(p.timestamp, nowUtc))
        return Result::Expired;
    if (p.type != 1 || p.directed() || p.compressed() || p.replay() || !p.signedPacket())
        return Result::Policy;
    Announcement announcement;
    result = decodeAnnouncement(p.payload, announcement);
    if (result != Result::Ok)
        return result;
    Digest identity;
    crypto.hash({announcement.noiseKey.data(), announcement.noiseKey.size()}, identity);
    if (!std::equal(p.sender.begin(), p.sender.end(), identity.begin()))
        return Result::Unauthenticated;
    Peer *existing = find(p.sender);
    if (existing && (existing->announcement.signingKey != announcement.signingKey ||
                     existing->announcement.noiseKey != announcement.noiseKey))
        return Result::Unauthenticated;
    Frame canonical;
    result = signingBytes(frame, canonical);
    if (result != Result::Ok || !crypto.verify(canonical.bytes(), p.signature, announcement.signingKey.data()))
        return Result::Unauthenticated;
    if (!existing) {
        for (auto &entry : entries)
            if (!entry.used) {
                existing = &entry;
                break;
            }
    }
    if (!existing) {
        // Phones announce every few seconds: only a peer silent for minutes gives up its slot.
        uint32_t oldest = STALE_MS - 1;
        for (auto &entry : entries)
            if (now - entry.seenAt > oldest) {
                oldest = now - entry.seenAt;
                existing = &entry;
            }
        if (existing)
            *existing = {};
    }
    if (!existing)
        return Result::Full;
    existing->used = true;
    existing->id = p.sender;
    existing->announcement = announcement;
    existing->seenAt = now;
    peer = existing;
    return Result::Ok;
}

Result PeerTable::authenticate(Crypto &crypto, Bytes frame, uint64_t nowUtc, uint32_t now, Peer *&peer)
{
    peer = nullptr;
    Packet p;
    const Result result = decode(frame, p);
    if (result != Result::Ok)
        return result;
    if (p.type == 1)
        return announce(crypto, frame, nowUtc, now, peer);
    if (nowUtc == 0)
        return Result::NoClock;
    if (!fresh(p.timestamp, nowUtc))
        return Result::Expired;
    if (p.replay())
        return Result::Policy;
    peer = find(p.sender);
    if (!peer)
        return Result::Unauthenticated;
    if (p.signedPacket()) {
        Frame canonical;
        if (signingBytes(frame, canonical) != Result::Ok ||
            !crypto.verify(canonical.bytes(), p.signature, peer->announcement.signingKey.data()))
            return Result::Unauthenticated;
    } else if (p.type != 0x10 && p.type != 0x11 && p.type != 0x04 && p.type != 0x25) {
        return Result::Unauthenticated;
    }
    // Unsigned types cannot prove their sender, so only signed traffic keeps a slot alive.
    if (p.signedPacket())
        peer->seenAt = now;
    return Result::Ok;
}

void BleReassembly::expire(uint32_t now)
{
    for (auto &entry : entries)
        if (entry.used && !Throttle::isWithinTimespanMsAt(entry.started, 30000, now))
            entry.used = false;
}

Result BleReassembly::accept(const Packet &p, uint32_t now, Frame &output)
{
    if (p.type != 0x20 || p.compressed() || p.replay() || p.payload.size <= 13 || p.ttl == 0)
        return Result::Policy;
    const auto *header = p.payload.data;
    const size_t index = read16(header + 8);
    const size_t count = read16(header + 10);
    const size_t length = p.payload.size - 13;
    if (!count || count > MAX_FRAGMENTS || index >= count || length > MAX_FRAME || header[12] == 0x20 || header[12] == 0x21)
        return Result::Oversize;
    PeerId id;
    std::copy_n(header, 8, id.begin());
    expire(now);
    Assembly *assembly = nullptr;
    for (auto &entry : entries)
        if (entry.used && entry.sender == p.sender && entry.id == id) {
            assembly = &entry;
            break;
        }
    if (!assembly) {
        for (auto &entry : entries)
            if (!entry.used) {
                entry = {};
                entry.used = true;
                entry.sender = p.sender;
                entry.id = id;
                entry.timestamp = p.timestamp;
                entry.started = now;
                entry.count = uint16_t(count);
                entry.originalType = header[12];
                entry.minimumTtl = p.ttl;
                assembly = &entry;
                break;
            }
    }
    if (!assembly)
        return Result::Full;
    auto &a = *assembly;
    if (a.count != count || a.originalType != header[12] || a.timestamp != p.timestamp) {
        a.used = false;
        return Result::Malformed;
    }
    a.minimumTtl = std::min(a.minimumTtl, p.ttl);
    if (a.sizes[index]) {
        if (a.sizes[index] != length || std::memcmp(a.bytes.data() + a.offsets[index], header + 13, length) != 0) {
            a.used = false;
            return Result::Malformed;
        }
        return Result::Duplicate;
    }
    if (length > a.bytes.size() - a.occupied) {
        a.used = false;
        return Result::Oversize;
    }
    a.offsets[index] = uint16_t(a.occupied);
    a.sizes[index] = uint16_t(length);
    std::memcpy(a.bytes.data() + a.occupied, header + 13, length);
    a.occupied += length;
    for (size_t i = 0; i < count; ++i)
        if (a.sizes[i] == 0)
            return Result::Incomplete;
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        std::memcpy(output.data.data() + offset, a.bytes.data() + a.offsets[i], a.sizes[i]);
        offset += a.sizes[i];
    }
    output.size = offset;
    a.used = false;
    Packet original;
    const Result result = decode(output.bytes(), original);
    if (result != Result::Ok)
        return result;
    if (original.sender != p.sender || original.type != a.originalType || original.timestamp != p.timestamp)
        return Result::Malformed;
    output.data[2] = std::min(original.ttl, a.minimumTtl);
    return Result::Ok;
}

} // namespace bitchat

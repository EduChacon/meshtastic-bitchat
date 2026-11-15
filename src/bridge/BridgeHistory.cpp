#include "BridgeHistory.h"

#include <algorithm>
#include <cstring>

namespace bitchat
{

Result decodeSyncRequest(Bytes payload, SyncRequest &out)
{
    out = {};
    bool hasP = false, hasM = false, hasFilter = false;
    size_t offset = 0;
    while (payload.size - offset >= 3) {
        const uint8_t type = payload.data[offset];
        const size_t length = read16(payload.data + offset + 1);
        offset += 3;
        if (length > payload.size - offset)
            return Result::Malformed;
        const uint8_t *value = payload.data + offset;
        offset += length;
        if (type == 1 && length == 1) {
            out.p = value[0];
            hasP = true;
        } else if (type == 2 && length == 4) {
            out.m = read32(value);
            hasM = true;
        } else if (type == 3) {
            if (length > SYNC_FILTER_LIMIT)
                return Result::Oversize;
            out.filter = {value, length};
            hasFilter = true;
        } else if (type == 4 && length >= 1 && length <= 8) {
            // little-endian bit field; bit 1 is the public message
            out.messages = (value[0] & 2) != 0;
        } else if (type == 5 && length == 8) {
            out.since = read64(value);
        }
    }
    if (!hasP || !hasM || !hasFilter || out.p < 1 || out.p > 32 || out.m == 0)
        return Result::Malformed;
    return Result::Ok;
}

void syncId(Crypto &crypto, const Packet &packet, SyncId &out)
{
    std::array<uint8_t, 1 + PEER_ID_SIZE + 8 + HISTORY_FRAME> material{};
    const size_t size = std::min(packet.payload.size, size_t(HISTORY_FRAME));
    material[0] = packet.type;
    std::copy(packet.sender.begin(), packet.sender.end(), material.begin() + 1);
    write64(material.data() + 1 + PEER_ID_SIZE, packet.timestamp);
    std::memcpy(material.data() + 1 + PEER_ID_SIZE + 8, packet.payload.data, size);
    Digest digest;
    crypto.hash({material.data(), 1 + PEER_ID_SIZE + 8 + size}, digest);
    std::copy_n(digest.begin(), out.size(), out.begin());
}

uint64_t syncBucket(Crypto &crypto, const SyncId &id, uint32_t m)
{
    if (m <= 1)
        return 0;
    Digest digest;
    crypto.hash({id.data(), id.size()}, digest);
    const uint64_t value = (read64(digest.data()) & 0x7fffffffffffffffULL) % m;
    return value ? value : 1;
}

bool syncFilterHolds(uint8_t p, uint32_t m, Bytes filter, uint64_t bucket)
{
    // An unusable filter means the phone holds nothing, as the app treats it.
    if (p < 1 || p > 32 || m <= 1)
        return false;
    size_t bit = 0;
    const size_t bits = filter.size * 8;
    const auto read = [&](bool &value) {
        if (bit >= bits)
            return false;
        value = (filter.data[bit / 8] >> (7 - bit % 8)) & 1;
        ++bit;
        return true;
    };
    uint64_t position = 0;
    for (;;) {
        uint64_t quotient = 0, remainder = 0;
        bool one = false;
        for (;;) {
            if (!read(one))
                return false;
            if (!one)
                break;
            ++quotient;
        }
        for (uint8_t i = 0; i < p; ++i) {
            if (!read(one))
                return false;
            remainder = (remainder << 1) | (one ? 1 : 0);
        }
        position += (quotient << p) + remainder + 1;
        if (position >= m || position > bucket)
            return false;
        if (position == bucket)
            return true;
    }
}

bool History::current(const Entry &entry, uint64_t now)
{
    // A message dated ahead of the clock is never offered; one older than the window has expired.
    return entry.size && entry.timestamp <= now + FRESHNESS_MS && now - std::min(now, entry.timestamp) <= HISTORY_MS;
}

bool History::keep(Crypto &crypto, Bytes frame)
{
    Packet packet;
    if (!entries.size() || frame.size > HISTORY_FRAME || decode(frame, packet) != Result::Ok || packet.type != 2 ||
        packet.directed() || !packet.signedPacket() || packet.compressed() || packet.replay())
        return false;
    newest = used ? (newest + 1) % entries.size() : 0;
    used = std::min(used + 1, entries.size());
    auto &entry = entries[newest];
    std::memcpy(entry.frame.data(), frame.data, frame.size);
    entry.size = uint16_t(frame.size);
    entry.timestamp = packet.timestamp;
    syncId(crypto, packet, entry.id);
    return true;
}

size_t History::count(uint64_t now) const
{
    size_t total = 0;
    for (const auto &entry : entries)
        total += current(entry, now);
    return total;
}

bool History::next(Crypto &crypto, const SyncRequest &request, uint64_t now, size_t &cursor, Frame &out) const
{
    if (!request.messages)
        return false;
    while (cursor < used) {
        const auto &entry = entries[(newest + entries.size() - cursor) % entries.size()];
        ++cursor;
        if (!current(entry, now) || entry.timestamp < request.since ||
            syncFilterHolds(request.p, request.m, request.filter, syncBucket(crypto, entry.id, request.m)))
            continue;
        std::memcpy(out.data.data(), entry.frame.data(), entry.size);
        out.size = entry.size;
        out.data[2] = 0;    // time to live: for the asking phone only
        out.data[11] |= 16; // answer to a request; neither field is covered by the signature
        return true;
    }
    return false;
}

} // namespace bitchat

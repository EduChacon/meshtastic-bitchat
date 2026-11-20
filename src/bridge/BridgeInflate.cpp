#include "BridgeInflate.h"

#include <algorithm>
#include <cstring>

namespace bitchat
{
namespace
{

struct Huffman {
    uint16_t counts[16];
    uint16_t symbols[288];
};

struct Reader {
    const uint8_t *data;
    size_t size;
    size_t at = 0;
    uint32_t bits = 0;
    unsigned held = 0;
    bool bad = false;

    unsigned take(unsigned count)
    {
        while (held < count) {
            if (at >= size) {
                bad = true;
                return 0;
            }
            bits |= uint32_t(data[at++]) << held;
            held += 8;
        }
        const unsigned value = bits & ((1u << count) - 1);
        bits >>= count;
        held -= count;
        return value;
    }
};

// Canonical code from code lengths; false for an over-subscribed set. An incomplete set is allowed, as
// zlib allows it for a single distance code.
bool build(Huffman &h, const uint8_t *lengths, unsigned count)
{
    std::fill(std::begin(h.counts), std::end(h.counts), 0);
    for (unsigned i = 0; i < count; ++i)
        ++h.counts[lengths[i]];
    h.counts[0] = 0;
    int left = 1;
    for (unsigned len = 1; len < 16; ++len) {
        left <<= 1;
        left -= h.counts[len];
        if (left < 0)
            return false;
    }
    uint16_t offsets[16];
    offsets[1] = 0;
    for (unsigned len = 1; len < 15; ++len)
        offsets[len + 1] = offsets[len] + h.counts[len];
    for (unsigned i = 0; i < count; ++i)
        if (lengths[i])
            h.symbols[offsets[lengths[i]]++] = uint16_t(i);
    return true;
}

int decodeSymbol(Reader &r, const Huffman &h)
{
    int code = 0, first = 0, index = 0;
    for (unsigned len = 1; len < 16; ++len) {
        code |= int(r.take(1));
        if (r.bad)
            return -1;
        const int count = h.counts[len];
        if (code - count < first)
            return h.symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const uint16_t LENGTH_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t LENGTH_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t DISTANCE_BASE[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t DISTANCE_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Reader &r, const Huffman &lit, const Huffman &dist, uint8_t *out, size_t capacity, size_t &produced)
{
    for (;;) {
        int symbol = decodeSymbol(r, lit);
        if (symbol < 0)
            return false;
        if (symbol < 256) {
            if (produced >= capacity)
                return false;
            out[produced++] = uint8_t(symbol);
            continue;
        }
        if (symbol == 256)
            return true;
        symbol -= 257;
        if (symbol >= 29)
            return false;
        const size_t length = LENGTH_BASE[symbol] + r.take(LENGTH_EXTRA[symbol]);
        const int d = decodeSymbol(r, dist);
        if (d < 0 || d >= 30 || r.bad)
            return false;
        const size_t distance = DISTANCE_BASE[d] + r.take(DISTANCE_EXTRA[d]);
        if (r.bad || distance > produced || length > capacity - produced)
            return false;
        for (size_t i = 0; i < length; ++i, ++produced)
            out[produced] = out[produced - distance];
    }
}

bool fixedTables(Huffman &lit, Huffman &dist)
{
    uint8_t lengths[288];
    std::fill(lengths, lengths + 144, 8);
    std::fill(lengths + 144, lengths + 256, 9);
    std::fill(lengths + 256, lengths + 280, 7);
    std::fill(lengths + 280, lengths + 288, 8);
    if (!build(lit, lengths, 288))
        return false;
    std::fill(lengths, lengths + 30, 5);
    return build(dist, lengths, 30);
}

bool dynamicTables(Reader &r, Huffman &lit, Huffman &dist)
{
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const unsigned nlen = r.take(5) + 257, ndist = r.take(5) + 1, ncode = r.take(4) + 4;
    if (r.bad || nlen > 286 || ndist > 30)
        return false;
    uint8_t lengths[320] = {};
    for (unsigned i = 0; i < ncode; ++i)
        lengths[ORDER[i]] = uint8_t(r.take(3));
    if (r.bad)
        return false;
    Huffman lencode;
    if (!build(lencode, lengths, 19))
        return false;
    std::fill(lengths, lengths + 19, 0);
    unsigned index = 0;
    while (index < nlen + ndist) {
        const int symbol = decodeSymbol(r, lencode);
        if (symbol < 0)
            return false;
        if (symbol < 16) {
            lengths[index++] = uint8_t(symbol);
            continue;
        }
        uint8_t value = 0;
        unsigned repeat;
        if (symbol == 16) {
            if (!index)
                return false;
            value = lengths[index - 1];
            repeat = 3 + r.take(2);
        } else if (symbol == 17) {
            repeat = 3 + r.take(3);
        } else {
            repeat = 11 + r.take(7);
        }
        if (r.bad || index + repeat > nlen + ndist)
            return false;
        while (repeat--)
            lengths[index++] = value;
    }
    // A block must be able to end.
    if (!lengths[256])
        return false;
    return build(lit, lengths, nlen) && build(dist, lengths + nlen, ndist);
}

} // namespace

bool inflateRaw(Bytes input, uint8_t *out, size_t capacity, size_t &produced)
{
    produced = 0;
    if (!input.data || !out)
        return false;
    Reader r{input.data, input.size};
    Huffman lit, dist;
    unsigned last = 0;
    do {
        last = r.take(1);
        const unsigned type = r.take(2);
        if (r.bad)
            return false;
        if (type == 0) {
            r.bits = 0;
            r.held = 0;
            if (input.size - r.at < 4)
                return false;
            const size_t length = input.data[r.at] | (input.data[r.at + 1] << 8);
            const size_t inverse = input.data[r.at + 2] | (input.data[r.at + 3] << 8);
            r.at += 4;
            if (length != (~inverse & 0xffff) || length > input.size - r.at || length > capacity - produced)
                return false;
            memcpy(out + produced, input.data + r.at, length);
            r.at += length;
            produced += length;
        } else if (type == 1) {
            if (!fixedTables(lit, dist) || !codes(r, lit, dist, out, capacity, produced))
                return false;
        } else if (type == 2) {
            if (!dynamicTables(r, lit, dist) || !codes(r, lit, dist, out, capacity, produced))
                return false;
        } else {
            return false;
        }
    } while (!last);
    return true;
}

bool messageText(const Packet &packet, uint8_t *out, size_t capacity, Bytes &text)
{
    text = {};
    if (!packet.compressed()) {
        text = packet.payload;
        return true;
    }
    const size_t prefix = packet.version == 1 ? 2 : 4;
    if (packet.payload.size <= prefix)
        return false;
    const size_t expected = packet.version == 1 ? read16(packet.payload.data) : read32(packet.payload.data);
    size_t produced = 0;
    if (expected > capacity ||
        !inflateRaw({packet.payload.data + prefix, packet.payload.size - prefix}, out, expected, produced) ||
        produced != expected)
        return false;
    text = {out, produced};
    return true;
}

} // namespace bitchat

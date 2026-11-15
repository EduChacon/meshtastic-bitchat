#pragma once
// Test-only encoder for the app's Golomb-coded set, written from the description in its GCSFilter.swift:
// positions sorted and unique, each delta x >= 1 coded as (x - 1) >> p ones, a zero, then p bits.
#include "bridge/BridgeHistory.h"
#include <algorithm>
#include <vector>

inline std::vector<uint8_t> gcsEncode(std::vector<uint64_t> positions, uint8_t p)
{
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    std::vector<uint8_t> out;
    unsigned filled = 0;
    const auto put = [&](bool bit) {
        if (!filled)
            out.push_back(0);
        out.back() |= uint8_t(bit << (7 - filled));
        filled = (filled + 1) % 8;
    };
    uint64_t previous = 0;
    for (const uint64_t position : positions) {
        const uint64_t value = position - previous - 1;
        previous = position;
        for (uint64_t i = 0; i < (value >> p); ++i)
            put(true);
        put(false);
        for (int i = p - 1; i >= 0; --i)
            put((value >> i) & 1);
    }
    return out;
}

// A REQUEST_SYNC payload saying the phone holds exactly these identifiers.
inline std::vector<uint8_t> syncRequest(bitchat::Crypto &crypto, const std::vector<bitchat::SyncId> &held, uint8_t p = 7,
                                        uint64_t since = 0, int types = 3)
{
    const uint32_t m = held.empty() ? 1 : uint32_t(held.size()) << p;
    std::vector<uint64_t> positions;
    for (const auto &id : held)
        positions.push_back(bitchat::syncBucket(crypto, id, m));
    const auto filter = held.empty() ? std::vector<uint8_t>{} : gcsEncode(positions, p);
    std::vector<uint8_t> out{1, 0, 1, p, 2, 0, 4, uint8_t(m >> 24), uint8_t(m >> 16), uint8_t(m >> 8), uint8_t(m)};
    out.insert(out.end(), {3, uint8_t(filter.size() >> 8), uint8_t(filter.size())});
    out.insert(out.end(), filter.begin(), filter.end());
    if (types >= 0)
        out.insert(out.end(), {4, 0, 1, uint8_t(types)});
    if (since) {
        out.insert(out.end(), {5, 0, 8});
        for (int shift = 56; shift >= 0; shift -= 8)
            out.push_back(uint8_t(since >> shift));
    }
    return out;
}

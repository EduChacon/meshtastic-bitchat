#pragma once

#include "BridgeCodec.h"

// Public history: the app keeps recent public messages and gives a newcomer the ones it lacks (its
// "gossip sync"). The bridge does the same for the text it brought from LoRa, so a phone that arrives
// later still gets it. Formats follow the app's RequestSyncPacket, GCSFilter and PacketIdUtil.

#ifndef BITCHAT_HISTORY_ENTRIES
#define BITCHAT_HISTORY_ENTRIES 24
#endif

namespace bitchat
{

// A text frame signed by the bridge: header, sender, at most 96 bytes of text and the signature.
constexpr size_t HISTORY_FRAME = 224;
// The app offers and accepts public messages for six hours.
constexpr uint64_t HISTORY_MS = 6ULL * 60 * 60 * 1000;
constexpr size_t SYNC_FILTER_LIMIT = 1024;
using SyncId = std::array<uint8_t, 16>;

// What the asking phone already holds.
struct SyncRequest {
    uint8_t p = 0;
    uint32_t m = 0;
    Bytes filter{};
    bool messages = true;
    uint64_t since = 0;
};

Result decodeSyncRequest(Bytes payload, SyncRequest &out);
void syncId(Crypto &crypto, const Packet &packet, SyncId &out);
// Position of an identifier in a filter of range m: never zero.
uint64_t syncBucket(Crypto &crypto, const SyncId &id, uint32_t m);
// Whether a Golomb-Rice coded set (parameter p, range m, bits most significant first) holds a position.
bool syncFilterHolds(uint8_t p, uint32_t m, Bytes filter, uint64_t bucket);

class History
{
  public:
    // Keeps one signed public message; with the table full, the oldest gives way.
    bool keep(Crypto &crypto, Bytes frame);
    // Messages still inside the window.
    size_t count(uint64_t now) const;
    // The next kept message the request lacks, newest first. `cursor` starts at zero and tells where to
    // go on. The copy is marked as an answer to a request: time to live zero and the reply flag.
    bool next(Crypto &crypto, const SyncRequest &request, uint64_t now, size_t &cursor, Frame &out) const;

  private:
    struct Entry {
        std::array<uint8_t, HISTORY_FRAME> frame{};
        uint16_t size = 0;
        uint64_t timestamp = 0;
        SyncId id{};
    };
    std::array<Entry, BITCHAT_HISTORY_ENTRIES> entries{};
    size_t newest = 0, used = 0;
    static bool current(const Entry &entry, uint64_t now);
};

} // namespace bitchat

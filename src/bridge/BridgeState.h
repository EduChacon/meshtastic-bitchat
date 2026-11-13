#pragma once

#include "BridgeCodec.h"
#include "mesh/Throttle.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace bitchat {

template <typename T, size_t Capacity> class BoundedQueue {
public:
  bool push(const T &value) {
    if (count == Capacity)
      return false;
    entries[(head + count) % Capacity] = value;
    ++count;
    return true;
  }
  T *front() { return count ? &entries[head] : nullptr; }
  void pop() {
    if (count) {
      head = (head + 1) % Capacity;
      --count;
    }
  }
  size_t size() const { return count; }
  void clear() { head = count = 0; }

private:
  std::array<T, Capacity> entries{};
  size_t head = 0;
  size_t count = 0;
};

class RateGate {
public:
  bool admit(uint32_t now, uint32_t interval) {
    if (used && Throttle::isWithinTimespanMsAt(last, interval, now))
      return false;
    used = true;
    last = now;
    return true;
  }

private:
  uint32_t last = 0;
  bool used = false;
};

template <size_t Capacity> class DedupCache {
public:
  bool contains(const Digest &digest, uint32_t now, uint32_t lifetime) const {
    for (const auto &entry : entries)
      if (entry.used && entry.digest == digest &&
          Throttle::isWithinTimespanMsAt(entry.at, lifetime, now))
        return true;
    return false;
  }
  Result insert(const Digest &digest, uint32_t now, uint32_t lifetime) {
    if (contains(digest, now, lifetime))
      return Result::Duplicate;
    for (size_t i = 0; i < Capacity; ++i) {
      auto &entry = entries[(next + i) % Capacity];
      if (!entry.used ||
          !Throttle::isWithinTimespanMsAt(entry.at, lifetime, now)) {
        entry = {digest, now, true};
        next = (next + i + 1) % Capacity;
        return Result::Ok;
      }
    }
    return Result::Full;
  }

private:
  struct Entry {
    Digest digest{};
    uint32_t at = 0;
    bool used = false;
  };
  std::array<Entry, Capacity> entries{};
  size_t next = 0;
};

struct Peer {
  PeerId id{};
  Announcement announcement{};
  uint32_t seenAt = 0;
  bool used = false;
  RateGate traffic;
  RateGate lora;
  RateGate presence;
};

class PeerTable {
public:
  static constexpr size_t CAPACITY = 24;
  Result announce(Crypto &crypto, Bytes frame, uint64_t nowUtc, uint32_t now,
                  Peer *&peer);
  Result authenticate(Crypto &crypto, Bytes frame, uint64_t nowUtc,
                      uint32_t now, Peer *&peer);
  Peer *find(const PeerId &id);
  size_t size() const;

private:
  std::array<Peer, CAPACITY> entries{};
};

class BleReassembly {
public:
  Result accept(const Packet &fragment, uint32_t now, Frame &output);
  void expire(uint32_t now);
  static constexpr size_t CAPACITY = 3;
  static constexpr size_t MAX_FRAGMENTS = 32;

  bool empty() const {
    return std::none_of(entries.begin(), entries.end(),
                        [](const auto &entry) { return entry.used; });
  }

private:
  struct Assembly {
    PeerId sender{};
    PeerId id{};
    uint64_t timestamp = 0;
    uint32_t started = 0;
    uint16_t count = 0;
    uint8_t originalType = 0;
    uint8_t minimumTtl = 0;
    bool used = false;
    std::array<uint16_t, MAX_FRAGMENTS> sizes{};
    std::array<uint16_t, MAX_FRAGMENTS> offsets{};
    std::array<uint8_t, MAX_FRAME> bytes{};
    size_t occupied = 0;
  };
  std::array<Assembly, CAPACITY> entries{};
};

} // namespace bitchat

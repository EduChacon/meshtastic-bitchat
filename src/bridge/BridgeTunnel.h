#pragma once

#include "BridgeCodec.h"
#include <algorithm>
#include <array>

namespace bitchat {

constexpr size_t TUNNEL_CHUNK = 160;
constexpr size_t TUNNEL_HEADER = 46;
constexpr size_t TUNNEL_TAG = 16;
constexpr size_t TUNNEL_MAX_FRAGMENT =
    TUNNEL_HEADER + TUNNEL_CHUNK + TUNNEL_TAG;
constexpr size_t TUNNEL_MAX_FRAGMENTS = 13;
using TunnelDomain = std::array<uint8_t, 8>;

struct TunnelIdentity {
  TunnelDomain domain{};
  uint32_t origin = 0;
  uint32_t boot = 0;
  uint32_t sequence = 0;
};

struct TunnelFragment {
  TunnelIdentity identity{};
  std::array<uint8_t, 16> packetId{};
  uint16_t total = 0;
  uint8_t index = 0;
  uint8_t count = 0;
  Bytes payload{};
};

Result encodeTunnelFragment(Crypto &crypto, Bytes frame,
                            const TunnelIdentity &identity,
                            const uint8_t key[32], uint8_t index,
                            uint8_t *output, size_t capacity, size_t &length);
Result decodeTunnelFragment(Crypto &crypto, Bytes wire,
                            const TunnelDomain &domain, const uint8_t key[32],
                            TunnelFragment &output);

class TunnelReassembly {
public:
  static constexpr size_t CAPACITY = 3;
  Result accept(Crypto &crypto, const TunnelFragment &fragment, uint32_t now,
                Frame &output);
  size_t expire(uint32_t now);

  bool empty() const {
    return std::none_of(entries.begin(), entries.end(),
                        [](const auto &entry) { return entry.used; });
  }

private:
  struct Assembly {
    TunnelIdentity identity{};
    std::array<uint8_t, 16> packetId{};
    uint16_t total = 0;
    uint16_t received = 0;
    uint32_t started = 0;
    bool used = false;
    Frame frame;
  };
  std::array<Assembly, CAPACITY> entries{};
};

} // namespace bitchat

#pragma once

#include "BridgeCodec.h"
#include <array>

namespace bitchat
{

constexpr size_t TEXT_LIMIT = 120;
constexpr size_t MESH_PAYLOAD_LIMIT = 233;

struct NativeText {
    std::array<uint8_t, MESH_PAYLOAD_LIMIT> data{};
    size_t size = 0;
    Bytes bytes() const { return {data.data(), size}; }
};

Result publicTextForLoRa(const Packet &packet, Bytes &body);
Result makeNativeText(Crypto &crypto, const PeerId &sender, Bytes body, const Digest &id, const uint8_t key[32], NativeText &out);
bool readBridgeTag(Crypto &crypto, Bytes text, const uint8_t key[32], std::array<uint8_t, 16> &id);
Result makeBitchatText(uint32_t from, Bytes body, NativeText &out);
void hexEncode(Bytes input, char *output);
bool hexDecode(Bytes input, uint8_t *output, size_t capacity);
void makeGatewayProof(Crypto &crypto, const PeerId &peer, uint32_t node, const uint8_t key[32], uint8_t output[22]);
bool containsGatewayProof(Crypto &crypto, const Packet &announce, uint32_t ownNode, const uint8_t key[32]);

} // namespace bitchat

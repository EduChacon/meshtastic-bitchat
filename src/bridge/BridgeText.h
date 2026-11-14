#pragma once

#include "BridgeCodec.h"
#include <array>

namespace bitchat
{

// Stock Meshtastic apps cap text at 200 bytes; a full 233-byte payload does not fit beside the routing bitfield.
constexpr size_t LORA_TEXT_LIMIT = 200;
constexpr size_t TEXT_PARTS = 3;
// The Bitchat app verifies a signature over its own re-encoding, which compresses payloads of 100 bytes
// or more. A device cannot reproduce that compression, so text the bridge signs stays below it.
constexpr size_t BITCHAT_TEXT_LIMIT = 99;
constexpr size_t BITCHAT_PARTS = 5;
constexpr size_t LABEL_LIMIT = 30;
constexpr size_t TEXT_BUFFER = 280;
constexpr char LORA_PREFIX[] = "!lora ";
constexpr char TUNNEL_PREFIX[] = "!bctun ";
constexpr char IMPORT_PREFIX[] = "[MT ";
constexpr char REPLY_PREFIX[] = "[bridge] ";
constexpr size_t TUNNEL_TEXT_LIMIT = 127;

struct NativeText {
    std::array<uint8_t, TEXT_BUFFER> data{};
    size_t size = 0;
    Bytes bytes() const { return {data.data(), size}; }
};

struct TextPlan {
    uint8_t count = 0;
    std::array<uint16_t, BITCHAT_PARTS + 1> offsets{};
};

enum class Command : uint8_t { None, Status, Reset };
// "!bridge" or "!ponte" in any letter case asks for a self-test; " reset" after
// it restarts the Bluetooth links.
Command bridgeCommand(Bytes text);
Result publicTextForLoRa(const Packet &packet, Bytes &body);
size_t cleanLabel(Bytes name, char *output, size_t capacity);
Result planNativeText(size_t labelSize, Bytes body, TextPlan &plan);
Result makeNativeText(Bytes label, Bytes body, const TextPlan &plan, size_t part, NativeText &out);
Result planBitchatText(size_t labelSize, Bytes body, TextPlan &plan);
Result makeBitchatText(Bytes label, Bytes body, const TextPlan &plan, size_t part, NativeText &out);
void hexEncode(Bytes input, char *output);
void makeGatewayProof(Crypto &crypto, const PeerId &peer, uint32_t node, const uint8_t key[32], uint8_t output[22]);
bool gatewayProofNode(Crypto &crypto, const Packet &announce, uint32_t ownNode, const uint8_t key[32], uint32_t &node);

} // namespace bitchat

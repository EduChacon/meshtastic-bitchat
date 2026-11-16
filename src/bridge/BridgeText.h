#pragma once

#include "BridgeCodec.h"
#include <array>

namespace bitchat
{

// Stock Meshtastic apps cap text at 200 bytes; a full 233-byte payload does not fit beside the routing bitfield.
constexpr size_t LORA_TEXT_LIMIT = 200;
constexpr size_t TEXT_PARTS = 3;
// The Bitchat app verifies a signature over its own re-encoding, which compresses payloads of 100 bytes
// or more. A device cannot reproduce that compression, so text the bridge signs stays below it, and at
// 96 bytes the whole signed frame also fits one write at the 185-byte MTU common on iPhones.
constexpr size_t BITCHAT_TEXT_LIMIT = 96;
constexpr size_t BITCHAT_PARTS = 5;
constexpr size_t LABEL_LIMIT = 30;
constexpr size_t TEXT_BUFFER = 280;
constexpr char LORA_PREFIX[] = "!lora ";
constexpr char TUNNEL_PREFIX[] = "!bctun ";
constexpr char IMPORT_PREFIX[] = "[MT ";
// The bridge of the other LoRa network tags what it brings to Bitchat in the same way, with its own letters.
constexpr char FOREIGN_PREFIX[] = "[MC ";
constexpr char FOREIGN_TAG[] = "MC";
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

// The settings (on, off, local, lora, link, economy, prefix, reset) are accepted only from the app attached
// to the radio; the rest may also be asked from Bitchat, the note changed by anyone nearby.
enum class Command : uint8_t {
    None,
    Status,
    Reset,
    On,
    Off,
    Local,
    Lora,
    Help,
    LinkOn,
    LinkOff,
    EconomyOn,
    EconomyOff,
    Guide,
    GuidePt,
    Who,
    WhoPt,
    PrefixOn,
    PrefixOff,
    Note,
    NoteOff,
    Battery,
    Radios,
    Sos,
    Test
};
// A command and what follows it. Portuguese when "!ponte" or a Portuguese word was used; for "note" and
// "sos" the argument is the rest of the text, in its own letter case.
struct BridgeRequest {
    Command command = Command::None;
    bool portuguese = false;
    Bytes argument{};
};
constexpr size_t COMMAND_TEXT_LIMIT = 200;
bool parseBridgeCommand(Bytes text, BridgeRequest &out);
// "!bridge" or "!ponte" in any letter case asks for a self-test; " reset" after
// it restarts the Bluetooth links.
Command bridgeCommand(Bytes text);
// Text for a note or an SOS: valid UTF-8, control characters dropped, cut on a character boundary.
size_t cleanText(Bytes text, char *output, size_t capacity, size_t limit);
// A rough charge for a lithium cell from its voltage; -1 when the reading is not a battery's.
int batteryPercent(uint16_t millivolts);
// Range test between bridges, carried as ordinary channel text: "#ponte-teste 1234", answered with
// "#ponte-teste 1234 ouvido -4.5 dB". A tenth of a decibel per unit, or SNR_UNKNOWN.
constexpr char TEST_MARK[] = "#ponte-teste ";
constexpr int16_t SNR_UNKNOWN = -32768;
bool parseRangeTest(Bytes body, uint16_t &id, bool &answer, int16_t &snrTenths);
size_t formatTenths(int16_t tenths, char *out, size_t capacity, bool comma);
// The short guide, one message per line, in English or Portuguese. Every line fits a signed Bitchat message.
enum class GuideMode : uint8_t { AllText, OnlyPrefixed, NoLora };
constexpr size_t GUIDE_LINES = 4;
const char *guideLine(bool portuguese, bool forApp, GuideMode mode, size_t index);
Result publicTextForLoRa(const Packet &packet, Bytes &body);
// "MT " or "MC " opens the label of text that already went from one LoRa network to the other.
bool networkTag(Bytes text);
// Text the other network's bridge brought to Bitchat, "[MC name] text", to be taken across once: the
// label for this side ("MC name") and the body. Text that already crossed is refused.
Result foreignTextForLoRa(const Packet &packet, char *label, size_t capacity, size_t &labelSize, Bytes &body);
// LoRa text that came from the other network, "MC name: text": the marked name and where the text starts.
bool crossedLoRaText(Bytes text, Bytes &name, Bytes &body);
size_t cleanLabel(Bytes name, char *output, size_t capacity);
Result planNativeText(size_t labelSize, Bytes body, TextPlan &plan);
Result makeNativeText(Bytes label, Bytes body, const TextPlan &plan, size_t part, NativeText &out);
Result planBitchatText(size_t labelSize, Bytes body, TextPlan &plan);
Result makeBitchatText(Bytes label, Bytes body, const TextPlan &plan, size_t part, NativeText &out);
void hexEncode(Bytes input, char *output);
// With this marker the bridge announcement is 182 bytes: one notification at the 185-byte MTU common on
// iPhones, and a payload under the size the app tries to compress before verifying.
constexpr size_t GATEWAY_PROOF = 8;
void makeGatewayProof(Crypto &crypto, const PeerId &peer, uint32_t node, const uint8_t key[32], uint8_t output[GATEWAY_PROOF]);
bool gatewayProofNode(Crypto &crypto, const Packet &announce, uint32_t ownNode, const uint8_t key[32], uint32_t &node);

} // namespace bitchat

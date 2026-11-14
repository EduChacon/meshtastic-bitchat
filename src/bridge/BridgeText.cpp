#include "BridgeText.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace bitchat
{

void makeGatewayProof(Crypto &crypto, const PeerId &peer, uint32_t node, const uint8_t key[32], uint8_t output[GATEWAY_PROOF])
{
    std::array<uint8_t, 22> material{};
    std::memcpy(material.data(), "BC-OWNER-1", 10);
    std::copy(peer.begin(), peer.end(), material.begin() + 10);
    write32(material.data() + 18, node);
    Digest mac;
    crypto.mac({material.data(), material.size()}, key, mac);
    write32(output, node);
    std::copy_n(mac.begin(), GATEWAY_PROOF - 4, output + 4);
}

bool gatewayProofNode(Crypto &crypto, const Packet &announce, uint32_t ownNode, const uint8_t key[32], uint32_t &node)
{
    if (announce.type != 1 || announce.compressed() || !announce.signedPacket())
        return false;
    for (size_t offset = 0; offset + 2 <= announce.payload.size;) {
        const uint8_t type = announce.payload.data[offset++];
        const size_t length = announce.payload.data[offset++];
        if (length > announce.payload.size - offset)
            return false;
        const uint8_t *proof = announce.payload.data + offset;
        if (type == GATEWAY_TLV && length == GATEWAY_PROOF && read32(proof) != ownNode) {
            std::array<uint8_t, GATEWAY_PROOF> expected;
            makeGatewayProof(crypto, announce.sender, read32(proof), key, expected.data());
            if (equalConstantTime({proof, length}, {expected.data(), expected.size()})) {
                node = read32(proof);
                return true;
            }
        }
        offset += length;
    }
    return false;
}

void hexEncode(Bytes input, char *output)
{
    static constexpr char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < input.size; ++i) {
        output[i * 2] = HEX[input.data[i] >> 4];
        output[i * 2 + 1] = HEX[input.data[i] & 15];
    }
    output[input.size * 2] = 0;
}

// Largest cut of at most `limit` bytes that keeps UTF-8 characters whole, preferring a space up to `reach` bytes back.
static size_t textCut(Bytes text, size_t limit, size_t reach)
{
    if (text.size <= limit)
        return text.size;
    size_t cut = limit;
    while (cut && (text.data[cut] & 0xc0) == 0x80)
        --cut;
    for (size_t back = 0; back < reach && back < cut; ++back)
        if (text.data[cut - back - 1] == ' ')
            return cut - back;
    return cut;
}

static Result splitText(Bytes body, size_t room, size_t reach, size_t parts, TextPlan &plan)
{
    size_t offset = 0;
    while (offset < body.size) {
        if (plan.count == parts)
            return Result::Oversize;
        offset += textCut({body.data + offset, body.size - offset}, room, reach);
        plan.offsets[++plan.count] = uint16_t(offset);
    }
    return Result::Ok;
}

Command bridgeCommand(Bytes text)
{
    while (text.size && (text.data[0] == ' ' || text.data[0] == '\n'))
        text = {text.data + 1, text.size - 1};
    while (text.size && (text.data[text.size - 1] == ' ' || text.data[text.size - 1] == '\n' || text.data[text.size - 1] == '\r'))
        --text.size;
    char word[16];
    if (text.size < 6 || text.size >= sizeof(word) || text.data[0] != '!')
        return Command::None;
    // Phone keyboards capitalise on their own.
    for (size_t i = 0; i < text.size; ++i)
        word[i] = text.data[i] >= 'A' && text.data[i] <= 'Z' ? char(text.data[i] + 32) : char(text.data[i]);
    word[text.size] = 0;
    for (const char *name : {"!bridge", "!ponte"}) {
        const size_t length = std::strlen(name);
        if (!std::strncmp(word, name, length)) {
            if (!word[length])
                return Command::Status;
            static const struct {
                const char *word;
                Command command;
            } choices[] = {{" reset", Command::Reset}, {" on", Command::On},     {" off", Command::Off},
                           {" local", Command::Local}, {" lora", Command::Lora}, {" help", Command::Help}};
            for (const auto &choice : choices)
                if (!std::strcmp(word + length, choice.word))
                    return choice.command;
        }
    }
    return Command::None;
}

Result publicTextForLoRa(const Packet &p, Bytes &body)
{
    body = {};
    if (p.type != 2 || p.directed() || !p.signedPacket() || p.compressed() || p.replay() ||
        startsWith(p.payload, TUNNEL_PREFIX) || startsWith(p.payload, IMPORT_PREFIX) || startsWith(p.payload, REPLY_PREFIX))
        return Result::Policy;
    body = p.payload;
    if (startsWith(body, LORA_PREFIX))
        body = {body.data + sizeof(LORA_PREFIX) - 1, body.size - (sizeof(LORA_PREFIX) - 1)};
    return body.size && validUtf8(body) ? Result::Ok : Result::Malformed;
}

size_t cleanLabel(Bytes name, char *output, size_t capacity)
{
    size_t size = 0;
    const size_t limit = std::min(capacity - 1, LABEL_LIMIT);
    if (validUtf8(name)) {
        for (size_t i = 0; i < name.size; ++i) {
            const uint8_t c = name.data[i];
            // Brackets and colons delimit labels; control characters could forge a second line.
            if (c < 0x20 || c == 0x7f || c == '[' || c == ']' || c == ':')
                continue;
            size_t length = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
            if (length > limit - size)
                break;
            while (length--)
                output[size++] = char(name.data[i++]);
            --i;
        }
    }
    while (size && output[size - 1] == ' ')
        --size;
    output[size] = 0;
    return size;
}

Result planNativeText(size_t labelSize, Bytes body, TextPlan &plan)
{
    plan = {};
    if (!labelSize || labelSize > LABEL_LIMIT || !body.size || body.size > UINT16_MAX || !validUtf8(body))
        return Result::Malformed;
    if (labelSize + 2 + body.size <= LORA_TEXT_LIMIT) {
        plan.count = 1;
        plan.offsets[1] = uint16_t(body.size);
        return Result::Ok;
    }
    // "label (1/3): text"
    return splitText(body, LORA_TEXT_LIMIT - labelSize - 8, 24, TEXT_PARTS, plan);
}

Result makeNativeText(Bytes label, Bytes body, const TextPlan &plan, size_t part, NativeText &out)
{
    out.size = 0;
    if (part >= plan.count || plan.offsets[part + 1] > body.size || plan.offsets[part] >= plan.offsets[part + 1])
        return Result::Malformed;
    char head[LABEL_LIMIT + 12];
    const auto *name = reinterpret_cast<const char *>(label.data);
    const int count = plan.count == 1 ? std::snprintf(head, sizeof(head), "%.*s: ", int(label.size), name)
                                      : std::snprintf(head, sizeof(head), "%.*s (%u/%u): ", int(label.size), name,
                                                      unsigned(part + 1), unsigned(plan.count));
    const size_t size = plan.offsets[part + 1] - plan.offsets[part];
    if (count < 0 || size_t(count) >= sizeof(head) || size_t(count) + size > LORA_TEXT_LIMIT)
        return Result::Oversize;
    std::memcpy(out.data.data(), head, count);
    std::memcpy(out.data.data() + count, body.data + plan.offsets[part], size);
    out.size = size_t(count) + size;
    return Result::Ok;
}

Result planBitchatText(size_t labelSize, Bytes body, TextPlan &plan)
{
    plan = {};
    if (!labelSize || labelSize > LABEL_LIMIT || !body.size || body.size > UINT16_MAX || !validUtf8(body))
        return Result::Malformed;
    // "[MT label] text"
    if (labelSize + 6 + body.size <= BITCHAT_TEXT_LIMIT) {
        plan.count = 1;
        plan.offsets[1] = uint16_t(body.size);
        return Result::Ok;
    }
    // "[MT label 1/5] text"
    return splitText(body, BITCHAT_TEXT_LIMIT - labelSize - 10, 10, BITCHAT_PARTS, plan);
}

Result makeBitchatText(Bytes label, Bytes body, const TextPlan &plan, size_t part, NativeText &out)
{
    out.size = 0;
    if (part >= plan.count || plan.offsets[part + 1] > body.size || plan.offsets[part] >= plan.offsets[part + 1])
        return Result::Malformed;
    char head[LABEL_LIMIT + 12];
    const auto *name = reinterpret_cast<const char *>(label.data);
    const int count = plan.count == 1 ? std::snprintf(head, sizeof(head), "%s%.*s] ", IMPORT_PREFIX, int(label.size), name)
                                      : std::snprintf(head, sizeof(head), "%s%.*s %u/%u] ", IMPORT_PREFIX, int(label.size), name,
                                                      unsigned(part + 1), unsigned(plan.count));
    const size_t size = plan.offsets[part + 1] - plan.offsets[part];
    if (count < 0 || size_t(count) >= sizeof(head) || size_t(count) + size > BITCHAT_TEXT_LIMIT)
        return Result::Oversize;
    std::memcpy(out.data.data(), head, count);
    std::memcpy(out.data.data() + count, body.data + plan.offsets[part], size);
    out.size = size_t(count) + size;
    return Result::Ok;
}

} // namespace bitchat

#include "BridgeText.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace bitchat
{

void makeGatewayProof(Crypto &crypto, const PeerId &peer, uint32_t node, const uint8_t key[32], uint8_t output[22])
{
    std::array<uint8_t, 22> material{};
    std::memcpy(material.data(), "BC-OWNER-1", 10);
    std::copy(peer.begin(), peer.end(), material.begin() + 10);
    write32(material.data() + 18, node);
    Digest mac;
    crypto.mac({material.data(), material.size()}, key, mac);
    output[0] = 'B';
    output[1] = 1;
    write32(output + 2, node);
    std::copy_n(mac.begin(), 16, output + 6);
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
        if (type == GATEWAY_TLV && length == 22 && read32(proof + 2) != ownNode) {
            std::array<uint8_t, 22> expected;
            makeGatewayProof(crypto, announce.sender, read32(proof + 2), key, expected.data());
            if (equalConstantTime({proof, length}, {expected.data(), expected.size()})) {
                node = read32(proof + 2);
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

// Largest cut of at most `limit` bytes that keeps UTF-8 characters whole, preferring a nearby space.
static size_t textCut(Bytes text, size_t limit)
{
    if (text.size <= limit)
        return text.size;
    size_t cut = limit;
    while (cut && (text.data[cut] & 0xc0) == 0x80)
        --cut;
    for (size_t back = 0; back < 24 && back < cut; ++back)
        if (text.data[cut - back - 1] == ' ')
            return cut - back;
    return cut;
}

Result publicTextForLoRa(const Packet &p, Bytes &body)
{
    body = {};
    if (p.type != 2 || p.directed() || !p.signedPacket() || p.compressed() || p.replay() ||
        startsWith(p.payload, TUNNEL_PREFIX) || startsWith(p.payload, IMPORT_PREFIX))
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
    const size_t room = LORA_TEXT_LIMIT - labelSize - 8;
    size_t offset = 0;
    while (offset < body.size) {
        if (plan.count == TEXT_PARTS)
            return Result::Oversize;
        offset += textCut({body.data + offset, body.size - offset}, room);
        plan.offsets[++plan.count] = uint16_t(offset);
    }
    return Result::Ok;
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

Result makeBitchatText(Bytes label, Bytes body, NativeText &out)
{
    out.size = 0;
    if (!label.size || label.size > LABEL_LIMIT || body.size == 0 || !validUtf8(body))
        return Result::Malformed;
    char head[LABEL_LIMIT + 8];
    const int count =
        std::snprintf(head, sizeof(head), "%s%.*s] ", IMPORT_PREFIX, int(label.size), reinterpret_cast<const char *>(label.data));
    if (count < 0 || size_t(count) >= sizeof(head) || body.size + size_t(count) > out.data.size())
        return Result::Oversize;
    std::memcpy(out.data.data(), head, count);
    std::memcpy(out.data.data() + count, body.data, body.size);
    out.size = size_t(count) + body.size;
    return Result::Ok;
}

} // namespace bitchat

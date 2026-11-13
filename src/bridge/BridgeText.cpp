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

bool containsGatewayProof(Crypto &crypto, const Packet &announce, uint32_t ownNode, const uint8_t key[32])
{
    if (announce.type != 1 || announce.compressed() || !announce.signedPacket())
        return false;
    for (size_t offset = 0; offset + 2 <= announce.payload.size;) {
        const uint8_t type = announce.payload.data[offset++];
        const size_t length = announce.payload.data[offset++];
        if (length > announce.payload.size - offset)
            return false;
        const uint8_t *proof = announce.payload.data + offset;
        if (type == 0x7f && length == 22 && read32(proof + 2) != ownNode) {
            std::array<uint8_t, 22> expected;
            makeGatewayProof(crypto, announce.sender, read32(proof + 2), key, expected.data());
            if (equalConstantTime({proof, length}, {expected.data(), expected.size()}))
                return true;
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

bool hexDecode(Bytes input, uint8_t *output, size_t capacity)
{
    if (input.size % 2 || input.size / 2 > capacity)
        return false;
    for (size_t i = 0; i < input.size; ++i) {
        uint8_t c = input.data[i];
        uint8_t nibble;
        if (c >= '0' && c <= '9')
            nibble = c - '0';
        else if (c >= 'a' && c <= 'f')
            nibble = c - 'a' + 10;
        else
            return false;
        if ((i & 1) == 0)
            output[i / 2] = uint8_t(nibble << 4);
        else
            output[i / 2] |= nibble;
    }
    return true;
}

Result publicTextForLoRa(const Packet &p, Bytes &body)
{
    body = {};
    if (p.type != 2 || p.directed() || !p.signedPacket() || p.compressed() || p.replay() || !startsWith(p.payload, "!lora "))
        return Result::Policy;
    body = {p.payload.data + 6, p.payload.size - 6};
    if (body.size == 0 || !validUtf8(body))
        return Result::Malformed;
    return body.size <= TEXT_LIMIT ? Result::Ok : Result::Oversize;
}

Result makeNativeText(Crypto &crypto, const PeerId &sender, Bytes body, const Digest &id, const uint8_t key[32], NativeText &out)
{
    out.size = 0;
    if (body.size == 0 || body.size > TEXT_LIMIT || !validUtf8(body))
        return Result::Oversize;
    char senderHex[17];
    char idHex[33];
    hexEncode({sender.data(), sender.size()}, senderHex);
    hexEncode({id.data(), 16}, idHex);
    char prefix[32];
    const int prefixSize = std::snprintf(prefix, sizeof(prefix), "[BC %s] ", senderHex);
    if (prefixSize < 0 || size_t(prefixSize) >= sizeof(prefix))
        return Result::Malformed;
    const size_t length = size_t(prefixSize) + body.size + 56;
    if (length > out.data.size())
        return Result::Oversize;
    auto *b = out.data.data();
    std::memcpy(b, prefix, prefixSize);
    std::memcpy(b + prefixSize, body.data, body.size);
    size_t offset = size_t(prefixSize) + body.size;
    std::memcpy(b + offset, " [mb1:", 6);
    offset += 6;
    std::memcpy(b + offset, idHex, 32);
    offset += 32;
    b[offset++] = ':';
    Digest mac;
    crypto.mac({b, offset}, key, mac);
    char tag[17];
    hexEncode({mac.data(), 8}, tag);
    std::memcpy(b + offset, tag, 16);
    offset += 16;
    b[offset++] = ']';
    out.size = offset;
    return Result::Ok;
}

bool readBridgeTag(Crypto &crypto, Bytes text, const uint8_t key[32], std::array<uint8_t, 16> &id)
{
    if (text.size < 56)
        return false;
    const size_t at = text.size - 56;
    const auto *tag = text.data + at;
    if (std::memcmp(tag, " [mb1:", 6) != 0 || tag[38] != ':' || tag[55] != ']')
        return false;
    std::array<uint8_t, 8> given;
    if (!hexDecode({tag + 6, 32}, id.data(), id.size()) || !hexDecode({tag + 39, 16}, given.data(), given.size()))
        return false;
    Digest expected;
    crypto.mac({text.data, at + 39}, key, expected);
    return equalConstantTime({given.data(), given.size()}, {expected.data(), given.size()});
}

Result makeBitchatText(uint32_t from, Bytes body, NativeText &out)
{
    out.size = 0;
    if (body.size == 0 || !validUtf8(body))
        return Result::Malformed;
    char prefix[20];
    const int count = std::snprintf(prefix, sizeof(prefix), "[MT !%08lx] ", static_cast<unsigned long>(from));
    if (count < 0 || size_t(count) >= sizeof(prefix) || body.size + size_t(count) > out.data.size())
        return Result::Oversize;
    std::memcpy(out.data.data(), prefix, count);
    std::memcpy(out.data.data() + count, body.data, body.size);
    out.size = size_t(count) + body.size;
    return Result::Ok;
}

} // namespace bitchat

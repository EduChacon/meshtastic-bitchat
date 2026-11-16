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

static char lower(uint8_t c)
{
    return c >= 'A' && c <= 'Z' ? char(c + 32) : char(c);
}

// Whether `text` opens with `word` in any letter case, followed by the end or a space.
static bool opensWith(Bytes text, const char *word)
{
    const size_t length = std::strlen(word);
    if (text.size < length)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (lower(text.data[i]) != word[i])
            return false;
    return text.size == length || text.data[length] == ' ';
}

static bool blank(uint8_t c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static Bytes trimmed(Bytes text)
{
    while (text.size && blank(text.data[0]))
        text = {text.data + 1, text.size - 1};
    while (text.size && blank(text.data[text.size - 1]))
        --text.size;
    return text;
}

bool parseBridgeCommand(Bytes text, BridgeRequest &out)
{
    out = {};
    text = trimmed(text);
    if (text.size < 6 || text.size > COMMAND_TEXT_LIMIT || text.data[0] != '!')
        return false;
    bool ponte = false;
    if (opensWith(text, "!bridge"))
        text = {text.data + 7, text.size - 7};
    else if (opensWith(text, "!ponte")) {
        text = {text.data + 6, text.size - 6};
        ponte = true;
    } else
        return false;
    // one space between the words, as the commands are written
    if (text.size >= 2 && text.data[1] == ' ')
        return false;
    const Bytes rest = trimmed(text);
    if (!rest.size) {
        out.command = Command::Status;
        out.portuguese = ponte;
        return true;
    }
    // Commands that take text after them.
    static const struct {
        const char *word;
        Command command;
        bool portuguese;
    } withText[] = {{"note", Command::Note, false},
                    {"recado", Command::Note, true},
                    {"sos", Command::Sos, false},
                    {"socorro", Command::Sos, true}};
    for (const auto &choice : withText)
        if (opensWith(rest, choice.word)) {
            const size_t length = std::strlen(choice.word);
            out.argument = trimmed({rest.data + length, rest.size - length});
            out.command = choice.command;
            out.portuguese = ponte || choice.portuguese;
            if (out.command == Command::Note && ((out.argument.size == 3 && opensWith(out.argument, "off")) ||
                                                 (out.argument.size == 6 && opensWith(out.argument, "apagar")))) {
                out.command = Command::NoteOff;
                out.argument = {};
            }
            return true;
        }
    // The others are whole words, compared in lower case.
    char word[24];
    if (rest.size >= sizeof(word))
        return false;
    for (size_t i = 0; i < rest.size; ++i)
        word[i] = lower(rest.data[i]);
    word[rest.size] = 0;
    static const struct {
        const char *word;
        Command command;
        bool portuguese;
    } choices[] = {{"reset", Command::Reset, false},
                   {"on", Command::On, false},
                   {"off", Command::Off, false},
                   {"local", Command::Local, false},
                   {"lora", Command::Lora, false},
                   {"help", Command::Help, false},
                   {"link on", Command::LinkOn, false},
                   {"link off", Command::LinkOff, false},
                   {"economy on", Command::EconomyOn, false},
                   {"economy off", Command::EconomyOff, false},
                   {"guide", Command::Guide, false},
                   {"guia", Command::GuidePt, true},
                   {"who", Command::Who, false},
                   {"quem", Command::WhoPt, true},
                   {"prefix on", Command::PrefixOn, false},
                   {"prefix off", Command::PrefixOff, false},
                   {"battery", Command::Battery, false},
                   {"bateria", Command::Battery, true},
                   {"radios", Command::Radios, false},
                   {"test", Command::Test, false},
                   {"teste", Command::Test, true}};
    for (const auto &choice : choices)
        if (!std::strcmp(word, choice.word)) {
            out.command = choice.command;
            // the guide and the names keep the language of their own word
            out.portuguese = choice.command == Command::Guide || choice.command == Command::Who ? false
                             : choice.command == Command::GuidePt || choice.command == Command::WhoPt
                                 ? true
                                 : ponte || choice.portuguese;
            return true;
        }
    return false;
}

Command bridgeCommand(Bytes text)
{
    BridgeRequest request;
    parseBridgeCommand(text, request);
    return request.command;
}

size_t cleanText(Bytes text, char *output, size_t capacity, size_t limit)
{
    size_t size = 0;
    limit = std::min(capacity - 1, limit);
    if (validUtf8(text)) {
        for (size_t i = 0; i < text.size; ++i) {
            const uint8_t c = text.data[i];
            // a line break or a tab separates words; other control characters are dropped
            if (c == '\n' || c == '\r' || c == '\t') {
                if (size && output[size - 1] != ' ' && size < limit)
                    output[size++] = ' ';
                continue;
            }
            if (c < 0x20 || c == 0x7f)
                continue;
            size_t length = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
            if (length > limit - size)
                break;
            while (length--)
                output[size++] = char(text.data[i++]);
            --i;
        }
    }
    while (size && output[size - 1] == ' ')
        --size;
    output[size] = 0;
    return size;
}

int batteryPercent(uint16_t millivolts)
{
    // Resting voltage of a lithium-ion cell against its charge, roughly.
    static const struct {
        uint16_t millivolts;
        uint8_t percent;
    } curve[] = {{3300, 0}, {3500, 5}, {3650, 20}, {3750, 40}, {3850, 60}, {4000, 80}, {4200, 100}};
    if (millivolts < 2500 || millivolts > 4500)
        return -1;
    if (millivolts <= curve[0].millivolts)
        return 0;
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i)
        if (millivolts <= curve[i].millivolts) {
            const auto &a = curve[i - 1], &b = curve[i];
            return a.percent + (millivolts - a.millivolts) * (b.percent - a.percent) / (b.millivolts - a.millivolts);
        }
    return 100;
}

bool parseRangeTest(Bytes body, uint16_t &id, bool &answer, int16_t &snrTenths)
{
    const size_t mark = sizeof(TEST_MARK) - 1;
    if (body.size < mark + 4 || std::memcmp(body.data, TEST_MARK, mark))
        return false;
    size_t i = mark;
    uint32_t value = 0;
    for (size_t n = 0; n < 4; ++n, ++i) {
        if (body.data[i] < '0' || body.data[i] > '9')
            return false;
        value = value * 10 + (body.data[i] - '0');
    }
    id = uint16_t(value);
    answer = false;
    snrTenths = SNR_UNKNOWN;
    if (i == body.size)
        return true;
    static const char said[] = " ouvido ";
    if (body.size - i < sizeof(said) - 1 || std::memcmp(body.data + i, said, sizeof(said) - 1))
        return false;
    i += sizeof(said) - 1;
    answer = true;
    if (body.size - i >= 4 && !std::memcmp(body.data + i, "? dB", 4) && i + 4 == body.size)
        return true;
    bool negative = false;
    if (i < body.size && body.data[i] == '-') {
        negative = true;
        ++i;
    }
    int32_t whole = 0, digits = 0;
    while (i < body.size && body.data[i] >= '0' && body.data[i] <= '9' && digits < 3) {
        whole = whole * 10 + (body.data[i++] - '0');
        ++digits;
    }
    if (!digits || i + 2 > body.size || body.data[i] != '.' || body.data[i + 1] < '0' || body.data[i + 1] > '9')
        return false;
    const int32_t tenths = whole * 10 + (body.data[i + 1] - '0');
    i += 2;
    if (body.size - i != 3 || std::memcmp(body.data + i, " dB", 3))
        return false;
    snrTenths = int16_t(negative ? -tenths : tenths);
    return true;
}

size_t formatTenths(int16_t tenths, char *out, size_t capacity, bool comma)
{
    if (tenths == SNR_UNKNOWN)
        return size_t(snprintf(out, capacity, "?"));
    const int magnitude = tenths < 0 ? -tenths : tenths;
    return size_t(snprintf(out, capacity, "%s%d%c%d", tenths < 0 ? "-" : "", magnitude / 10, comma ? ',' : '.', magnitude % 10));
}

const char *guideLine(bool portuguese, bool forApp, GuideMode mode, size_t index)
{
    // First line per mode, then lines 2 and 3, then the last line in Bitchat and in the app.
    static const char *const english[] = {
        "Guide 1/4: this radio takes the public text of #mesh to a LoRa channel and back.",
        "Guide 1/4: this radio takes #mesh text starting with !lora to a LoRa channel and back.",
        "Guide 1/4: right now this radio only relays Bitchat between phones; LoRa is off.",
        "2/4: text from LoRa shows as [MT name]. Private messages do not cross.",
        "3/4: LoRa is slow and public. Keep texts short; anyone with the channel can read.",
        "4/4: here also: !bridge who, note, battery, radios, test, and sos <text> for help.",
        "4/4: !bridge shows the state, !bridge who the names from LoRa, !bridge help the rest."};
    static const char *const portugueseLines[] = {
        "Guia 1/4: este rádio leva o texto público do #mesh a um canal LoRa e traz o de lá.",
        "Guia 1/4: este rádio leva ao LoRa o texto do #mesh iniciado por !lora e traz o de lá.",
        "Guia 1/4: agora este rádio só repassa o Bitchat entre telefones, sem usar o LoRa.",
        "2/4: o que vem do LoRa aparece como [MT nome]. Mensagens privadas não atravessam.",
        "3/4: o LoRa é lento e público. Escreva pouco; quem tem o canal lê tudo.",
        "4/4: aqui também: !ponte quem, recado, bateria, radios, teste e sos <texto> (socorro).",
        "4/4: !bridge mostra o estado, !ponte quem os nomes do LoRa, !bridge help o resto."};
    if (index >= GUIDE_LINES)
        return nullptr;
    const size_t entry = index == 0 ? static_cast<size_t>(mode) : index == GUIDE_LINES - 1 ? (forApp ? 6 : 5) : index + 2;
    return (portuguese ? portugueseLines : english)[entry];
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

bool networkTag(Bytes text)
{
    return text.size >= 3 && text.data[0] == 'M' && (text.data[1] == 'T' || text.data[1] == 'C') && text.data[2] == ' ';
}

bool crossedLoRaText(Bytes text, Bytes &name, Bytes &body)
{
    if (!networkTag(text))
        return false;
    for (size_t i = 3; i + 1 < text.size && i < LABEL_LIMIT + 12; ++i)
        if (text.data[i] == ':' && text.data[i + 1] == ' ') {
            name = {text.data, i};
            body = {text.data + i + 2, text.size - i - 2};
            return body.size != 0;
        }
    return false;
}

Result foreignTextForLoRa(const Packet &p, char *label, size_t capacity, size_t &labelSize, Bytes &body)
{
    labelSize = 0;
    body = {};
    const size_t prefix = sizeof(FOREIGN_PREFIX) - 1, tag = sizeof(FOREIGN_TAG);
    if (p.type != 2 || p.directed() || !p.signedPacket() || p.compressed() || p.replay() ||
        !startsWith(p.payload, FOREIGN_PREFIX) || capacity < LABEL_LIMIT + 1)
        return Result::Policy;
    size_t close = prefix;
    while (close + 1 < p.payload.size && !(p.payload.data[close] == ']' && p.payload.data[close + 1] == ' '))
        ++close;
    if (close + 1 >= p.payload.size)
        return Result::Malformed;
    const Bytes name{p.payload.data + prefix, close - prefix};
    body = {p.payload.data + close + 2, p.payload.size - close - 2};
    if (!name.size || !body.size || !validUtf8(body))
        return Result::Malformed;
    // One crossing only: text that came from this network must not return to it through the other one.
    Bytes inner, rest;
    if (networkTag(name) || crossedLoRaText(body, inner, rest))
        return Result::Policy;
    std::memcpy(label, FOREIGN_TAG, tag - 1);
    label[tag - 1] = ' ';
    const size_t cleaned = cleanLabel(name, label + tag, LABEL_LIMIT - tag + 1);
    if (!cleaned)
        return Result::Malformed;
    labelSize = tag + cleaned;
    return Result::Ok;
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

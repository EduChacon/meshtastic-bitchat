#ifdef MESHTASTIC_ENABLE_BITCHAT
// Private chats with the bridge: a phone opens one in Bitchat as with any person nearby, and asks there what it
// would ask in the public chat. The answers stay in that chat. "@name text" there goes to that Meshtastic node as
// a direct message, and the node's answer comes back to the chat (stage 2). Nothing else of it goes to LoRa but
// what a command itself sends there (an SOS, a range test).
//
// Handshake packets are unsigned, so anyone can claim a phone's ID: nothing unauthenticated ends a session or a
// handshake under way, a newcomer never pushes out a chat in use, and handshakes are limited per phone and overall.
#include "BitchatBridgeModule.h"
#include "BridgeDiagnostics.h"
#include "BridgePlatform.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "airtime.h"
#include <algorithm>
#include <cstring>
#include <esp_system.h>

using namespace bitchat;

static constexpr uint8_t TYPE_HANDSHAKE = 0x10;
static constexpr uint8_t TYPE_ENCRYPTED = 0x11;
static constexpr uint8_t KIND_MESSAGE = 0x01;
static constexpr uint8_t KIND_DELIVERED = 0x03;
// The largest private message read: an ID and up to 255 bytes of text, with room to spare.
static constexpr size_t PRIVATE_LIMIT = 400;
static constexpr uint32_t SESSION_IDLE_MS = 30UL * 60 * 1000;
// A chat used this recently is not given up for a newcomer.
static constexpr uint32_t SESSION_KEEP_MS = 2UL * 60 * 1000;
static constexpr uint32_t HANDSHAKE_WAIT_MS = 20000;
// After the first try, the bridge starts a handshake with the same phone at most once a minute.
static constexpr uint32_t INITIATE_EVERY_MS = 60000;
// Messages that do not open count against a session only when none has opened for this long.
static constexpr uint32_t QUIET_BEFORE_RESET_MS = 10000;
// Mail and conversations are kept a day.
static constexpr uint32_t KEEP_DAY_MS = 24UL * 3600 * 1000;
static constexpr char DIRECT_PREFIX[] = "[Bitchat ";

static uint32_t idHash(Bytes id)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < id.size; ++i)
        hash = (hash ^ id.data[i]) * 16777619u;
    return hash ? hash : 1;
}

// Random bytes, or false when the generator gave nothing (the nRF52 one fills zeros after a timeout).
static bool randomBytes(uint8_t *out, size_t size)
{
    esp_fill_random(out, size);
    uint8_t any = 0;
    for (size_t i = 0; i < size; ++i)
        any |= out[i];
    return any != 0;
}

// An ID like the apps' own: a random UUID in capitals.
static bool newMessageId(char out[37])
{
    uint8_t bytes[16];
    if (!randomBytes(bytes, sizeof(bytes)))
        return false;
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    static constexpr char DIGITS[] = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out[o++] = '-';
        out[o++] = DIGITS[bytes[i] >> 4];
        out[o++] = DIGITS[bytes[i] & 15];
    }
    out[o] = 0;
    return true;
}

static char lowerAscii(char c)
{
    return c >= 'A' && c <= 'Z' ? char(c + 32) : c;
}

static bool sameWord(Bytes text, const char *word)
{
    const size_t size = std::strlen(word);
    if (text.size != size)
        return false;
    for (size_t i = 0; i < size; ++i)
        if (lowerAscii(char(text.data[i])) != word[i])
            return false;
    return true;
}

// `text` starts with `name` (letter case aside), and the name ends there: at the end, a space or a colon.
static bool startsWithName(Bytes text, const char *name, size_t size)
{
    if (!size || size > text.size)
        return false;
    for (size_t i = 0; i < size; ++i)
        if (lowerAscii(char(text.data[i])) != lowerAscii(name[i]))
            return false;
    return text.size == size || text.data[size] == ' ' || text.data[size] == ':';
}

// What follows a name: the colon and spaces after it left out.
static Bytes afterName(Bytes text, size_t size)
{
    size_t at = size;
    if (at < text.size && text.data[at] == ':')
        ++at;
    while (at < text.size && text.data[at] == ' ')
        ++at;
    return {text.data + at, text.size - at};
}

// Cut on a character boundary, at most `capacity` bytes.
static size_t utf8Fit(const char *text, size_t size, size_t capacity)
{
    if (size <= capacity)
        return size;
    size_t cut = capacity;
    while (cut && (uint8_t(text[cut]) & 0xc0) == 0x80)
        --cut;
    return cut;
}

static void clearSession(PrivateSession &session)
{
    session.handshake.wipe();
    noiseWipe(&session.send, sizeof(session.send));
    noiseWipe(&session.receive, sizeof(session.receive));
    session = {};
}

void BitchatBridgeModule::expireHandshake(PrivateSession &session, uint32_t now)
{
    if (session.handshaking && !Throttle::isWithinTimespanMsAt(session.handshakeAt, HANDSHAKE_WAIT_MS, now)) {
        session.handshaking = false;
        session.handshake.wipe();
    }
}

PrivateSession *BitchatBridgeModule::privateSession(const PeerId &peer, uint32_t now, bool create)
{
    if (!runtime->privates && (!create || !allocateBuffer(runtime->privates)))
        return nullptr;
    PrivateSession *found = nullptr, *free = nullptr, *oldest = nullptr;
    for (auto &session : runtime->privates->slots) {
        if (session.used) {
            expireHandshake(session, now);
            if (!session.handshaking && !Throttle::isWithinTimespanMsAt(session.usedAt, SESSION_IDLE_MS, now))
                clearSession(session);
        }
        if (session.used && session.peer == peer)
            found = &session;
        else if (!session.used) {
            if (!free)
                free = &session;
        } else if (!session.handshaking && !Throttle::isWithinTimespanMsAt(session.usedAt, SESSION_KEEP_MS, now) &&
                   (!oldest || int32_t(session.usedAt - oldest->usedAt) < 0)) {
            oldest = &session;
        }
    }
    if (found || !create)
        return found;
    PrivateSession *slot = free ? free : oldest;
    if (!slot)
        return nullptr;
    clearSession(*slot);
    slot->peer = peer;
    slot->used = true;
    slot->usedAt = now;
    return slot;
}

void BitchatBridgeModule::privateFrame(const Packet &packet, const Peer &peer, uint16_t source, uint32_t now)
{
    // Bitchat never compresses ciphertext in practice; a compressed one is not read.
    if (packet.compressed() || !utc())
        return;
    // Only a first handshake message, or ciphertext from a phone this bridge lost track of, may take a slot.
    const bool opener =
        packet.type == TYPE_HANDSHAKE ? packet.payload.size == NOISE_MESSAGE1 : packet.payload.size > NOISE_TRANSPORT_OVERHEAD;
    PrivateSession *session = privateSession(packet.sender, now, opener);
    if (!session)
        return;
    expireHandshake(*session, now);
    // Answers go straight to the phone when it is connected to this radio, and through the others when not.
    session->link = source != NO_LINK && packet.ttl == 7 ? source : NO_LINK;
    if (packet.type == TYPE_HANDSHAKE)
        privateHandshake(*session, packet, now);
    else if (packet.type == TYPE_ENCRYPTED)
        privateTransport(*session, packet, peer, session->link != NO_LINK, now);
}

static void established(PrivateSession &session, NoiseHandshake &trial, uint32_t now)
{
    trial.split(session.send, session.receive);
    trial.wipe();
    session.handshake.wipe();
    session.handshaking = false;
    session.ready = true;
    session.failures = 0;
    session.usedAt = now;
    session.openedAt = now ? now : 1;
}

void BitchatBridgeModule::privateHandshake(PrivateSession &session, const Packet &packet, uint32_t now)
{
    const Bytes in = packet.payload;
    auto &handshake = session.handshake;
    const uint8_t *staticPrivate = identity.data() + 32;
    PeerId proved{};
    if (session.handshaking && handshake.initiator()) {
        if (in.size == NOISE_MESSAGE2) {
            // The phone answered this bridge's handshake: it must prove the key its ID stands for. Read on a
            // copy, so bytes that do not authenticate leave the handshake waiting for the real answer.
            NoiseHandshake trial = handshake;
            uint8_t out[NOISE_MESSAGE3];
            bool ok = trial.readMessage2(in);
            if (ok) {
                noisePeerId(trial.remoteStatic(), proved);
                ok = proved == packet.sender && trial.writeMessage3(staticPrivate, noisePublic.data(), out) &&
                     sendNoise(TYPE_HANDSHAKE, packet.sender, {out, sizeof(out)}, session.link);
            }
            if (ok) {
                established(session, trial, now);
                flushMailbox(session, now);
            } else {
                trial.wipe();
            }
            return;
        }
        // Both sides started at once: the lower ID stays the initiator, as in both apps.
        if (in.size != NOISE_MESSAGE1 ||
            std::lexicographical_compare(ownId.begin(), ownId.end(), packet.sender.begin(), packet.sender.end()))
            return;
    } else if (session.handshaking && in.size == NOISE_MESSAGE3) {
        NoiseHandshake trial = handshake;
        bool ok = trial.readMessage3(in);
        if (ok) {
            noisePeerId(trial.remoteStatic(), proved);
            ok = proved == packet.sender;
        }
        // The new session replaces the old one only now; anything else keeps waiting.
        if (ok) {
            established(session, trial, now);
            flushMailbox(session, now);
        } else {
            trial.wipe();
        }
        return;
    }
    // Only a first message is answered: with no handshake under way the apps take anything as one, so a
    // stray second or third message is never sent back.
    if (in.size != NOISE_MESSAGE1)
        return;
    // Each answer costs three curve operations: three per phone, then one every 20 s; for all phones together,
    // six, then one every five seconds.
    if (!session.handshakes.admit(now, 3, 20000) || !runtime->handshakes.admit(now, 6, 5000))
        return;
    uint8_t ephemeral[NOISE_KEY];
    if (!randomBytes(ephemeral, sizeof(ephemeral)))
        return;
    NoiseHandshake fresh;
    fresh.start(false, ephemeral);
    noiseWipe(ephemeral, sizeof(ephemeral));
    uint8_t out[NOISE_MESSAGE2];
    if (!fresh.readMessage1(in) || !fresh.writeMessage2(staticPrivate, noisePublic.data(), out) ||
        !sendNoise(TYPE_HANDSHAKE, packet.sender, {out, sizeof(out)}, session.link)) {
        fresh.wipe();
        return;
    }
    // The answer went out: this handshake replaces any earlier one (the phone started again).
    handshake = fresh;
    fresh.wipe();
    session.handshaking = true;
    session.handshakeAt = now;
}

bool BitchatBridgeModule::initiatePrivate(PrivateSession &session, uint32_t now)
{
    expireHandshake(session, now);
    if (session.handshaking ||
        (session.initiatedAt && Throttle::isWithinTimespanMsAt(session.initiatedAt, INITIATE_EVERY_MS, now)) ||
        !session.handshakes.admit(now, 3, 20000) || !runtime->handshakes.admit(now, 6, 5000))
        return false;
    uint8_t ephemeral[NOISE_KEY];
    if (!randomBytes(ephemeral, sizeof(ephemeral)))
        return false;
    session.initiatedAt = now ? now : 1;
    session.handshake.start(true, ephemeral);
    noiseWipe(ephemeral, sizeof(ephemeral));
    uint8_t out[NOISE_MESSAGE1];
    if (!session.handshake.writeMessage1(out) || !sendNoise(TYPE_HANDSHAKE, session.peer, {out, sizeof(out)}, session.link)) {
        session.handshake.wipe();
        return false;
    }
    session.handshaking = true;
    session.handshakeAt = now;
    return true;
}

void BitchatBridgeModule::privateTransport(PrivateSession &session, const Packet &packet, const Peer &peer, bool direct,
                                           uint32_t now)
{
    const Bytes in = packet.payload;
    if (!session.ready) {
        // A phone that still holds a session this bridge lost (a restart): a new handshake, as the apps do.
        initiatePrivate(session, now);
        return;
    }
    if (in.size <= NOISE_TRANSPORT_OVERHEAD || in.size > PRIVATE_LIMIT + NOISE_TRANSPORT_OVERHEAD)
        return;
    uint8_t plain[PRIVATE_LIMIT];
    if (!noiseDecrypt(session.receive, in, plain)) {
        // Three in a row that do not open, while nothing opened for a while: the phone's keys are not this
        // session's. Junk under the phone's ID while it is talking changes nothing.
        if (!Throttle::isWithinTimespanMsAt(session.usedAt, QUIET_BEFORE_RESET_MS, now) && ++session.failures >= 3) {
            session.failures = 0;
            initiatePrivate(session, now);
        }
        return;
    }
    const size_t size = in.size - NOISE_TRANSPORT_OVERHEAD;
    session.failures = 0;
    session.usedAt = now;
    // Delivery and read receipts, verification, files: nothing for the bridge to do.
    if (plain[0] != KIND_MESSAGE) {
        noiseWipe(plain, size);
        return;
    }
    Bytes id{}, content{};
    bool valid = true;
    for (size_t offset = 1; offset < size && valid;) {
        if (offset + 2 > size || offset + 2 + plain[offset + 1] > size) {
            valid = false;
            break;
        }
        const Bytes value{plain + offset + 2, plain[offset + 1]};
        if (plain[offset] == 0)
            id = value;
        else if (plain[offset] == 1)
            content = value;
        else
            valid = false; // both apps refuse fields they do not know
        offset += 2 + value.size;
    }
    if (valid && id.size) {
        // Delivered, every time: without it the app keeps the message and sends it again. A message is
        // remembered only when the delivery went out, so one that could not be acknowledged is answered when
        // the app sends it again.
        if (sendPrivate(session, KIND_DELIVERED, id)) {
            const uint32_t hash = idHash(id);
            const bool repeat = std::find(session.recent.begin(), session.recent.end(), hash) != session.recent.end();
            if (!repeat) {
                session.recent[session.recentNext] = hash;
                session.recentNext = uint8_t((session.recentNext + 1) % session.recent.size());
                // Favourite marks are the app's own control messages, not chat.
                if (content.size && validUtf8(content) && !startsWith(content, "[FAVORITED]") &&
                    !startsWith(content, "[UNFAVORITED]"))
                    privateRequest(session, peer, content, direct, now);
            }
        }
    }
    noiseWipe(plain, size);
    // A phone writing in its chat is here: mail kept for it goes now.
    flushMailbox(session, now);
}

void BitchatBridgeModule::privateRequest(PrivateSession &session, const Peer &peer, Bytes text, bool direct, uint32_t now)
{
    privateReply = &session;
    if (text.size && text.data[0] == '@') {
        directToLoRa(session, peer, text, now);
        privateReply = nullptr;
        return;
    }
    BridgeRequest asked;
    // In the private chat the command words are enough: "bateria" as "!ponte bateria".
    char line[COMMAND_TEXT_LIMIT + 1];
    if (!parseBridgeCommand(text, asked)) {
        if (sameWord(text, "ponte") || sameWord(text, "estado")) {
            asked.command = Command::Status;
            asked.portuguese = true;
        } else if (sameWord(text, "bridge") || sameWord(text, "status")) {
            asked.command = Command::Status;
        } else {
            for (const char *prefix : {"!bridge ", "!ponte "}) {
                const size_t length = std::strlen(prefix);
                if (text.size + length >= sizeof(line))
                    continue;
                std::memcpy(line, prefix, length);
                std::memcpy(line + length, text.data, text.size);
                if (parseBridgeCommand({reinterpret_cast<const uint8_t *>(line), length + text.size}, asked))
                    break;
            }
        }
    }
    if (asked.command == Command::None || asked.command == Command::Help) {
        static constexpr char HELP[] =
            "Ponte LoRa. Escreva: ponte (estado), bateria, quem, guia, teste, recado <texto>, sos <texto>, "
            "@contato texto (mensagem privada pelo LoRa). In English: bridge, battery, who, guide, test, note, sos, "
            "@node text.";
        replyPrivately(HELP, sizeof(HELP) - 1);
    } else {
        answerAsked(asked, peer, direct, now);
    }
    privateReply = nullptr;
}

bool BitchatBridgeModule::replyPrivately(const char *text, size_t size)
{
    if (!privateReply || !privateReply->ready)
        return false;
    const size_t prefix = sizeof(REPLY_PREFIX) - 1;
    if (size >= prefix && std::memcmp(text, REPLY_PREFIX, prefix) == 0) {
        text += prefix;
        size -= prefix;
    }
    size = utf8Fit(text, size, 255);
    char id[37];
    if (!newMessageId(id))
        return false;
    uint8_t body[2 + 36 + 2 + 255];
    size_t offset = 0;
    body[offset++] = 0;
    body[offset++] = 36;
    std::memcpy(body + offset, id, 36);
    offset += 36;
    body[offset++] = 1;
    body[offset++] = uint8_t(size);
    std::memcpy(body + offset, text, size);
    offset += size;
    const bool sent = sendPrivate(*privateReply, KIND_MESSAGE, {body, offset});
    noiseWipe(body, offset);
    return sent;
}

bool BitchatBridgeModule::sendPrivate(PrivateSession &session, uint8_t kind, Bytes body)
{
    if (!session.ready || body.size + 1 > PRIVATE_LIMIT || !session.outgoing.admit(millis(), 10, 1000))
        return false;
    uint8_t plain[PRIVATE_LIMIT];
    uint8_t sealed[PRIVATE_LIMIT + NOISE_TRANSPORT_OVERHEAD];
    plain[0] = kind;
    std::memcpy(plain + 1, body.data, body.size);
    const bool ok = noiseEncrypt(session.send, {plain, body.size + 1}, sealed) &&
                    sendNoise(TYPE_ENCRYPTED, session.peer, {sealed, body.size + 1 + NOISE_TRANSPORT_OVERHEAD}, session.link);
    noiseWipe(plain, body.size + 1);
    return ok;
}

// Unsigned and addressed to one phone, as iOS sends them. Padded as the apps pad these two types: the apps'
// stream reader takes their length as the padded one. Straight to the phone when it is connected here; else to
// every phone, which pass it on. Never tunneled: the copies the phones relay back carry this bridge's own ID and
// are dropped.
bool BitchatBridgeModule::sendNoise(uint8_t type, const PeerId &to, Bytes payload, uint16_t link)
{
    if (!identityReady || !utc())
        return false;
    const uint64_t timestamp = nextTimestamp();
    if (encode(type, ownId, &to, timestamp, 7, payload, {}, runtime->working, true) != Result::Ok)
        return false;
    const bool sent = (link != NO_LINK && bleSendTo(runtime->working.bytes(), link)) || bleBroadcast(runtime->working.bytes());
    if (!sent) {
        reject(Result::Full);
        return false;
    }
    ++originatedBle;
    return true;
}

// Stage 2, from Bitchat: "@name text" in the private chat, to the Meshtastic node of that name (long name, then
// short name; the longest that matches).
void BitchatBridgeModule::directToLoRa(PrivateSession &session, const Peer &peer, Bytes text, uint32_t now)
{
    const auto say = [this](const char *line) { replyPrivately(line, std::strlen(line)); };
    if (!directMessages()) {
        say("Mensagens privadas pelo LoRa estão desligadas neste rádio.");
        return;
    }
    const Bytes rest{text.data + 1, text.size - 1};
    const meshtastic_NodeInfoLite *best = nullptr;
    size_t bestSize = 0;
    bool twin = false;
    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); ++i) {
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || !node->has_user || node->num == nodeDB->getNodeNum())
            continue;
        for (const auto *name : {node->user.long_name, node->user.short_name}) {
            const size_t size =
                strnlen(name, name == node->user.long_name ? sizeof(node->user.long_name) : sizeof(node->user.short_name));
            if (!startsWithName(rest, name, size))
                continue;
            if (size > bestSize) {
                best = node;
                bestSize = size;
                twin = false;
            } else if (size == bestSize && best && node->num != best->num) {
                twin = true;
            }
            break;
        }
    }
    char line[LORA_TEXT_LIMIT + 64];
    if (!best) {
        size_t word = 0;
        while (word < rest.size && rest.data[word] != ' ' && rest.data[word] != ':')
            ++word;
        word = utf8Fit(reinterpret_cast<const char *>(rest.data), word, 30);
        snprintf(line, sizeof(line), "Não conheço \"%.*s\" entre os contatos deste rádio.", int(word), rest.data);
        say(line);
        return;
    }
    char name[LABEL_LIMIT + 1];
    size_t nameSize = cleanLabel(
        {reinterpret_cast<const uint8_t *>(best->user.long_name), strnlen(best->user.long_name, sizeof(best->user.long_name))},
        name, sizeof(name));
    if (!nameSize)
        nameSize = snprintf(name, sizeof(name), "!%08lx", static_cast<unsigned long>(best->num));
    if (twin) {
        snprintf(line, sizeof(line), "Há mais de um contato \"%.*s\"; mensagem não enviada.", int(bestSize), rest.data);
        say(line);
        return;
    }
    const Bytes body = afterName(rest, bestSize);
    if (!body.size) {
        say("Escreva o texto depois do nome: @contato texto.");
        return;
    }
    char label[LABEL_LIMIT + 8];
    const size_t labelSize = peerLabel(peer, label, sizeof(label));
    const int head = snprintf(line, sizeof(line), "%s%.*s] ", DIRECT_PREFIX, int(labelSize), label);
    if (head < 0 || size_t(head) + body.size > LORA_TEXT_LIMIT) {
        snprintf(line, sizeof(line), "Mensagem longa demais: até %u caracteres.",
                 unsigned(head < 0 ? 0 : LORA_TEXT_LIMIT - std::min<size_t>(LORA_TEXT_LIMIT, size_t(head))));
        say(line);
        return;
    }
    // Without the node's key the firmware refuses a direct message (no legacy direct messages).
    if (best->user.public_key.size != 32) {
        snprintf(line, sizeof(line), "%.*s ainda não tem chave pública; mensagem não enviada.", int(nameSize), name);
        say(line);
        return;
    }
    if (!session.direct.admit(now, 2, 20000) || !runtime->privates->directOverall.admit(now, 5, 180000)) {
        say("Aguarde um pouco: limite de mensagens pelo LoRa.");
        return;
    }
    std::memcpy(line + head, body.data, body.size);
    const size_t size = size_t(head) + body.size;
    auto *packet =
        (airTime && airTime->isTxAllowedAirUtil() && airTime->isTxAllowedChannelUtil(true) && router->getQueueStatus().free >= 2)
            ? router->allocForSending()
            : nullptr;
    if (!packet) {
        say("LoRa ocupado; tente de novo em instantes.");
        return;
    }
    packet->to = best->num;
    packet->channel = 0;
    packet->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packet->decoded.payload.size = size;
    std::copy_n(reinterpret_cast<const uint8_t *>(line), size, packet->decoded.payload.bytes);
    packet->want_ack = true;
    packet->priority = meshtastic_MeshPacket_Priority_DEFAULT;
    service->sendToMesh(packet, RX_SRC_LOCAL, true);
    ++submittedRadio;
    // The node's answers go back to this phone for a day.
    auto &conversations = runtime->privates->conversations;
    Conversation *slot = nullptr;
    for (auto &entry : conversations)
        if (entry.used && entry.node == best->num) {
            slot = &entry;
            break;
        }
    if (!slot)
        for (auto &entry : conversations)
            if (!slot || (slot->used && (!entry.used || int32_t(entry.at - slot->at) < 0)))
                slot = &entry;
    *slot = {best->num, session.peer, now, true};
    snprintf(line, sizeof(line), "Enviado a %.*s pelo LoRa.", int(nameSize), name);
    say(line);
}

// Stage 2, from LoRa: a direct message to this node. "@nickname text" goes to that Bitchat phone; otherwise to the
// phone that last wrote to this node through the bridge, for a day. The rest is the owner's own mail.
void BitchatBridgeModule::directFromLoRa(uint32_t from, Bytes text)
{
    if (!directMessages() || !text.size || !validUtf8(text))
        return;
    const uint32_t now = millis();
    PeerId target{};
    bool found = false;
    if (text.data[0] == '@') {
        const Bytes rest{text.data + 1, text.size - 1};
        size_t bestSize = 0;
        for (size_t i = 0; i < PeerTable::CAPACITY; ++i) {
            const Peer *peer = runtime->peers->slot(i);
            if (!peer || peer->announcement.gateway)
                continue;
            char label[LABEL_LIMIT + 8];
            const size_t labelSize = peerLabel(*peer, label, sizeof(label));
            const size_t nickSize = strnlen(peer->announcement.nickname.data(), peer->announcement.nickname.size());
            for (const auto &name : {std::make_pair(static_cast<const char *>(label), labelSize),
                                     std::make_pair(static_cast<const char *>(peer->announcement.nickname.data()), nickSize)})
                if (name.second > bestSize && startsWithName(rest, name.first, name.second)) {
                    bestSize = name.second;
                    target = peer->id;
                    found = true;
                }
        }
        if (found)
            text = afterName(rest, bestSize);
    }
    if (!found && runtime->privates)
        for (const auto &entry : runtime->privates->conversations)
            if (entry.used && entry.node == from && Throttle::isWithinTimespanMsAt(entry.at, KEEP_DAY_MS, now)) {
                target = entry.peer;
                found = true;
            }
    if (!found || !text.size)
        return;
    char name[LABEL_LIMIT + 1];
    const auto *node = nodeDB->getMeshNode(from);
    size_t nameSize = 0;
    if (node && node->has_user)
        nameSize = cleanLabel({reinterpret_cast<const uint8_t *>(node->user.long_name),
                               strnlen(node->user.long_name, sizeof(node->user.long_name))},
                              name, sizeof(name));
    if (!nameSize)
        nameSize = snprintf(name, sizeof(name), "!%08lx", static_cast<unsigned long>(from));
    char line[MAILBOX_TEXT + 1];
    const int head = snprintf(line, sizeof(line), "%s%.*s] ", IMPORT_PREFIX, int(nameSize), name);
    if (head < 0)
        return;
    const size_t body = utf8Fit(reinterpret_cast<const char *>(text.data), text.size, MAILBOX_TEXT - size_t(head));
    std::memcpy(line + head, text.data, body);
    ++importedRadio;
    deliverToPhone(target, line, size_t(head) + body, now);
}

void BitchatBridgeModule::deliverToPhone(const PeerId &peer, const char *text, size_t size, uint32_t now)
{
    if (!allocateBuffer(runtime->privates))
        return;
    if (PrivateSession *session = privateSession(peer, now, false)) {
        if (session->ready) {
            privateReply = session;
            const bool sent = replyPrivately(text, size);
            privateReply = nullptr;
            if (sent)
                return;
        }
    }
    // Kept for when the phone is back: the oldest gives way.
    auto &mailbox = runtime->privates->mailbox;
    MailboxEntry *slot = nullptr;
    for (auto &entry : mailbox)
        if (!slot || (slot->used && (!entry.used || int32_t(entry.at - slot->at) < 0)))
            slot = &entry;
    slot->peer = peer;
    slot->at = now;
    slot->size = uint16_t(std::min(size, slot->text.size()));
    std::memcpy(slot->text.data(), text, slot->size);
    slot->used = true;
    phoneArrived(peer, now);
}

void BitchatBridgeModule::flushMailbox(PrivateSession &session, uint32_t now)
{
    if (!runtime->privates || !session.ready)
        return;
    for (auto &entry : runtime->privates->mailbox) {
        if (!entry.used)
            continue;
        if (!Throttle::isWithinTimespanMsAt(entry.at, KEEP_DAY_MS, now)) {
            noiseWipe(&entry, sizeof(entry));
            continue;
        }
        if (entry.peer != session.peer)
            continue;
        privateReply = &session;
        const bool sent = replyPrivately(entry.text.data(), entry.size);
        privateReply = nullptr;
        if (!sent)
            return; // the rest waits for the next message or announce
        noiseWipe(&entry, sizeof(entry));
    }
}

void BitchatBridgeModule::phoneArrived(const PeerId &peer, uint32_t now)
{
    if (!runtime || !runtime->privates)
        return;
    bool mail = false;
    for (const auto &entry : runtime->privates->mailbox)
        mail |= entry.used && entry.peer == peer && Throttle::isWithinTimespanMsAt(entry.at, KEEP_DAY_MS, now);
    if (!mail)
        return;
    PrivateSession *session = privateSession(peer, now, true);
    if (!session)
        return;
    if (session->ready)
        flushMailbox(*session, now);
    else
        initiatePrivate(*session, now);
}
#endif

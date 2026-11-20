#ifdef MESHTASTIC_ENABLE_BITCHAT
// Cashu payments over LoRa. A phone near this radio posts a token in Bitchat's public chat; the bridge
// carries the payer's own frames, whole, to the bridges on the same Meshtastic channel, which give them to
// the phones near them. The bridge never holds, opens or spends a token: it only decides whether the
// token may travel, and tells the payer how it went. See docs/bridge/CASHU.md.
#include "BitchatBridgeModule.h"
#include "BridgeDefaults.h"
#include "BridgeDiagnostics.h"
#include "BridgeInflate.h"
#include "BridgePlatform.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "airtime.h"
#include "main.h"
#include <cstring>

using namespace bitchat;

namespace
{
// Text and token of one message, inflated: on the heap while it is read, as the stack is short.
struct CashuWork {
    uint8_t text[MAX_PAYLOAD];
    uint8_t scratch[CASHU_SCRATCH];
};

// A phone takes an announce up to two minutes old by its own clock, and the trip takes some of that: an
// older one is not sent, the bridge waits for the payer's next (phones announce every 15 to 40 s).
constexpr uint32_t ANNOUNCE_FRESH_MS = 30000;
constexpr uint32_t ANNOUNCE_WAIT_MS = 45000;
// After a refusal from a bridge, another one may still take the payment for this long.
constexpr uint32_t REFUSAL_GRACE_MS = 15000;

// At most `limit` bytes of UTF-8, cut between characters.
size_t clip(const char *text, size_t limit, char *out, size_t capacity)
{
    size_t n = std::min(strlen(text), std::min(limit, capacity - 1));
    while (n && (uint8_t(text[n]) & 0xc0) == 0x80)
        --n;
    memcpy(out, text, n);
    out[n] = 0;
    return n;
}

// "21 sat"
void amountText(uint64_t amount, const char *unit, char *out, size_t capacity)
{
    char digits[21];
    size_t n = 0;
    do {
        digits[n++] = char('0' + amount % 10);
        amount /= 10;
    } while (amount && n < sizeof(digits));
    size_t at = 0;
    while (n && at + 1 < capacity)
        out[at++] = digits[--n];
    snprintf(out + at, capacity - at, " %s", unit[0] ? unit : "sat");
}

// A frame without the padding the sender may have added: phones read both, and LoRa airtime is dear.
Bytes natural(Bytes frame, const Packet &packet)
{
    return {frame.data, std::min(frame.size, packet.naturalSize)};
}
} // namespace

// A line to every phone, "[bridge] " in front, cut between characters to fit.
void BitchatBridgeModule::say(const char *text)
{
    char clipped[BITCHAT_TEXT_LIMIT];
    tellBitchat(clipped, clip(text, BITCHAT_TEXT_LIMIT - sizeof(REPLY_PREFIX), clipped, sizeof(clipped)));
}

// Whether text on the bridge's LoRa channel stays among those given its key. The bridge's public channel
// has a key everyone has (it is in this firmware), and only 32-byte keys can be chosen as the bridge's.
bool BitchatBridgeModule::channelPrivate() const
{
    if (settings.textChannel == 255 || settings.textChannel >= channels.getNumChannels())
        return false;
    const auto &psk = channels.getByIndex(settings.textChannel).settings.psk;
    return psk.size == 32 && !std::equal(PUBLIC_CHANNEL_KEY.begin(), PUBLIC_CHANNEL_KEY.end(), psk.bytes);
}

void BitchatBridgeModule::rememberAnnounce(const Packet &packet, Bytes frame)
{
    const Bytes kept = natural(frame, packet);
    if (!allocateBuffer(runtime->pay) || kept.size > runtime->pay->announced[0].frame.size())
        return;
    auto *slot = &runtime->pay->announced[0];
    for (auto &entry : runtime->pay->announced) {
        if (entry.size && entry.peer == packet.sender) {
            slot = &entry;
            break;
        }
        if (millis() - entry.at > millis() - slot->at || !entry.size)
            slot = &entry;
    }
    slot->peer = packet.sender;
    slot->size = uint16_t(kept.size);
    slot->at = millis();
    std::copy_n(kept.data, kept.size, slot->frame.begin());
    if (payment.holding && payment.sender == packet.sender && runtime->held && runtime->payOut)
        startPayment(kept, runtime->held->bytes());
}

bool BitchatBridgeModule::carryCashu(const Packet &packet, Bytes frame, const Peer &peer, const Digest &id)
{
    if (!packet.compressed() && !mentionsCashu(packet.payload))
        return false;
    std::unique_ptr<CashuWork> work;
    if (!allocateBuffer(work)) {
        // Unread, a token could go out as pieces of text: an uncompressed message that names one stays here.
        reject(Result::Full);
        return !packet.compressed();
    }
    Bytes text;
    if (!messageText(packet, work->text, sizeof(work->text), text))
        return false;
    CashuToken token;
    const CashuFound found = findCashu(text, work->scratch, token);
    work.reset();
    if (found == CashuFound::None)
        return false;
    // Without a LoRa channel the payment simply stays in this room.
    if (!textReady || textConflict || seen(id))
        return true;
    // Refusals are spaced out: a phone repeating a bad token gets one answer, not a flood of them.
    const auto tell = [&](const char *message) {
        if (paymentReplies.admit(millis(), 3000))
            say(message);
    };
    if (found == CashuFound::Malformed) {
        tell("Token Cashu ilegível: não foi levado pela LoRa.");
        return true;
    }
    if (found == CashuFound::Several) {
        tell("Um token por mensagem: nada foi levado pela LoRa.");
        return true;
    }
    // The phones near the other bridge read Bitchat's public chat too: there a token anyone may spend is
    // theirs for the taking. Only a key-locked token goes over a channel whose key is public.
    if (token.unlocked() && !channelPrivate()) {
        tell("Token sem trava P2PK: num canal aberto outros poderiam gastá-lo. Não foi pela LoRa.");
        return true;
    }
    if ((runtime->payOut && runtime->payOut->active()) || payment.waiting || payment.holding) {
        tell("A ponte já leva outro pagamento: reenvie em 2 min.");
        return true;
    }
    if (!allocateBuffer(runtime->payOut)) {
        tell("Sem memória para o pagamento agora: reenvie em 1 min.");
        return true;
    }
    const Bytes message = natural(frame, packet);
    if (4 + message.size > PAY_BUNDLE - 120) {
        tell("Token grande demais para a LoRa: junte as provas na carteira e reenvie.");
        return true;
    }
    payment = {};
    payment.id = read32(id.data());
    payment.amount = token.amount;
    snprintf(payment.unit, sizeof(payment.unit), "%s", token.unit);
    char label[LABEL_LIMIT + 8];
    peerLabel(peer, label, sizeof(label));
    clip(label, 14, payment.payer, sizeof(payment.payer));
    payment.sender = packet.sender;
    payment.messageId = id;
    // Every limit counts from when the payer's phone signed the message: phones check that time.
    const uint64_t now = utc();
    payment.signedAt = millis() - uint32_t(packet.timestamp < now ? std::min<uint64_t>(now - packet.timestamp, 120000) : 0);
    if (runtime->pay)
        for (const auto &entry : runtime->pay->announced)
            if (entry.size && entry.peer == packet.sender && millis() - entry.at < ANNOUNCE_FRESH_MS) {
                startPayment({entry.frame.data(), entry.size}, message);
                return true;
            }
    // Held until the payer announces again.
    if (!allocateBuffer(runtime->held)) {
        tell("Sem memória para o pagamento agora: reenvie em 1 min.");
        return true;
    }
    std::copy_n(message.data, message.size, runtime->held->data.begin());
    runtime->held->size = message.size;
    payment.holding = true;
    payment.heldAt = millis();
    return true;
}

void BitchatBridgeModule::startPayment(Bytes announce, Bytes message)
{
    payment.holding = false;
    // Too little of the two minutes left for the trip and a resend: better to say so now.
    if (!Throttle::isWithinTimespanMsAt(payment.signedAt, 70000, millis())) {
        say("Seu celular demorou a se anunciar: nada foi levado. Reenvie o token.");
        runtime->held.reset();
        return;
    }
    if (!runtime->payOut->start(payment.id, announce, message, payment.signedAt)) {
        say("Token grande demais para a LoRa: junte as provas na carteira e reenvie.");
        runtime->held.reset();
        return;
    }
    runtime->held.reset();
    // Remembered across a restart, so the same message is never carried twice.
    record(payment.messageId);
    payment.at = payment.signedAt;
    payment.channel = settings.textChannel;
    payment.binding = settings.textBinding;
    payment.waiting = true;
    char amount[32], line[160];
    amountText(payment.amount, payment.unit, amount, sizeof(amount));
    snprintf(line, sizeof(line), "Levando %s de %s pela LoRa (%u partes)…", amount, payment.payer,
             unsigned(runtime->payOut->count()));
    say(line);
}

// The payer is told in one line, the outcome first: Bitchat text from the bridge stays under 96 bytes.
// A bridge is named only when it answered by a direct message Meshtastic could check (PKI); otherwise it
// is the node number it came from, marked as not checked.
void BitchatBridgeModule::reportPayment()
{
    char amount[32], who[24], line[160];
    amountText(payment.amount, payment.unit, amount, sizeof(amount));
    if (payment.trusted)
        snprintf(who, sizeof(who), "%s", payment.bridge);
    else
        snprintf(who, sizeof(who), "!%04x", unsigned(payment.node & 0xffff));
    if (!payment.answered)
        snprintf(line, sizeof(line), "✗ %s sem confirmação. Se não chegou, o token segue com você.", amount);
    else if (payment.outcome == PayOutcome::Delivered && payment.trusted)
        snprintf(line, sizeof(line), "✓ %s entregue à ponte %s (%u cel.). Confirme com quem recebe.", amount, who,
                 unsigned(payment.phones));
    else if (payment.outcome == PayOutcome::Delivered)
        snprintf(line, sizeof(line), "✓? %s entregue, diz a ponte %s (não conferida). Confirme com quem recebe.", amount, who);
    else if (payment.outcome == PayOutcome::NoPhones)
        snprintf(line, sizeof(line), "✗ %s: ponte %s sem celular perto. O token segue com você.", amount, who);
    else if (payment.outcome == PayOutcome::Late)
        snprintf(line, sizeof(line), "✗ %s chegou tarde à ponte %s. O token segue com você.", amount, who);
    else
        snprintf(line, sizeof(line), "✗ A ponte %s recusou %s. O token segue com você.", who, amount);
    say(line);
    payment.waiting = false;
    if (runtime && runtime->payOut)
        runtime->payOut->finish();
}

void BitchatBridgeModule::paymentAnswered(const PayHeard &heard, uint32_t from, bool pki, uint32_t now)
{
    if (!payment.waiting || heard.id != payment.id)
        return;
    if (heard.kind == PayKind::Want) {
        if (runtime->payOut)
            runtime->payOut->resend(heard.missing, now);
        return;
    }
    // A direct message Meshtastic decrypted with the sender's key (PKI) comes from that node; its name is
    // the one this radio knows. A broadcast proves nothing.
    char name[40] = "";
    const auto *node = nodeDB->getMeshNode(from);
    const bool trusted = pki && node && node->has_user;
    if (trusted)
        snprintf(name, sizeof(name), "%s", node->user.long_name);
    if (!name[0])
        snprintf(name, sizeof(name), "!%08lx", static_cast<unsigned long>(from));
    // A checked answer beats an unchecked one, which anyone on the channel could write; then delivered
    // beats refused.
    const bool delivered = heard.outcome == PayOutcome::Delivered;
    const uint8_t rank = uint8_t((trusted ? 2 : 0) + (delivered ? 1 : 0) + 1);
    if (rank > payment.rank) {
        payment.rank = rank;
        payment.outcome = heard.outcome;
        payment.phones = heard.phones;
        payment.trusted = trusted;
        payment.node = from;
        clip(name, 14, payment.bridge, sizeof(payment.bridge));
    }
    if (!payment.answered) {
        payment.answered = true;
        payment.answeredAt = now;
    }
    // Only a delivery a known node confirmed ends the wait; another bridge may still have done better.
    if (delivered && trusted)
        reportPayment();
}

bool BitchatBridgeModule::payDue() const
{
    return (runtime->pay && runtime->pay->control.size()) || (runtime->payOut && runtime->payOut->hasPending());
}

void BitchatBridgeModule::watchPayment(uint32_t now)
{
    if (runtime->payIn) {
        Runtime::PayItem want;
        if (allocateBuffer(runtime->pay) && runtime->pay->control.size() < 3 && runtime->payIn->want(now, want.datagram))
            runtime->pay->control.push(want);
        runtime->payIn->expire(now);
        if (runtime->payIn->empty())
            runtime->payIn.reset();
    }
    if (payment.holding && !Throttle::isWithinTimespanMsAt(payment.heldAt, ANNOUNCE_WAIT_MS, now)) {
        payment.holding = false;
        runtime->held.reset();
        say("Seu celular não se anunciou a tempo: nada foi levado. Reenvie o token.");
    }
    if (runtime->payOut && !runtime->payOut->active() && !payment.holding)
        runtime->payOut.reset();
    if (!payment.waiting)
        return;
    if (!runtime || !runtime->payOut || !runtime->payOut->active()) {
        payment.waiting = false;
        return;
    }
    // The bridge was moved to another channel, perhaps one anyone may read: what is left stays here.
    if (runtime->payOut->hasPending() &&
        (settings.textChannel != payment.channel || settings.textBinding != payment.binding || !textReady)) {
        runtime->payOut->finish();
        payment.waiting = false;
        say("✗ O canal da ponte mudou: o pagamento não foi levado inteiro. O token segue com você.");
        return;
    }
    if ((payment.answered && !Throttle::isWithinTimespanMsAt(payment.answeredAt, REFUSAL_GRACE_MS, now)) ||
        !Throttle::isWithinTimespanMsAt(payment.at, PAY_WAIT_MS, now))
        reportPayment();
}

// Parts and answers share the bridge's airtime and spacing; a part waits behind the bridge's text, an answer
// goes first.
bool BitchatBridgeModule::sendPay()
{
    PayDatagram datagram;
    uint32_t to = NODENUM_BROADCAST;
    bool control = false;
    if (auto *queued = runtime->pay ? runtime->pay->control.front() : nullptr) {
        datagram = queued->datagram;
        to = queued->to;
        control = true;
    } else if (!runtime->payOut || !runtime->payOut->next(datagram)) {
        return false;
    }
    const auto unsend = [&]() {
        // Not sent: a part goes back to the ones still to send.
        if (!control && runtime->payOut)
            runtime->payOut->resend(uint16_t(1u << datagram.data[7]), runtime->payOut->startedAt());
    };
    auto *packet = router->allocForSending();
    if (!packet) {
        unsend();
        reject(Result::Full);
        return false;
    }
    packet->to = to;
    packet->channel = settings.textChannel;
    packet->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    packet->decoded.payload.size = datagram.size;
    std::copy_n(datagram.data.begin(), datagram.size, packet->decoded.payload.bytes);
    packet->want_ack = false;
    packet->priority = meshtastic_MeshPacket_Priority_DEFAULT;
    const uint32_t cost = router->bridgeAirtime(packet);
    if (cost > tokens || router->getQueueStatus().free < 2) {
        packetPool.release(packet);
        unsend();
        return false;
    }
    tokens -= cost;
    if (control)
        runtime->pay->control.pop();
    service->sendToMesh(packet, RX_SRC_LOCAL, true);
    return true;
}

// A part of a payment or an answer about one. `direct`: sent to this node; `pki`: Meshtastic decrypted it
// with the sender's key.
void BitchatBridgeModule::payData(uint32_t from, bool direct, bool pki, Bytes data)
{
    const uint32_t now = millis();
    PayHeard heard;
    if (data.data[2] & 0x0f) {
        // An answer about a payment: only the bridge that sent it cares.
        if (readPayAnswer(data, heard))
            paymentAnswered(heard, from, pki && direct, now);
        return;
    }
    if (direct)
        return;
    if (!allocateBuffer(runtime->payIn)) {
        reject(Result::Full);
        return;
    }
    if (runtime->payIn->accept(data, now, heard) && heard.complete)
        deliverPayment(heard, from);
    if (runtime->payIn->empty())
        runtime->payIn.reset();
}

PayOutcome BitchatBridgeModule::checkPayment(const PayHeard &heard, uint64_t &amount, char *unit, size_t unitSize, char *payer,
                                             size_t payerSize)
{
    const uint32_t now = millis();
    Packet announced, message;
    if (!heard.announce.size || decode(heard.announce, announced) != Result::Ok || announced.type != 1 ||
        decode(heard.message, message) != Result::Ok || message.type != 2 || message.directed() ||
        message.sender != announced.sender)
        return PayOutcome::Invalid;
    Peer *peer = nullptr;
    Result result = runtime->peers->authenticate(crypto, heard.announce, utc(), now, peer);
    if (result == Result::Ok && peer && !peer->announcement.gateway)
        result = runtime->peers->authenticate(crypto, heard.message, utc(), now, peer);
    else if (result == Result::Ok)
        result = Result::Policy;
    if (result == Result::Expired)
        return PayOutcome::Late;
    if (result != Result::Ok || !peer)
        return PayOutcome::Invalid;
    // The same rule as where it came from: only a token, and only a locked one on a channel anyone may read.
    std::unique_ptr<CashuWork> work;
    Bytes text;
    CashuToken token;
    if (!allocateBuffer(work) || !messageText(message, work->text, sizeof(work->text), text) ||
        findCashu(text, work->scratch, token) != CashuFound::Token || (token.unlocked() && !channelPrivate()))
        return PayOutcome::Invalid;
    amount = token.amount;
    snprintf(unit, unitSize, "%s", token.unit);
    char label[LABEL_LIMIT + 8];
    peerLabel(*peer, label, sizeof(label));
    clip(label, 14, payer, payerSize);
    return PayOutcome::Delivered;
}

void BitchatBridgeModule::deliverPayment(const PayHeard &heard, uint32_t from)
{
    const uint32_t now = millis();
    uint64_t amount = 0;
    char unit[13] = "sat", payer[LABEL_LIMIT + 1] = "";
    PayOutcome outcome = checkPayment(heard, amount, unit, sizeof(unit), payer, sizeof(payer));
    Digest announceId, messageId;
    if (outcome == PayOutcome::Delivered && (packetDigest(crypto, heard.announce, announceId) != Result::Ok ||
                                             packetDigest(crypto, heard.message, messageId) != Result::Ok))
        outcome = PayOutcome::Invalid;
    const uint8_t phones = uint8_t(std::min<size_t>(bleListeners(), 255));
    if (outcome == PayOutcome::Delivered && !phones)
        outcome = PayOutcome::NoPhones;
    if (outcome == PayOutcome::Delivered && !runtime->dedup->contains(messageId, now, 125000)) {
        // The announce first: a phone shows a message only from someone it knows.
        for (const Bytes frame : {heard.announce, heard.message}) {
            const Bytes out = relay(frame, runtime->working) == Result::Ok ? runtime->working.bytes() : frame;
            if (!bleBroadcast(out))
                outcome = PayOutcome::NoPhones;
        }
        // Phones relay what they hear, so the copies coming back are dropped as any other.
        if (outcome == PayOutcome::Delivered) {
            runtime->dedup->insert(announceId, now, 125000);
            runtime->dedup->insert(messageId, now, 125000);
        }
        if (outcome == PayOutcome::Delivered) {
            char text[32], line[160];
            amountText(amount, unit, text, sizeof(text));
            snprintf(line, sizeof(line), "%s de %s chegaram pela LoRa. Toque no token para resgatar.", text, payer);
            say(line);
        }
    }
    // Parts that did not hold together may be someone else's: the true ones can still complete it.
    if (outcome == PayOutcome::Invalid && runtime->payIn)
        runtime->payIn->forget(heard.id);
    // To the payer's bridge as a direct message: Meshtastic encrypts it with both nodes' keys (PKI) when it
    // knows the other's, which proves who answered. Without that key a broadcast still tells the outcome.
    const auto *payerNode = nodeDB->getMeshNode(from);
    const bool direct = payerNode && payerNode->user.public_key.size == 32 && config.security.private_key.size == 32;
    uint8_t key[8]{};
    write32(key, nodeDB->getNodeNum());
    Runtime::PayItem ack;
    makePayAck(heard.id, outcome, phones, key, nullptr, ack.datagram);
    if (!allocateBuffer(runtime->pay)) {
        reject(Result::Full);
        return;
    }
    // The payer's bridge may not know this node's key yet and then cannot read the direct message: the
    // broadcast goes too, and the checked answer wins when both arrive.
    if (direct) {
        ack.to = from;
        if (!runtime->pay->control.push(ack))
            reject(Result::Full);
    }
    ack.to = NODENUM_BROADCAST;
    if (!runtime->pay->control.push(ack))
        reject(Result::Full);
}

#endif

#include "BridgePayTunnel.h"

#include "mesh/Throttle.h"
#include <algorithm>
#include <cstring>

namespace bitchat
{

static void header(PayKind kind, uint32_t id, PayDatagram &out)
{
    out.data[0] = 'B';
    out.data[1] = 'C';
    out.data[2] = uint8_t(PAY_VERSION << 4 | uint8_t(kind));
    write32(out.data.data() + 3, id);
}

bool payDatagram(Bytes data)
{
    return data.size >= 7 && data.size <= PAY_DATAGRAM && data.data[0] == 'B' && data.data[1] == 'C' &&
           (data.data[2] >> 4) == PAY_VERSION;
}

size_t payParts(size_t bundleSize)
{
    return (bundleSize + PAY_CHUNK - 1) / PAY_CHUNK;
}

void payAckSigned(uint32_t id, PayOutcome outcome, uint8_t phones, const uint8_t key[8], const uint8_t messageHash[8],
                  uint8_t (&out)[PAY_ACK_SIGNED])
{
    out[0] = 'B';
    out[1] = 'C';
    out[2] = 'A';
    write32(out + 3, id);
    out[7] = uint8_t(outcome);
    out[8] = phones;
    memcpy(out + 9, key, 8);
    memcpy(out + 17, messageHash, 8);
}

bool makePayAck(uint32_t id, PayOutcome outcome, uint8_t phones, const uint8_t key[8], const uint8_t *signature, PayDatagram &out)
{
    header(PayKind::Ack, id, out);
    out.data[7] = uint8_t(outcome);
    out.data[8] = phones;
    // An older bridge reads the first four bytes of the key as the node number, which they are.
    memcpy(out.data.data() + 9, key, signature ? 8 : 4);
    out.size = 13;
    if (signature) {
        memcpy(out.data.data() + 17, signature, 64);
        out.size = PAY_ACK_SIZE;
    }
    return true;
}

bool makePayWant(uint32_t id, uint16_t missing, PayDatagram &out)
{
    header(PayKind::Want, id, out);
    write16(out.data.data() + 7, missing);
    out.size = 9;
    return true;
}

bool PaySender::start(uint32_t id, Bytes announce, Bytes message, uint32_t now)
{
    const size_t total = 4 + announce.size + message.size;
    if (used || !announce.size || !message.size || announce.size > 0xffff || message.size > 0xffff || total > PAY_BUNDLE)
        return false;
    write16(bundle.data(), uint16_t(announce.size));
    memcpy(bundle.data() + 2, announce.data, announce.size);
    write16(bundle.data() + 2 + announce.size, uint16_t(message.size));
    memcpy(bundle.data() + 4 + announce.size, message.data, message.size);
    size = total;
    parts = uint8_t(payParts(total));
    pending = uint16_t((1u << parts) - 1);
    bundleId = id;
    started = now;
    used = true;
    return true;
}

bool PaySender::next(PayDatagram &out)
{
    if (!used || !pending)
        return false;
    uint8_t index = 0;
    while (!(pending & (1u << index)))
        ++index;
    pending &= uint16_t(~(1u << index));
    const size_t offset = index * PAY_CHUNK;
    const size_t length = std::min(PAY_CHUNK, size - offset);
    header(PayKind::Data, bundleId, out);
    out.data[7] = index;
    out.data[8] = parts;
    memcpy(out.data.data() + PAY_HEADER, bundle.data() + offset, length);
    out.size = PAY_HEADER + length;
    return true;
}

void PaySender::resend(uint16_t missing, uint32_t now)
{
    // Parts that would arrive after the phones stop taking the frame are not worth the airtime.
    if (used && Throttle::isWithinTimespanMsAt(started, PAY_WAIT_MS - 20000, now))
        pending |= uint16_t(missing & ((1u << parts) - 1));
}

bool readPayAnswer(Bytes data, PayHeard &out)
{
    out = {};
    if (!payDatagram(data))
        return false;
    out.kind = PayKind(data.data[2] & 0x0f);
    out.id = read32(data.data + 3);
    if (out.kind == PayKind::Ack) {
        if (data.size < 13 || data.data[7] > uint8_t(PayOutcome::Invalid))
            return false;
        out.outcome = PayOutcome(data.data[7]);
        out.phones = data.data[8];
        out.node = read32(data.data + 9);
        if (data.size >= PAY_ACK_SIZE) {
            memcpy(out.key.data(), data.data + 9, 8);
            memcpy(out.signature.data(), data.data + 17, 64);
            out.signedAck = true;
        } else {
            memcpy(out.key.data(), data.data + 9, 4);
        }
        return true;
    }
    if (out.kind == PayKind::Want && data.size >= 9) {
        out.missing = read16(data.data + 7);
        return out.missing != 0;
    }
    return false;
}

bool PayReceiver::accept(Bytes data, uint32_t now, PayHeard &out)
{
    out = {};
    if (!payDatagram(data))
        return false;
    if ((data.data[2] & 0x0f) != uint8_t(PayKind::Data))
        return readPayAnswer(data, out);
    out.id = read32(data.data + 3);
    if (data.size <= PAY_HEADER)
        return false;
    const uint8_t index = data.data[7], count = data.data[8];
    const size_t length = data.size - PAY_HEADER;
    // Every part but the last fills its chunk, so the bundle size follows from the last one.
    if (!count || count > PAY_PARTS || index >= count || (index + 1 < count && length != PAY_CHUNK))
        return false;
    expire(now);
    Slot *slot = nullptr;
    for (auto &s : slots)
        if (s.used && s.id == out.id && s.count == count)
            slot = &s;
    if (!slot) {
        for (auto &s : slots)
            if (!s.used) {
                slot = &s;
                break;
            }
        if (!slot) {
            // Both busy: the older gives way, as it is the likelier to fail anyway.
            slot = (now - slots[0].firstAt) >= (now - slots[1].firstAt) ? &slots[0] : &slots[1];
        }
        *slot = {};
        slot->used = true;
        slot->id = out.id;
        slot->count = count;
        slot->firstAt = now;
    }
    if (slot->done || (slot->have & (1u << index)))
        return false;
    memcpy(slot->data.data() + index * PAY_CHUNK, data.data + PAY_HEADER, length);
    slot->have |= uint16_t(1u << index);
    slot->lastAt = now;
    if (index + 1 == count)
        slot->size = uint16_t(index * PAY_CHUNK + length);
    const uint16_t all = uint16_t((1u << count) - 1);
    if (slot->have != all)
        return false;
    slot->done = true;
    // Whole: the sizes inside must account for every byte, or the bundle is reported as invalid (empty).
    out.complete = true;
    const uint8_t *b = slot->data.data();
    const size_t announce = read16(b);
    if (announce == 0 || 4 + announce > slot->size)
        return true;
    const size_t message = read16(b + 2 + announce);
    if (message == 0 || 4 + announce + message != slot->size)
        return true;
    out.announce = {b + 2, announce};
    out.message = {b + 4 + announce, message};
    return true;
}

bool PayReceiver::want(uint32_t now, PayDatagram &out)
{
    for (auto &s : slots) {
        if (!s.used || s.done || s.wants >= PAY_WANTS || Throttle::isWithinTimespanMsAt(s.lastAt, PAY_QUIET_MS, now))
            continue;
        ++s.wants;
        s.lastAt = now;
        return makePayWant(s.id, uint16_t(~s.have & ((1u << s.count) - 1)), out);
    }
    return false;
}

void PayReceiver::expire(uint32_t now)
{
    for (auto &s : slots)
        if (s.used && !Throttle::isWithinTimespanMsAt(s.firstAt, PAY_WAIT_MS + 20000, now))
            s = {};
}

void PayReceiver::forget(uint32_t id)
{
    for (auto &s : slots)
        if (s.used && s.id == id)
            s = {};
}

bool PayReceiver::empty() const
{
    return !slots[0].used && !slots[1].used;
}

} // namespace bitchat

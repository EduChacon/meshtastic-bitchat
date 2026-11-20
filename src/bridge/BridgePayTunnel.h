#pragma once

#include "BridgeCodec.h"
#include <array>

// The Cashu tunnel between bridges. A payment is the payer's own signed Bitchat frames (the latest
// announce and the message holding the token), carried verbatim as Meshtastic PRIVATE_APP packets on the
// bridge's text channel and given to the phones near the other bridge as if the payer were there.
// Nothing is changed on the way, so the signatures still hold, and nothing is shown before every part
// arrived: a payment is whole or absent. The same format as the MeshCore bridge's, with larger parts.
//
// Datagram: 'B' 'C', version (high nibble) and kind (low nibble), bundle id (4 bytes), then per kind
//   data  index, count, chunk
//   ack   outcome, phones, node number of the receiving bridge (4 bytes). Sent as a direct message,
//         which Meshtastic encrypts with the two nodes' keys when it knows them (PKI): that proves the
//         sender. A broadcast ack proves nothing. The 81-byte signed form is MeshCore's.
//   want  missing parts as a 16-bit mask
// Bundle: announce size (2 bytes, big endian), announce, message size (2 bytes), message.

namespace bitchat
{

constexpr uint8_t PAY_VERSION = 1;
constexpr size_t PAY_DATAGRAM = 200; // below Meshtastic's 233-byte payload, with room for PKI
constexpr size_t PAY_HEADER = 9;
constexpr size_t PAY_CHUNK = PAY_DATAGRAM - PAY_HEADER;
constexpr size_t PAY_PARTS = 12;
constexpr size_t PAY_BUNDLE = PAY_PARTS * PAY_CHUNK;
// A phone checks a frame's time against its own clock with two minutes' tolerance: the whole trip,
// resends included, has to fit inside that.
constexpr uint32_t PAY_WAIT_MS = 100000;
// Without a new part for this long the receiver asks once more for what it lacks.
constexpr uint32_t PAY_QUIET_MS = 12000;
constexpr uint8_t PAY_WANTS = 2;

enum class PayKind : uint8_t { Data = 0, Ack = 1, Want = 2 };

enum class PayOutcome : uint8_t {
    Delivered = 0, // given to the phones near the receiving bridge
    NoPhones = 1,  // whole and valid, but no phone was there to take it
    Late = 2,      // arrived after the phones would accept it
    Invalid = 3    // signatures or format did not hold
};

struct PayDatagram {
    std::array<uint8_t, PAY_DATAGRAM> data{};
    size_t size = 0;
    Bytes bytes() const { return {data.data(), size}; }
};

// What one datagram meant.
struct PayHeard {
    PayKind kind = PayKind::Data;
    uint32_t id = 0;
    // data: the bundle is now whole; announce and message stay empty when its sizes do not add up
    bool complete = false;
    Bytes announce{}, message{};
    // ack
    PayOutcome outcome = PayOutcome::Delivered;
    uint8_t phones = 0;
    uint32_t node = 0; // the node number
    std::array<uint8_t, 8> key{};
    bool signedAck = false;
    std::array<uint8_t, 64> signature{};
    // want
    uint16_t missing = 0;
};

bool payDatagram(Bytes data);
// An ack or a want; false for data and anything malformed.
bool readPayAnswer(Bytes data, PayHeard &out);
size_t payParts(size_t bundleSize);
constexpr size_t PAY_ACK_SIGNED = 25;
constexpr size_t PAY_ACK_SIZE = 17 + 64;
// What the receiving radio signs: "BCA", bundle id, outcome, phones, its key prefix, and the first 8
// bytes of the SHA-256 of the message frame it got, so an ack fits one payment only.
void payAckSigned(uint32_t id, PayOutcome outcome, uint8_t phones, const uint8_t key[8], const uint8_t messageHash[8],
                  uint8_t (&out)[PAY_ACK_SIGNED]);
// `signature` may be null for an unsigned ack.
bool makePayAck(uint32_t id, PayOutcome outcome, uint8_t phones, const uint8_t key[8], const uint8_t *signature,
                PayDatagram &out);
bool makePayWant(uint32_t id, uint16_t missing, PayDatagram &out);

// One payment on its way out.
class PaySender
{
  public:
    bool start(uint32_t id, Bytes announce, Bytes message, uint32_t now);
    bool active() const { return used; }
    uint32_t id() const { return bundleId; }
    uint8_t count() const { return parts; }
    uint32_t startedAt() const { return started; }
    bool hasPending() const { return pending != 0; }
    // The next part to send; false when none is due.
    bool next(PayDatagram &out);
    // Parts the receiver asked for are sent again, while there is time.
    void resend(uint16_t missing, uint32_t now);
    void finish() { used = false; }

  private:
    std::array<uint8_t, PAY_BUNDLE> bundle{};
    size_t size = 0;
    uint32_t bundleId = 0, started = 0;
    uint16_t pending = 0;
    uint8_t parts = 0;
    bool used = false;
};

// Parts heard from other bridges, kept until the bundle is whole or too old.
class PayReceiver
{
  public:
    // A datagram from the channel. A whole bundle is returned once; later copies of its parts are not.
    bool accept(Bytes data, uint32_t now, PayHeard &out);
    // A want for a bundle still incomplete and quiet; false when nothing is to be asked now.
    bool want(uint32_t now, PayDatagram &out);
    void expire(uint32_t now);
    // Drops a bundle, done or not, so its parts may arrive again.
    void forget(uint32_t id);
    bool empty() const;

  private:
    struct Slot {
        std::array<uint8_t, PAY_BUNDLE> data{};
        uint32_t id = 0, firstAt = 0, lastAt = 0;
        uint16_t have = 0, size = 0;
        uint8_t count = 0, wants = 0;
        bool used = false, done = false;
    };
    std::array<Slot, 2> slots{};
};

} // namespace bitchat

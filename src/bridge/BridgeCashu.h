#pragma once

#include "BridgeCodec.h"

// Cashu ecash tokens in Bitchat text (NUT-00 "cashuA" JSON and "cashuB" CBOR), read far enough to decide
// whether a bridge may carry them: the amount, the mint and whether every proof is locked to a key
// (NUT-10/NUT-11 P2PK). Nothing here talks to a mint; the bridge never holds or spends a token.

namespace bitchat
{

// Base64 of the largest token a message can hold, decoded.
constexpr size_t CASHU_SCRATCH = 1400;

enum class CashuFound : uint8_t {
    None,      // no token in the text
    Token,     // one token, read
    Malformed, // looks like a token but does not decode
    Several    // more than one token in one message
};

struct CashuToken {
    char version = 0; // 'A' or 'B'
    uint64_t amount = 0;
    char unit[13]{};
    char mint[49]{}; // host of the mint
    uint16_t proofs = 0;
    // Proofs only the holder of a key can spend: P2PK, and either no locktime or a refund key for after it.
    uint16_t locked = 0;
    bool unlocked() const { return locked < proofs; }
};

// Whether the text names a token at all ("cashuA"/"cashuB"), read or not.
bool mentionsCashu(Bytes text);
// Whether text could be a token or a piece of one: it names one, or holds 40 or more base64url
// characters in a row, with capitals, small letters and digits among them (a token cut where the name
// was left in another message).
bool tokenLike(Bytes text);
CashuFound findCashu(Bytes text, uint8_t (&scratch)[CASHU_SCRATCH], CashuToken &out);

// The secret of one proof: whether it is P2PK and stays locked after any locktime.
bool lockedSecret(Bytes secret);

} // namespace bitchat

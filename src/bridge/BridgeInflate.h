#pragma once

#include "BridgeCodec.h"

namespace bitchat
{

// Raw DEFLATE (RFC 1951), as the Bitchat app compresses payloads of 100 bytes or more. Decoding stops
// with false on any malformed stream or when the output would pass `capacity`.
bool inflateRaw(Bytes input, uint8_t *out, size_t capacity, size_t &produced);

// The text of a public message, inflated when the app compressed it. `out` holds MAX_PAYLOAD bytes.
bool messageText(const Packet &packet, uint8_t *out, size_t capacity, Bytes &text);

} // namespace bitchat

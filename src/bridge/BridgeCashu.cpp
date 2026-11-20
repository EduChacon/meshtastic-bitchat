#include "BridgeCashu.h"

#include <algorithm>
#include <cstring>

namespace bitchat
{
namespace
{

constexpr uint64_t MAX_AMOUNT = 2100000000000000ULL;
constexpr size_t SECRET_LIMIT = 512;
constexpr unsigned DEPTH_LIMIT = 12;

bool tokenChar(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '+' ||
           c == '/' || c == '=';
}

int base64Value(uint8_t c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '-' || c == '+')
        return 62;
    if (c == '_' || c == '/')
        return 63;
    return -1;
}

// Base64 or base64url, padded or not.
bool base64Decode(Bytes in, uint8_t *out, size_t capacity, size_t &size)
{
    size = 0;
    uint32_t acc = 0;
    unsigned bits = 0;
    size_t end = in.size;
    while (end && in.data[end - 1] == '=')
        --end;
    if (in.size - end > 2 || end % 4 == 1)
        return false;
    for (size_t i = 0; i < end; ++i) {
        const int v = base64Value(in.data[i]);
        if (v < 0)
            return false;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (size >= capacity)
                return false;
            out[size++] = uint8_t(acc >> bits);
        }
    }
    return true;
}

void copyText(Bytes text, char *out, size_t capacity)
{
    size_t n = 0;
    for (size_t i = 0; i < text.size && n + 1 < capacity; ++i) {
        const uint8_t c = text.data[i];
        if (c >= 0x20 && c < 0x7f)
            out[n++] = char(c);
    }
    out[n] = 0;
}

// "https://mint.example.com:3338/path" -> "mint.example.com"
void mintHost(Bytes url, char *out, size_t capacity)
{
    size_t start = 0;
    for (size_t i = 0; i + 2 < url.size; ++i)
        if (url.data[i] == ':' && url.data[i + 1] == '/' && url.data[i + 2] == '/') {
            start = i + 3;
            break;
        }
    size_t end = start;
    while (end < url.size && url.data[end] != '/' && url.data[end] != ':' && url.data[end] != '?')
        ++end;
    copyText({url.data + start, end - start}, out, capacity);
}

bool sameText(Bytes text, const char *literal)
{
    const size_t n = strlen(literal);
    return text.size == n && memcmp(text.data, literal, n) == 0;
}

// ---------- JSON, just enough for a token and a NUT-10 secret

struct Json {
    const uint8_t *data;
    size_t size;
    size_t at = 0;
    unsigned depth = 0;

    void space()
    {
        while (at < size && (data[at] == ' ' || data[at] == '\n' || data[at] == '\r' || data[at] == '\t'))
            ++at;
    }
    bool peek(uint8_t c)
    {
        space();
        return at < size && data[at] == c;
    }
    bool eat(uint8_t c)
    {
        if (!peek(c))
            return false;
        ++at;
        return true;
    }
    // A string, unescaped into `out` when given; `length` is its size there.
    bool string(char *out, size_t capacity, size_t &length)
    {
        length = 0;
        if (!eat('"'))
            return false;
        while (at < size) {
            uint8_t c = data[at++];
            if (c == '"') {
                if (out && length < capacity)
                    out[length] = 0;
                return !out || length < capacity;
            }
            if (c < 0x20)
                return false;
            uint32_t code = c;
            if (c == '\\') {
                if (at >= size)
                    return false;
                const uint8_t e = data[at++];
                switch (e) {
                case '"':
                case '\\':
                case '/':
                    code = e;
                    break;
                case 'b':
                    code = 8;
                    break;
                case 'f':
                    code = 12;
                    break;
                case 'n':
                    code = 10;
                    break;
                case 'r':
                    code = 13;
                    break;
                case 't':
                    code = 9;
                    break;
                case 'u': {
                    if (size - at < 4)
                        return false;
                    code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const uint8_t h = data[at++];
                        code <<= 4;
                        if (h >= '0' && h <= '9')
                            code |= h - '0';
                        else if ((h | 0x20) >= 'a' && (h | 0x20) <= 'f')
                            code |= (h | 0x20) - 'a' + 10;
                        else
                            return false;
                    }
                    break;
                }
                default:
                    return false;
                }
            }
            if (!out)
                continue;
            // Escapes become UTF-8; the secrets that matter here are plain ASCII.
            uint8_t encoded[3];
            size_t n = 0;
            if (code < 0x80 || c != '\\') {
                encoded[n++] = uint8_t(code);
            } else if (code < 0x800) {
                encoded[n++] = uint8_t(0xc0 | (code >> 6));
                encoded[n++] = uint8_t(0x80 | (code & 0x3f));
            } else {
                encoded[n++] = uint8_t(0xe0 | (code >> 12));
                encoded[n++] = uint8_t(0x80 | ((code >> 6) & 0x3f));
                encoded[n++] = uint8_t(0x80 | (code & 0x3f));
            }
            if (length + n >= capacity)
                return false;
            memcpy(out + length, encoded, n);
            length += n;
        }
        return false;
    }
    // Raw text of a string with no escapes, pointing into the input.
    bool plain(Bytes &text)
    {
        if (!eat('"'))
            return false;
        const size_t start = at;
        while (at < size && data[at] != '"') {
            if (data[at] == '\\' || data[at] < 0x20)
                return false;
            ++at;
        }
        if (at >= size)
            return false;
        text = {data + start, at - start};
        ++at;
        return true;
    }
    bool number(uint64_t &value)
    {
        space();
        value = 0;
        const size_t start = at;
        while (at < size && data[at] >= '0' && data[at] <= '9') {
            if (value > (MAX_AMOUNT - 9) / 10)
                return false;
            value = value * 10 + (data[at++] - '0');
        }
        // fractions and exponents are not amounts
        return at > start && !(at < size && (data[at] == '.' || data[at] == 'e' || data[at] == 'E'));
    }
    bool skip()
    {
        space();
        if (at >= size || ++depth > DEPTH_LIMIT)
            return false;
        bool ok = true;
        const uint8_t c = data[at];
        if (c == '"') {
            size_t n;
            ok = string(nullptr, 0, n);
        } else if (c == '{' || c == '[') {
            const uint8_t close = c == '{' ? '}' : ']';
            ++at;
            if (!eat(close)) {
                do {
                    if (c == '{') {
                        size_t n;
                        ok = string(nullptr, 0, n) && eat(':');
                    }
                    ok = ok && skip();
                } while (ok && eat(','));
                ok = ok && eat(close);
            }
        } else {
            const size_t start = at;
            while (at < size && data[at] != ',' && data[at] != '}' && data[at] != ']' && data[at] != ' ' && data[at] != '\n' &&
                   data[at] != '\r' && data[at] != '\t')
                ++at;
            ok = at > start;
        }
        --depth;
        return ok;
    }
    // Calls `field(key)` for each member of an object; the callback reads the value.
    template <typename F> bool object(F field)
    {
        if (!eat('{'))
            return false;
        if (eat('}'))
            return true;
        do {
            Bytes key;
            if (!plain(key) || !eat(':') || !field(key))
                return false;
        } while (eat(','));
        return eat('}');
    }
    template <typename F> bool array(F item)
    {
        if (!eat('['))
            return false;
        if (eat(']'))
            return true;
        do {
            if (!item())
                return false;
        } while (eat(','));
        return eat(']');
    }
};

bool addAmount(CashuToken &out, uint64_t amount)
{
    if (!amount || amount > MAX_AMOUNT || out.amount > MAX_AMOUNT - amount || out.proofs == 0xffff)
        return false;
    out.amount += amount;
    ++out.proofs;
    return true;
}

bool decodeV3(Bytes payload, CashuToken &out)
{
    Json j{payload.data, payload.size};
    char secret[SECRET_LIMIT];
    const bool ok = j.object([&](Bytes key) {
        if (sameText(key, "token"))
            return j.array([&]() {
                return j.object([&](Bytes entryKey) {
                    if (sameText(entryKey, "mint")) {
                        Bytes url;
                        if (!j.plain(url))
                            return false;
                        if (!out.mint[0])
                            mintHost(url, out.mint, sizeof(out.mint));
                        return true;
                    }
                    if (!sameText(entryKey, "proofs"))
                        return j.skip();
                    return j.array([&]() {
                        uint64_t amount = 0;
                        bool haveSecret = false, locked = false;
                        if (!j.object([&](Bytes proofKey) {
                                if (sameText(proofKey, "amount"))
                                    return j.number(amount);
                                if (sameText(proofKey, "secret")) {
                                    size_t length;
                                    if (!j.string(secret, sizeof(secret), length))
                                        return false;
                                    haveSecret = true;
                                    locked = lockedSecret({reinterpret_cast<uint8_t *>(secret), length});
                                    return true;
                                }
                                return j.skip();
                            }) ||
                            !haveSecret || !addAmount(out, amount))
                            return false;
                        out.locked += locked;
                        return true;
                    });
                });
            });
        if (sameText(key, "unit")) {
            Bytes unit;
            if (!j.plain(unit))
                return false;
            copyText(unit, out.unit, sizeof(out.unit));
            return true;
        }
        return j.skip();
    });
    j.space();
    return ok && j.at == j.size && out.proofs;
}

// ---------- CBOR, definite lengths only

struct Cbor {
    const uint8_t *data;
    size_t size;
    size_t at = 0;
    unsigned depth = 0;

    bool head(uint8_t &major, uint64_t &argument)
    {
        if (at >= size)
            return false;
        const uint8_t b = data[at++];
        major = b >> 5;
        const uint8_t info = b & 0x1f;
        if (info < 24) {
            argument = info;
            return true;
        }
        if (info > 27)
            return false;
        const size_t width = size_t(1) << (info - 24);
        if (size - at < width)
            return false;
        argument = 0;
        for (size_t i = 0; i < width; ++i)
            argument = (argument << 8) | data[at++];
        return true;
    }
    bool text(Bytes &out)
    {
        uint8_t major;
        uint64_t length;
        if (!head(major, length) || major != 3 || length > size - at)
            return false;
        out = {data + at, size_t(length)};
        at += size_t(length);
        return true;
    }
    bool unsignedValue(uint64_t &value)
    {
        uint8_t major;
        return head(major, value) && major == 0;
    }
    bool skip()
    {
        uint8_t major;
        uint64_t argument;
        if (++depth > DEPTH_LIMIT || !head(major, argument)) {
            --depth;
            return false;
        }
        bool ok = true;
        switch (major) {
        case 0:
        case 1:
        case 7:
            break;
        case 2:
        case 3:
            ok = argument <= size - at;
            if (ok)
                at += size_t(argument);
            break;
        case 4:
        case 5: {
            // every item takes at least one byte
            const uint64_t items = major == 5 ? argument * 2 : argument;
            ok = argument <= size && items <= size - at;
            for (uint64_t i = 0; ok && i < items; ++i)
                ok = skip();
            break;
        }
        case 6:
            ok = skip();
            break;
        }
        --depth;
        return ok;
    }
    template <typename F> bool map(F field)
    {
        uint8_t major;
        uint64_t count;
        if (!head(major, count) || major != 5 || count > size - at)
            return false;
        for (uint64_t i = 0; i < count; ++i) {
            Bytes key;
            if (!text(key) || !field(key))
                return false;
        }
        return true;
    }
    template <typename F> bool array(F item)
    {
        uint8_t major;
        uint64_t count;
        if (!head(major, count) || major != 4 || count > size - at)
            return false;
        for (uint64_t i = 0; i < count; ++i)
            if (!item())
                return false;
        return true;
    }
};

bool decodeV4(Bytes payload, CashuToken &out)
{
    Cbor c{payload.data, payload.size};
    const bool ok = c.map([&](Bytes key) {
        if (sameText(key, "m")) {
            Bytes url;
            if (!c.text(url))
                return false;
            mintHost(url, out.mint, sizeof(out.mint));
            return true;
        }
        if (sameText(key, "u")) {
            Bytes unit;
            if (!c.text(unit))
                return false;
            copyText(unit, out.unit, sizeof(out.unit));
            return true;
        }
        if (!sameText(key, "t"))
            return c.skip();
        return c.array([&]() {
            return c.map([&](Bytes groupKey) {
                if (!sameText(groupKey, "p"))
                    return c.skip();
                return c.array([&]() {
                    uint64_t amount = 0;
                    bool haveSecret = false, locked = false;
                    if (!c.map([&](Bytes proofKey) {
                            if (sameText(proofKey, "a"))
                                return c.unsignedValue(amount);
                            if (sameText(proofKey, "s")) {
                                Bytes secret;
                                if (!c.text(secret) || secret.size > SECRET_LIMIT)
                                    return false;
                                haveSecret = true;
                                locked = lockedSecret(secret);
                                return true;
                            }
                            return c.skip();
                        }) ||
                        !haveSecret || !addAmount(out, amount))
                        return false;
                    out.locked += locked;
                    return true;
                });
            });
        });
    });
    return ok && c.at == c.size && out.proofs;
}

// Where a token starts: "cashuA"/"cashuB" not inside a longer word, as the app's pattern wants.
bool tokenAt(Bytes text, size_t i)
{
    if (text.size - i < 6 + 40 || memcmp(text.data + i, "cashu", 5) != 0 || (text.data[i + 5] != 'A' && text.data[i + 5] != 'B'))
        return false;
    // Not inside a word, as the app's \b wants: "=cashuA..." in a link is still a token.
    const auto word = [](uint8_t c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    };
    if (i && word(text.data[i - 1]))
        return false;
    for (size_t k = i + 6; k < i + 46; ++k)
        if (!tokenChar(text.data[k]))
            return false;
    return true;
}

} // namespace

bool lockedSecret(Bytes secret)
{
    Json j{secret.data, secret.size};
    Bytes kind, data;
    bool locktime = false, refund = false, haveData = false, haveTags = false, haveRefund = false, twice = false;
    if (!j.eat('[') || !j.plain(kind) || !sameText(kind, "P2PK") || !j.eat(','))
        return false;
    const bool ok = j.object([&](Bytes key) {
        // Readers differ on repeated keys (the mint may keep another one than this): repeated means unlocked.
        if (sameText(key, "data")) {
            twice |= haveData;
            haveData = true;
            return j.plain(data);
        }
        if (!sameText(key, "tags"))
            return j.skip();
        twice |= haveTags;
        haveTags = true;
        return j.array([&]() {
            Bytes name;
            unsigned values = 0;
            if (!j.eat('[') || !j.plain(name))
                return false;
            while (j.eat(',')) {
                Bytes value;
                if (!j.plain(value))
                    return false;
                values += value.size > 0;
            }
            if (!j.eat(']'))
                return false;
            if (sameText(name, "locktime")) {
                twice |= locktime;
                locktime = true;
            } else if (sameText(name, "refund")) {
                twice |= haveRefund;
                haveRefund = true;
                refund = values > 0;
            }
            return true;
        });
    });
    if (!ok || twice || !j.eat(']'))
        return false;
    j.space();
    if (j.at != j.size)
        return false;
    // A compressed public key: 33 bytes in hex, starting 02 or 03.
    if (data.size != 66 || data.data[0] != '0' || (data.data[1] != '2' && data.data[1] != '3'))
        return false;
    for (size_t i = 0; i < data.size; ++i) {
        const uint8_t h = data.data[i] | 0x20;
        if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f')))
            return false;
    }
    // After a locktime with no refund key anyone may spend the proof.
    return !locktime || refund;
}

bool mentionsCashu(Bytes text)
{
    for (size_t i = 0; i + 6 <= text.size; ++i)
        if (memcmp(text.data + i, "cashu", 5) == 0 && (text.data[i + 5] == 'A' || text.data[i + 5] == 'B'))
            return true;
    return false;
}

bool tokenLike(Bytes text)
{
    if (mentionsCashu(text))
        return true;
    // Base64 of a token mixes capitals, small letters and digits within a few characters; laughter
    // ("kkkk..."), separator lines, hex fingerprints, addresses and invoices (one letter case) do not.
    size_t run = 0;
    bool upper = false, lower = false, digit = false;
    for (size_t i = 0; i < text.size; ++i) {
        const uint8_t c = text.data[i];
        const bool u = c >= 'A' && c <= 'Z', l = c >= 'a' && c <= 'z', d = c >= '0' && c <= '9';
        if (!(u || l || d || c == '-' || c == '_')) {
            run = 0;
            upper = lower = digit = false;
            continue;
        }
        ++run;
        upper |= u;
        lower |= l;
        digit |= d;
        if (run >= 40 && upper && lower && digit)
            return true;
    }
    return false;
}

CashuFound findCashu(Bytes text, uint8_t (&scratch)[CASHU_SCRATCH], CashuToken &out)
{
    out = {};
    // Every mention counts, read or not: a second token the reader cannot see must not slip past the lock
    // check, and a mention that is no readable token is still kept off the text paths.
    size_t mentions = 0;
    for (size_t i = 0; i + 6 <= text.size; ++i)
        if (memcmp(text.data + i, "cashu", 5) == 0 && (text.data[i + 5] == 'A' || text.data[i + 5] == 'B'))
            ++mentions;
    if (!mentions)
        return CashuFound::None;
    if (mentions > 1)
        return CashuFound::Several;
    size_t start = text.size;
    for (size_t i = 0; i + 46 <= text.size && start == text.size; ++i)
        if (tokenAt(text, i))
            start = i;
    if (start == text.size)
        return CashuFound::Malformed;
    size_t end = start + 6;
    while (end < text.size && tokenChar(text.data[end]))
        ++end;
    size_t size = 0;
    out.version = char(text.data[start + 5]);
    if (!base64Decode({text.data + start + 6, end - start - 6}, scratch, sizeof(scratch), size))
        return CashuFound::Malformed;
    const Bytes payload{scratch, size};
    const bool ok = out.version == 'A' ? decodeV3(payload, out) : decodeV4(payload, out);
    if (!ok) {
        const char version = out.version;
        out = {};
        out.version = version;
        return CashuFound::Malformed;
    }
    if (!out.unit[0])
        strcpy(out.unit, "sat");
    return CashuFound::Token;
}

} // namespace bitchat

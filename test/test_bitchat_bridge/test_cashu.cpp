#include "bridge/BridgeCashu.h"
#include "bridge/BridgeInflate.h"
#include "bridge/BridgePayTunnel.h"
#include "cashu_vectors.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace bitchat;
static size_t checks = 0;
static void check(bool passed, int line, const char *expression, const char *name = nullptr)
{
    ++checks;
    if (!passed) {
        std::cerr << "FAIL line " << line << ": " << expression << (name ? " [" : "") << (name ? name : "") << (name ? "]" : "")
                  << '\n';
        std::exit(1);
    }
}
#define CHECK(expression) check((expression), __LINE__, #expression)
#define CHECK_CASE(expression, name) check((expression), __LINE__, #expression, name)

static std::vector<uint8_t> unhex(const char *text)
{
    const size_t length = std::strlen(text);
    std::vector<uint8_t> bytes(length / 2);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = uint8_t(std::stoul(std::string(text + 2 * i, 2), nullptr, 16));
    return bytes;
}
static Bytes bytes(const std::vector<uint8_t> &data)
{
    return {data.data(), data.size()};
}
static Bytes utf8(const char *value)
{
    return {reinterpret_cast<const uint8_t *>(value), std::strlen(value)};
}

static void inflateStreams()
{
    static uint8_t out[4096];
    for (const auto &s : STREAMS) {
        const auto compressed = unhex(s.compressed);
        const auto plain = unhex(s.plain);
        size_t produced = 0;
        CHECK_CASE(inflateRaw(bytes(compressed), out, sizeof(out), produced), s.name);
        CHECK_CASE(produced == plain.size() && (!produced || std::memcmp(out, plain.data(), produced) == 0), s.name);
        // One byte short of room fails instead of writing past it.
        if (!plain.empty())
            CHECK_CASE(!inflateRaw(bytes(compressed), out, plain.size() - 1, produced), s.name);
        // Any cut stream fails.
        for (size_t cut = 0; cut + 1 < compressed.size(); cut += 1 + compressed.size() / 13)
            CHECK_CASE(!inflateRaw({compressed.data(), cut}, out, sizeof(out), produced) || produced <= plain.size(), s.name);
    }
    // Random junk never reads or writes out of bounds (the sanitizer watches).
    std::mt19937 rng(11);
    for (int round = 0; round < 3000; ++round) {
        std::vector<uint8_t> junk(rng() % 300);
        for (auto &b : junk)
            b = uint8_t(rng());
        size_t produced = 0;
        inflateRaw(bytes(junk), out, 1792, produced);
        CHECK(produced <= 1792);
    }
    // Flipped bits in a real stream: fails or yields bounded output.
    const auto base = unhex(STREAMS[10].compressed);
    for (size_t bit = 0; bit < base.size() * 8; bit += 3) {
        auto copy = base;
        copy[bit / 8] ^= uint8_t(1u << (bit % 8));
        size_t produced = 0;
        inflateRaw(bytes(copy), out, 1792, produced);
        CHECK(produced <= 1792);
    }
}

static void messagePayloads()
{
    static uint8_t out[MAX_PAYLOAD];
    const auto &s = STREAMS[10];
    const auto compressed = unhex(s.compressed);
    const auto plain = unhex(s.plain);
    std::vector<uint8_t> payload(2);
    payload[0] = uint8_t(plain.size() >> 8);
    payload[1] = uint8_t(plain.size());
    payload.insert(payload.end(), compressed.begin(), compressed.end());
    Packet p;
    p.version = 1;
    p.flags = 4;
    p.payload = bytes(payload);
    Bytes text;
    CHECK(messageText(p, out, sizeof(out), text));
    CHECK(text.size == plain.size() && std::memcmp(text.data, plain.data(), text.size) == 0);
    // A size prefix that does not match what the stream holds is refused.
    payload[1] ^= 1;
    p.payload = bytes(payload);
    CHECK(!messageText(p, out, sizeof(out), text));
    // Version 2 has four bytes of size.
    std::vector<uint8_t> v2{0, 0, uint8_t(plain.size() >> 8), uint8_t(plain.size())};
    v2.insert(v2.end(), compressed.begin(), compressed.end());
    p.version = 2;
    p.payload = bytes(v2);
    CHECK(messageText(p, out, sizeof(out), text) && text.size == plain.size());
    // Uncompressed: the payload itself.
    p.flags = 0;
    CHECK(messageText(p, out, sizeof(out), text) && text.data == v2.data());
}

static void tokens()
{
    static uint8_t scratch[CASHU_SCRATCH];
    for (const auto &c : TOKENS) {
        CashuToken token;
        const CashuFound found = findCashu(utf8(c.text), scratch, token);
        CHECK_CASE(int(found) == c.found, c.name);
        if (found != CashuFound::Token)
            continue;
        CHECK_CASE(token.amount == c.amount, c.name);
        CHECK_CASE(token.proofs == c.proofs, c.name);
        CHECK_CASE(token.locked == c.locked, c.name);
        CHECK_CASE(std::strcmp(token.unit, c.unit) == 0, c.name);
        CHECK_CASE(std::strcmp(token.mint, c.mint) == 0, c.name);
        CHECK_CASE(token.unlocked() == (c.locked < c.proofs), c.name);
    }
    // Every prefix of a real token is either refused or read as a whole token, never anything else.
    for (const auto &c : TOKENS) {
        if (c.found != 1)
            continue;
        std::string text(c.text);
        for (size_t cut = 0; cut < text.size(); cut += 7) {
            CashuToken token;
            const CashuFound found = findCashu(utf8(text.substr(0, cut).c_str()), scratch, token);
            CHECK_CASE(found != CashuFound::Several, c.name);
            if (found == CashuFound::Token)
                CHECK_CASE(token.amount <= c.amount, c.name);
        }
    }
    // Random bytes in a token's place never reach out of bounds.
    std::mt19937 rng(5);
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    for (int round = 0; round < 3000; ++round) {
        std::string text = round % 2 ? "cashuA" : "cashuB";
        const size_t n = 40 + rng() % 1200;
        for (size_t i = 0; i < n; ++i)
            text += alphabet[rng() % 64];
        CashuToken token;
        findCashu(utf8(text.c_str()), scratch, token);
    }
    // Pieces of a token without its name, and text that is no token.
    CHECK(tokenLike(utf8("ab3Fg-_9ab3Fg-_9ab3Fg-_9ab3Fg-_9ab3Fg-_9")));
    CHECK(!tokenLike(utf8("ab3Fg-_9ab3Fg-_9ab3Fg-_9ab3Fg-_9ab3Fg-_ e mais")));
    CHECK(!tokenLike(utf8("https://exemplo.com.br/caminho/longo/de/verdade?x=1&y=2")));
    CHECK(tokenLike(utf8("veja cashuB")));
    // Secrets read by themselves.
    CHECK(!lockedSecret(utf8("abc")));
    CHECK(!lockedSecret(utf8("[\"P2PK\",{\"data\":\"02aa\"}]")));
    std::string key = "02";
    for (int i = 0; i < 32; ++i)
        key += "ab";
    std::string upper = "03";
    for (int i = 0; i < 32; ++i)
        upper += "AB";
    const auto secret = [](const std::string &text) { return lockedSecret(utf8(text.c_str())); };
    CHECK(secret("[\"P2PK\",{\"nonce\":\"00\",\"data\":\"" + key + "\"}]"));
    CHECK(secret(" [ \"P2PK\" , { \"data\" : \"" + upper + "\" , \"tags\" : [ ] } ] "));
    CHECK(!secret("[\"P2PK\",{\"data\":\"" + key + "\",\"tags\":[[\"locktime\",\"1\"]]}]"));
    CHECK(secret("[\"P2PK\",{\"data\":\"" + key + "\",\"tags\":[[\"locktime\",\"1\"],[\"refund\",\"" + upper + "\"]]}]"));
    CHECK(!secret("[\"P2PK\",{\"data\":\"" + key + "\"}] x"));
    CHECK(!secret("[\"P2PK\",{\"data\":\"" + key.substr(2) + "00\"}]"));
    CHECK(!secret("[\"HTLC\",{\"data\":\"" + key + "\"}]"));
    // Repeated keys or tags: read differently by different readers, so never taken as locked.
    CHECK(!secret("[\"P2PK\",{\"data\":\"" + key + "\",\"data\":\"" + upper + "\"}]"));
    CHECK(!secret("[\"P2PK\",{\"data\":\"" + key + "\",\"tags\":[],\"tags\":[[\"locktime\",\"1\"]]}]"));
    CHECK(!secret("[\"P2PK\",{\"data\":\"" + key + "\",\"tags\":[[\"locktime\",\"1\"],[\"refund\"],[\"refund\",\"" + upper +
                  "\"]]}]"));
}

static std::vector<uint8_t> filled(size_t size, uint8_t seed)
{
    std::vector<uint8_t> v(size);
    for (size_t i = 0; i < size; ++i)
        v[i] = uint8_t(seed + i * 7);
    return v;
}

static void tunnel()
{
    const auto announce = filled(230, 1), message = filled(1100, 9);
    const size_t expected = (4 + announce.size() + message.size() + PAY_CHUNK - 1) / PAY_CHUNK;
    CHECK(payParts(4 + announce.size() + message.size()) == expected);
    PaySender sender;
    CHECK(sender.start(0x01020304, bytes(announce), bytes(message), 1000));
    CHECK(!sender.start(5, bytes(announce), bytes(message), 1000)); // one at a time
    std::vector<PayDatagram> parts;
    PayDatagram d;
    while (sender.next(d)) {
        CHECK(d.size <= PAY_DATAGRAM && payDatagram(d.bytes()));
        parts.push_back(d);
    }
    CHECK(parts.size() == expected);

    // In any order, with copies: complete exactly once, after the last missing part.
    std::mt19937 rng(3);
    for (int round = 0; round < 50; ++round) {
        PayReceiver receiver;
        std::vector<size_t> order;
        for (size_t i = 0; i < parts.size(); ++i)
            order.push_back(i), order.push_back(i);
        std::shuffle(order.begin(), order.end(), rng);
        unsigned completions = 0;
        std::vector<bool> seen(parts.size());
        size_t distinct = 0;
        for (size_t i : order) {
            PayHeard heard;
            const bool any = receiver.accept(parts[i].bytes(), 2000, heard);
            if (!seen[i]) {
                seen[i] = true;
                ++distinct;
            }
            if (any && heard.complete) {
                ++completions;
                CHECK(distinct == parts.size());
                CHECK(heard.id == 0x01020304);
                CHECK(heard.announce.size == announce.size() &&
                      !std::memcmp(heard.announce.data, announce.data(), announce.size()));
                CHECK(heard.message.size == message.size() && !std::memcmp(heard.message.data, message.data(), message.size()));
            }
        }
        CHECK(completions == 1);
    }

    // A missing part: nothing is delivered, the receiver asks for it when quiet, and a resend completes it.
    {
        PayReceiver receiver;
        PayHeard heard;
        for (size_t i = 0; i < parts.size(); ++i)
            if (i != 4) {
                receiver.accept(parts[i].bytes(), 2000, heard);
                CHECK(!heard.complete);
            }
        CHECK(!receiver.want(2000 + PAY_QUIET_MS - 1, d));
        CHECK(receiver.want(2000 + PAY_QUIET_MS, d));
        PayHeard want;
        PayReceiver other;
        CHECK(other.accept(d.bytes(), 0, want) && want.kind == PayKind::Want && want.missing == (1u << 4) &&
              want.id == 0x01020304);
        sender.resend(want.missing, 2000 + PAY_QUIET_MS);
        CHECK(sender.next(d) && d.data[7] == 4);
        CHECK(!sender.next(d));
        receiver.accept(d.bytes(), 2000 + PAY_QUIET_MS + 10, heard);
        CHECK(heard.complete && heard.message.size == message.size());
        // Asked at most twice, then given up.
        PayReceiver stubborn;
        stubborn.accept(parts[0].bytes(), 0, heard);
        CHECK(stubborn.want(PAY_QUIET_MS, d));
        CHECK(stubborn.want(2 * PAY_QUIET_MS, d));
        CHECK(!stubborn.want(3 * PAY_QUIET_MS, d));
        // An incomplete bundle is forgotten after the wait; its parts never show.
        stubborn.expire(PAY_WAIT_MS + 20001);
        CHECK(stubborn.empty());
    }
    // Resends late in the window are not made.
    sender.resend(1, 1000 + PAY_WAIT_MS);
    CHECK(!sender.next(d));
    sender.finish();
    CHECK(!sender.active());

    // Damaged parts: a short middle part, a wrong count, a foreign magic.
    {
        PayReceiver receiver;
        PayHeard heard;
        auto shortPart = parts[2];
        shortPart.size -= 1;
        CHECK(!receiver.accept(shortPart.bytes(), 0, heard));
        auto wrong = parts[2];
        wrong.data[8] = 0;
        CHECK(!receiver.accept(wrong.bytes(), 0, heard));
        auto foreign = parts[2];
        foreign.data[0] = 'X';
        CHECK(!receiver.accept(foreign.bytes(), 0, heard));
        auto future = parts[2];
        future.data[2] = 0x20;
        CHECK(!receiver.accept(future.bytes(), 0, heard));
    }
    // A bundle whose inner sizes do not add up is complete but empty.
    {
        PaySender s;
        std::vector<uint8_t> a = filled(10, 1), m = filled(20, 2);
        CHECK(s.start(9, bytes(a), bytes(m), 0));
        CHECK(s.next(d) && !s.next(d));
        d.data[PAY_HEADER + 1] = 200; // announce size beyond the bundle
        PayReceiver receiver;
        PayHeard heard;
        CHECK(receiver.accept(d.bytes(), 0, heard) && heard.complete && !heard.announce.size && !heard.message.size);
    }
    // Too large for twelve parts.
    {
        PaySender s;
        const auto big = filled(PAY_BUNDLE, 3);
        CHECK(!s.start(1, bytes(announce), bytes(big), 0));
    }
    // Acks.
    {
        const uint8_t key[8] = {0xa1, 0xb2, 0xc3, 0xd4, 5, 6, 7, 8};
        CHECK(makePayAck(77, PayOutcome::NoPhones, 3, key, nullptr, d) && d.size == 13);
        PayReceiver receiver;
        PayHeard heard;
        CHECK(receiver.accept(d.bytes(), 0, heard));
        CHECK(heard.kind == PayKind::Ack && heard.id == 77 && heard.outcome == PayOutcome::NoPhones && heard.phones == 3 &&
              heard.node == 0xa1b2c3d4 && !heard.signedAck);
        uint8_t signature[64];
        for (int i = 0; i < 64; ++i)
            signature[i] = uint8_t(i);
        CHECK(makePayAck(78, PayOutcome::Delivered, 1, key, signature, d) && d.size == PAY_ACK_SIZE && d.size <= PAY_DATAGRAM);
        CHECK(readPayAnswer(d.bytes(), heard) && heard.signedAck && heard.node == 0xa1b2c3d4 && heard.key[7] == 8 &&
              heard.signature[63] == 63);
        uint8_t signedBytes[PAY_ACK_SIGNED], hash[8] = {9, 9, 9, 9, 9, 9, 9, 9};
        payAckSigned(78, PayOutcome::Delivered, 1, key, hash, signedBytes);
        CHECK(signedBytes[0] == 'B' && signedBytes[2] == 'A' && signedBytes[7] == 0 && signedBytes[24] == 9);
        d.data[7] = 9;
        CHECK(!receiver.accept(d.bytes(), 0, heard));
    }
    // Two bundles at once share the two slots; a third pushes out the older.
    {
        PayReceiver receiver;
        PaySender a, b, c;
        const auto small = filled(200, 4);
        CHECK(a.start(1, bytes(announce), bytes(small), 0) && b.start(2, bytes(announce), bytes(small), 0) &&
              c.start(3, bytes(announce), bytes(small), 0));
        PayDatagram da, db, dc;
        PayHeard heard;
        a.next(da);
        b.next(db);
        c.next(dc);
        receiver.accept(da.bytes(), 0, heard);
        receiver.accept(db.bytes(), 10, heard);
        receiver.accept(dc.bytes(), 20, heard);
        unsigned done = 0;
        for (PaySender *s : {&b, &c, &a})
            while (s->next(d))
                if (receiver.accept(d.bytes(), 30, heard) && heard.complete)
                    ++done, CHECK(heard.id != 1 || heard.message.size == small.size());
        CHECK(done >= 2);
    }
}

int main()
{
    inflateStreams();
    messagePayloads();
    tokens();
    tunnel();
    std::cout << "cashu: " << checks << " checks passed\n";
    return 0;
}

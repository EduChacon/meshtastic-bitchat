"""Cases for the Cashu tunnel tests: DEFLATE streams made by zlib and Cashu tokens of every kind the bridge
must tell apart. The official tokens come from the Cashu NUT-00 test vectors (MIT licence); the rest are
built here, so each expectation follows from how the token was made."""

import base64
import json
import random
import zlib

from pathlib import Path

# cashubtc/nuts tests/00-tests.md, copied with its source in the file
OFFICIAL = json.loads(
    (
        Path(__file__).resolve().parents[2]
        / "test/test_bitchat_bridge/cashu_official.json"
    ).read_text()
)["cases"]

KEY = "02" + "ab" * 32
REFUND = "03" + "cd" * 32


def secret(kind, tags, data=KEY):
    return json.dumps(
        [kind, {"nonce": "11" * 32, "data": data, "tags": tags}], separators=(",", ":")
    )


SECRETS = {
    "plain": "407915bc212be61a77e3e6d2aeb4c727980bda51cd06a6afc29e2861768a7837",
    "p2pk": secret("P2PK", [["sigflag", "SIG_INPUTS"]]),
    "p2pk refund": secret("P2PK", [["locktime", "1700000000"], ["refund", REFUND]]),
    "p2pk expiring": secret("P2PK", [["locktime", "1700000000"]]),
    "p2pk empty refund": secret("P2PK", [["locktime", "1700000000"], ["refund"]]),
    "p2pk bad key": secret("P2PK", [], data="04" + "ab" * 32),
    "htlc": secret("HTLC", [], data="ee" * 32),
}
LOCKED = {"p2pk", "p2pk refund"}


def cbor(value):
    def head(major, n):
        if n < 24:
            return bytes([major << 5 | n])
        for info, width in ((24, 1), (25, 2), (26, 4), (27, 8)):
            if n < 1 << (8 * width):
                return bytes([major << 5 | info]) + n.to_bytes(width, "big")
        raise ValueError(n)

    if isinstance(value, bool):
        return bytes([0xF5 if value else 0xF4])
    if isinstance(value, int):
        return head(0, value)
    if isinstance(value, bytes):
        return head(2, len(value)) + value
    if isinstance(value, str):
        raw = value.encode()
        return head(3, len(raw)) + raw
    if isinstance(value, list):
        return head(4, len(value)) + b"".join(cbor(v) for v in value)
    if isinstance(value, dict):
        return head(5, len(value)) + b"".join(
            cbor(k) + cbor(v) for k, v in value.items()
        )
    raise TypeError(value)


def b64(raw, pad=False):
    text = base64.urlsafe_b64encode(raw).decode()
    return text if pad else text.rstrip("=")


def token_a(kinds, amounts, unit="sat", mint="https://mint.example.com:3338/x"):
    proofs = [
        {
            "amount": a,
            "id": "009a1f293253e41e",
            "secret": SECRETS[k],
            "C": "02" + "77" * 32,
            "dleq": {"e": "aa" * 32, "s": "bb" * 32, "r": "cc" * 32},
        }
        for k, a in zip(kinds, amounts)
    ]
    body = {
        "token": [{"mint": mint, "proofs": proofs}],
        "unit": unit,
        "memo": "obrigado á",
    }
    return "cashuA" + b64(json.dumps(body).encode(), pad=True)


def token_b(kinds, amounts, unit="sat", mint="https://mint.example.com"):
    proofs = [
        {
            "a": a,
            "s": SECRETS[k],
            "c": bytes.fromhex("02" + "77" * 32),
            "d": {"e": bytes(32), "s": bytes(32), "r": bytes(32)},
        }
        for k, a in zip(kinds, amounts)
    ]
    body = {
        "m": mint,
        "u": unit,
        "d": "pix",
        "t": [{"i": bytes.fromhex("00ad268c4d1f5826"), "p": proofs}],
    }
    return "cashuB" + b64(cbor(body))


def tokens():
    """(name, text, found, amount, proofs, locked, unit, mint); found: 0 none, 1 token, 2 malformed, 3 several."""
    cases = [
        (c["name"], c["token"], 1, c["amount"], c["proofs"], 0, "sat", c["mint"])
        for c in OFFICIAL
    ]
    for maker, label, mint in (
        (token_a, "A", "mint.example.com"),
        (token_b, "B", "mint.example.com"),
    ):
        # A message holds at most 1792 bytes, so version A, wordier, gets one proof less.
        proofs = 1 if label == "A" else 2
        for kind in SECRETS:
            text = maker([kind] * proofs, [8, 2][:proofs])
            locked = proofs if kind in LOCKED else 0
            cases.append(
                (
                    f"{label} {kind}",
                    f"pay: {text} thanks",
                    1,
                    [8, 10][proofs - 1],
                    proofs,
                    locked,
                    "sat",
                    mint,
                )
            )
        kinds = ["p2pk", "plain", "p2pk refund"][: proofs + 1]
        mixed = maker(kinds, [1, 2, 4][: proofs + 1])
        cases.append(
            (
                f"{label} mixed",
                mixed,
                1,
                [3, 7][proofs - 1],
                proofs + 1,
                proofs,
                "sat",
                mint,
            )
        )
        # Larger than any message: refused, not read past its buffer.
        cases.append(
            (
                f"{label} oversize",
                maker(["p2pk refund"] * 4, [1, 1, 1, 1]),
                2,
                0,
                0,
                0,
                "",
                "",
            )
        )
        cases.append(
            (
                f"{label} usd",
                maker(["p2pk"], [150], unit="usd"),
                1,
                150,
                1,
                1,
                "usd",
                mint,
            )
        )
        good = maker(["p2pk"], [5])
        cases.append((f"{label} truncated", good[: len(good) // 2], 2, 0, 0, 0, "", ""))
        cases.append((f"{label} several", good + " " + good, 3, 0, 0, 0, "", ""))
        cases.append((f"{label} uri", "cashu:" + good, 1, 5, 1, 1, "sat", mint))
        cases.append((f"{label} zero amount", maker(["p2pk"], [0]), 2, 0, 0, 0, "", ""))
    cases.append(("no token", "hello cashu world, cashu is ecash", 0, 0, 0, 0, "", ""))
    # A mention that is no readable token is kept off the text paths all the same.
    cases.append(("bare word", "cashuA is a prefix", 2, 0, 0, 0, "", ""))
    cases.append(("inside word", "xcashuB" + "Q" * 60, 2, 0, 0, 0, "", ""))
    locked, open_ = token_b(["p2pk"], [5]), token_b(["plain"], [5])
    # A second token glued to a word is still seen: the lock check cannot be dodged.
    cases.append(("hidden second", locked + " and x" + open_, 3, 0, 0, 0, "", ""))
    cases.append(
        (
            "in a link",
            "https://wallet.example/?token=" + locked,
            1,
            5,
            1,
            1,
            "sat",
            "mint.example.com",
        )
    )
    cases.append(("garbage", "cashuB" + "Q" * 60, 2, 0, 0, 0, "", ""))
    cases.append(("bad base64", "cashuA" + "e" * 61, 2, 0, 0, 0, "", ""))
    return cases


def streams():
    """(name, compressed, plain) DEFLATE streams: stored, fixed and dynamic blocks, several blocks."""
    rng = random.Random(7)
    texts = [
        b"",
        b"a",
        b"pay: " + token_b(["p2pk"] * 3, [1, 2, 4]).encode(),
        token_a(["p2pk refund", "plain"], [16, 32]).encode() * 2,
        bytes(rng.randrange(256) for _ in range(1500)),
        b"abcabcabc" * 180,
    ]
    out = []
    for i, text in enumerate(texts):
        for level, strategy, label in (
            (0, zlib.Z_DEFAULT_STRATEGY, "stored"),
            (6, zlib.Z_FIXED, "fixed"),
            (9, zlib.Z_DEFAULT_STRATEGY, "dynamic"),
            (1, zlib.Z_HUFFMAN_ONLY, "huffman"),
        ):
            c = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
            data = b""
            # several blocks: flush in the middle
            half = len(text) // 2
            data += (
                c.compress(text[:half])
                + c.flush(zlib.Z_FULL_FLUSH)
                + c.compress(text[half:])
                + c.flush()
            )
            out.append((f"{label} {i}", data, text))
    return out


def header(path):
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "struct Stream { const char *name; const char *compressed; const char *plain; };",
        "static const Stream STREAMS[] = {",
    ]
    for name, data, text in streams():
        lines.append(
            "{" + ",".join(json.dumps(v) for v in (name, data.hex(), text.hex())) + "},"
        )
    lines += [
        "};",
        "struct TokenCase { const char *name; const char *text; int found; uint64_t amount; unsigned proofs;"
        " unsigned locked; const char *unit; const char *mint; };",
        "static const TokenCase TOKENS[] = {",
    ]
    for case in tokens():
        name, text, found, amount, proofs, locked, unit, mint = case
        lines.append(
            "{"
            + ",".join(
                [
                    json.dumps(name),
                    json.dumps(text),
                    str(found),
                    f"{amount}ULL",
                    str(proofs),
                    str(locked),
                    json.dumps(unit),
                    json.dumps(mint),
                ]
            )
            + "},"
        )
    lines.append("};")
    path.write_text("\n".join(lines) + "\n")

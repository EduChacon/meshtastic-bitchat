#!/usr/bin/env python3
"""Reference transcripts of Bitchat's private chat, made with an independent Noise implementation.

The bridge's own Noise code (src/bridge/BridgeNoise.cpp) must produce the same bytes for the same keys.
The transcripts come from the `noiseprotocol` Python package (Noise_XX_25519_ChaChaPoly_SHA256, empty prologue
and payloads, as both Bitchat apps use it); transport messages get Bitchat's framing, a 4-byte big-endian nonce
before each ciphertext. The result is committed as test/test_bitchat_bridge/noise_vectors.json, so the tests need no Python
package; this script only regenerates it:

    pip install noiseprotocol
    python3 tools/bridge/noise_vectors.py > test/test_bitchat_bridge/noise_vectors.json
"""

import hashlib
import json
import struct
import sys

from noise.connection import Keypair, NoiseConnection

NAME = b"Noise_XX_25519_ChaChaPoly_SHA256"


def connection(initiator, static, ephemeral):
    noise = NoiseConnection.from_name(NAME)
    if initiator:
        noise.set_as_initiator()
    else:
        noise.set_as_responder()
    noise.set_keypair_from_private_bytes(Keypair.STATIC, static)
    noise.set_keypair_from_private_bytes(Keypair.EPHEMERAL, ephemeral)
    noise.start_handshake()
    return noise


def key(seed):
    return hashlib.sha256(seed.encode()).digest()


def public(private):
    noise = NoiseConnection.from_name(NAME)
    noise.set_keypair_from_private_bytes(Keypair.STATIC, private)
    return noise.noise_protocol.keypairs["s"].public_bytes


def transcript(name):
    keys = {
        part: key(f"{name}/{part}")
        for part in (
            "initiator_static",
            "initiator_ephemeral",
            "responder_static",
            "responder_ephemeral",
        )
    }
    initiator = connection(True, keys["initiator_static"], keys["initiator_ephemeral"])
    responder = connection(False, keys["responder_static"], keys["responder_ephemeral"])
    message1 = initiator.write_message()
    responder.read_message(message1)
    message2 = responder.write_message()
    initiator.read_message(message2)
    message3 = initiator.write_message()
    responder.read_message(message3)
    assert initiator.handshake_finished and responder.handshake_finished
    transport = []
    for sender, receiver, direction, texts in (
        (
            initiator,
            responder,
            "initiator",
            [b"\x01hello bridge", b"", bytes(range(200))],
        ),
        (responder, initiator, "responder", [b"\x03message-id", b"\x01reply"]),
    ):
        for nonce, text in enumerate(texts):
            sealed = sender.encrypt(text)
            assert receiver.decrypt(sealed) == text
            transport.append(
                {
                    "from": direction,
                    "plain": text.hex(),
                    "wire": (struct.pack(">I", nonce) + sealed).hex(),
                }
            )
    return {
        "name": name,
        **{part: value.hex() for part, value in keys.items()},
        "initiator_public": public(keys["initiator_static"]).hex(),
        "responder_public": public(keys["responder_static"]).hex(),
        "message1": message1.hex(),
        "message2": message2.hex(),
        "message3": message3.hex(),
        "handshake_hash": initiator.get_handshake_hash().hex(),
        "transport": transport,
    }


def main():
    cases = [
        transcript(name) for name in ("phone-to-bridge", "bridge-to-phone", "third")
    ]
    json.dump(
        {"source": "noiseprotocol (Python), " + NAME.decode(), "cases": cases},
        sys.stdout,
        indent=1,
    )
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()

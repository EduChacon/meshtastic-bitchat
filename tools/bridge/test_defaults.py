#!/usr/bin/env python3
"""Run production default setup/persistence with host storage/channel fakes."""

import base64
import hashlib
import os
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
public_key = bytes(
    int(value, 16)
    for value in re.findall(
        r"0x([0-9a-f]{2})", (root / "src/bridge/BridgeDefaults.h").read_text()
    )
)
assert (
    public_key == hashlib.sha256(b"meshtastic-bitchat/public-text-channel/v1").digest()
)
assert (
    base64.b64encode(public_key).decode()
    in (root / "docs/bridge/DEFAULTS.md").read_text()
)


def block(source, signature):
    start = source.index(signature)
    opened = source.index("{", start)
    depth, end = 1, opened + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


with tempfile.TemporaryDirectory(prefix="bridge-defaults-") as directory:
    work = Path(directory)
    source = (root / "src/bridge/BitchatBridgeModule.cpp").read_text()
    header = (root / "src/bridge/BitchatBridgeModule.h").read_text()
    (work / "settings-under-test.inc").write_text(
        block(header, "struct Settings {")
        + " settings;\n"
        + block(header, "struct Note {")
        + " note;\n"
    )
    (work / "defaults-under-test.inc").write_text(
        "\n".join(
            block(source, signature)
            for signature in (
                "BitchatBridgeModule::BitchatBridgeModule()",
                "int8_t BitchatBridgeModule::readDirectSetting()",
                "bool BitchatBridgeModule::persist()",
                "uint8_t BitchatBridgeModule::provisionPublicChannel()",
                "void BitchatBridgeModule::applyDefaults()",
                "void BitchatBridgeModule::ensureDefaultChannel()",
                "void BitchatBridgeModule::updateClockWindow()",
            )
        )
    )
    binary = work / "defaults-tests"
    subprocess.run(
        [
            os.environ.get("CXX", "clang++"),
            "-std=c++17",
            "-O1",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I" + str(work),
            "-I" + str(root / "src"),
            str(root / "test/test_bitchat_bridge/test_defaults.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True, timeout=30)

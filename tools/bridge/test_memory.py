#!/usr/bin/env python3
"""Run production runtime allocation and BLE with a bounded host heap model."""

import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
tests = root / "test/test_bitchat_bridge"


def block(source, signature):
    start = source.index(signature)
    opened = source.index("{", start)
    depth, end = 1, opened + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


with tempfile.TemporaryDirectory(prefix="bridge-memory-") as directory:
    work = Path(directory)
    header = (root / "src/bridge/BitchatBridgeModule.h").read_text()
    source = (root / "src/bridge/BitchatBridgeModule.cpp").read_text()
    (work / "runtime-layout.inc").write_text(
        "\n".join(
            block(header, name) + ";" for name in ("struct RadioItem", "struct Runtime")
        )
    )
    (work / "runtime-code.inc").write_text(
        "\n".join(
            block(source, name)
            for name in (
                "bool BitchatBridgeModule::ensureRuntime()",
                "void BitchatBridgeModule::releaseRuntime()",
            )
        )
    )
    binary = work / "memory-tests"
    subprocess.run(
        [
            os.environ.get("CXX", "clang++"),
            "-std=c++17",
            "-O1",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-DMESHTASTIC_ENABLE_BITCHAT=1",
            "-I" + str(work),
            "-I" + str(tests / "stubs"),
            "-I" + str(root / "src"),
            str(tests / "test_memory.cpp"),
            str(root / "src/bridge/BridgeBle.cpp"),
            str(root / "src/bridge/BridgeCodec.cpp"),
            str(root / "src/bridge/BridgeDiagnostics.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True, timeout=60)

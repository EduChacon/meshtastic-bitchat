#!/usr/bin/env python3
"""Run the real bridge module end to end with simulated phones and radio; no hardware."""

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
tests = root / "test/test_bitchat_bridge"
parser = argparse.ArgumentParser()
parser.add_argument("--crypto-dir", type=Path, required=True)
crypto = parser.parse_args().crypto_dir.resolve()
if json.loads((crypto / "library.json").read_text()).get("version") != "0.4.0":
    raise SystemExit("Expected rweather Crypto 0.4.0")

with tempfile.TemporaryDirectory(prefix="bridge-flow-") as directory:
    binary = Path(directory) / "flow-tests"
    subprocess.run(
        [
            os.environ.get("CXX", "clang++"),
            "-std=c++17",
            "-O1",
            "-g",
            "-ffunction-sections",
            "-fdata-sections",
            "-Wl,--gc-sections",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-DMESHTASTIC_ENABLE_BITCHAT=1",
            "-I" + str(tests / "flow_stubs"),
            "-I" + str(tests / "stubs"),
            "-I" + str(root / "src"),
            "-I" + str(crypto),
            str(tests / "test_flow.cpp"),
            *(
                str(root / "src/bridge" / name)
                for name in (
                    "BitchatBridgeModule.cpp",
                    "BridgeBle.cpp",
                    "BridgeCodec.cpp",
                    "BridgeDiagnostics.cpp",
                    "BridgeHistory.cpp",
                    "BridgeState.cpp",
                    "BridgeText.cpp",
                    "BridgeTunnel.cpp",
                )
            ),
            *(
                str(crypto / (name + ".cpp"))
                for name in (
                    "Crypto",
                    "Hash",
                    "SHA256",
                    "SHA512",
                    "BigNumberUtil",
                    "Curve25519",
                    "Ed25519",
                    "HKDF",
                )
            ),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True, timeout=300)

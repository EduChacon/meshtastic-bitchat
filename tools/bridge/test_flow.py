#!/usr/bin/env python3
"""Run the real bridge module end to end with simulated phones and radio; no hardware."""

import argparse
import json
import os
import subprocess
import sys
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
    # Cashu tokens and DEFLATE streams for the payment tests, made by the same script as the MeshCore bridge's.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import cashu_vectors

    cashu_vectors.header(Path(directory) / "cashu_vectors.h")
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
            "-I" + str(directory),
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
                    "BridgeCashu.cpp",
                    "BridgeCodec.cpp",
                    "BridgeDiagnostics.cpp",
                    "BridgeHistory.cpp",
                    "BridgeInflate.cpp",
                    "BridgeNoise.cpp",
                    "BridgePayTunnel.cpp",
                    "BridgePayments.cpp",
                    "BridgePrivate.cpp",
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
                    "Cipher",
                    "AuthenticatedCipher",
                    "ChaCha",
                    "ChaChaPoly",
                    "Poly1305",
                )
            ),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True, timeout=300)

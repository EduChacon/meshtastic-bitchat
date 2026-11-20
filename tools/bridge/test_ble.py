#!/usr/bin/env python3
"""Run the real BLE adapter and the nRF52 storage shim with host fakes; no hardware access."""

import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
tests = root / "test/test_bitchat_bridge"
with tempfile.TemporaryDirectory(prefix="bridge-ble-") as directory:
    binary = Path(directory) / "ble-tests"
    subprocess.run(
        [
            os.environ.get("CXX", "clang++"),
            "-std=c++17",
            "-O1",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-DMESHTASTIC_ENABLE_BITCHAT=1",
            "-I" + str(tests / "stubs"),
            "-I" + str(root / "src"),
            str(tests / "test_ble.cpp"),
            str(root / "src/bridge/BridgeBle.cpp"),
            str(root / "src/bridge/BridgeCodec.cpp"),
            str(root / "src/bridge/BridgeDiagnostics.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True, timeout=30)
    # The nRF52 Preferences shim, on an in-memory filesystem.
    prefs = Path(directory) / "prefs-tests"
    subprocess.run(
        [
            os.environ.get("CXX", "clang++"),
            "-std=c++17",
            "-O1",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-DMESHTASTIC_ENABLE_BITCHAT=1",
            "-DARCH_NRF52=1",
            "-I" + str(tests / "prefs_stubs"),
            "-I" + str(root / "src/bridge/nrf52"),
            "-I" + str(root / "src"),
            str(tests / "test_prefs.cpp"),
            "-o",
            str(prefs),
        ],
        check=True,
    )
    subprocess.run([str(prefs)], check=True, timeout=30)

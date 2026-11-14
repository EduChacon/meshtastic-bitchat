#!/usr/bin/env python3
"""Exercise real bridge menu callbacks with a host screen lifecycle; no hardware."""

import hashlib
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opened = source.index("{", start)
    depth, end = 1, opened + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


with tempfile.TemporaryDirectory(prefix="bridge-menu-") as directory:
    work = Path(directory)
    module = (root / "src/bridge/BitchatBridgeModule.cpp").read_text()
    (work / "mode-under-test.inc").write_text(
        function(module, "bool BitchatBridgeModule::startBasicMode(")
        + function(module, "void BitchatBridgeModule::closeNativeWindow(")
    )
    source = (root / "src/bridge/BridgeMenu.cpp").read_text()
    (work / "menu-under-test.inc").write_text(
        source[source.index("namespace graphics") : source.rindex("#endif")]
    )
    fixture = root / "test/test_bitchat_bridge/fixtures/menu_v0.1.0-experimental.3.cpp"
    assert hashlib.sha256(fixture.read_bytes()).hexdigest() == (
        "bb4cd212406e06f4beedaf137f969f468f28767d15929adca4dcaafb8e1f755b"
    )
    old = fixture.read_text()
    (work / "menu-original-control.inc").write_text(
        old[old.index("namespace graphics") : old.rindex("#endif")]
    )
    for control in (False, True):
        binary = work / ("control" if control else "menu-tests")
        subprocess.run(
            [
                os.environ.get("CXX", "clang++"),
                "-std=c++17",
                "-O1",
                "-g",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-I" + str(work),
                *(["-DBRIDGE_OLD_MENU_CONTROL=1"] if control else []),
                str(root / "test/test_bitchat_bridge/test_menu.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True, timeout=30)

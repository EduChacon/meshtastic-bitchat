#!/usr/bin/env python3
"""Fault-inject the actual patched startup functions; no radio access."""

import argparse
import os
import runpy
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--nimble-dir", type=Path, required=True)
args = parser.parse_args()
patcher = runpy.run_path(str(root / "tools/bridge/patch_nimble.py"))


def function(source, signature):
    start = source.index(signature)
    opened = source.index("{", start)
    depth = 1
    end = opened + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


with tempfile.TemporaryDirectory(prefix="bridge-startup-") as directory:
    work = Path(directory)
    library = work / "NimBLE-Arduino"
    for name in patcher["PATCHES"]:
        target = library / "src" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.nimble_dir / "src" / name, target)
    patcher["apply"](library)
    patched = {path: path.read_bytes() for path in library.rglob("*") if path.is_file()}
    patcher["apply"](library)
    assert all(path.read_bytes() == data for path, data in patched.items())

    device = (library / "src/NimBLEDevice.cpp").read_text()
    port = (
        library / "src/nimble/porting/npl/freertos/src/nimble_port_freertos.c"
    ).read_text()
    integration = (root / "src/nimble/NimbleBluetooth.cpp").read_text()
    extracted = function(port, "esp_err_t esp_nimble_enable(")
    extracted += function(device, "void NimBLEDevice::init(")
    control = ""
    for name, signature, source in (
        (
            "nimble/porting/npl/freertos/src/nimble_port_freertos.c",
            "esp_err_t esp_nimble_enable(",
            port,
        ),
        ("NimBLEDevice.cpp", "void NimBLEDevice::init(", device),
    ):
        for old, new in reversed(patcher["PATCHES"][name][1]):
            source = source.replace(new, old, 1)
        control += function(source, signature)
    for signature in (
        "void NimbleBluetooth::setup(",
        "void NimbleBluetooth::deinit(",
        "bool NimbleBluetooth::isActive(",
        "bool NimbleBluetooth::isConnected(",
    ):
        body = function(integration, signature)
        extracted += body
        control += body
    (work / "startup-under-test.inc").write_text(extracted)
    (work / "startup-original-control.inc").write_text(control)
    damaged = library / "src/NimBLEDevice.h"
    damaged.write_text(damaged.read_text() + "\n// unexpected change\n")
    before = {path: path.read_bytes() for path in patched}
    try:
        patcher["apply"](library)
        raise AssertionError("Unknown dependency was accepted")
    except ValueError:
        assert all(path.read_bytes() == data for path, data in before.items())

    binary = work / "startup-tests"
    subprocess.run(
        [
            os.environ.get("CXX", "clang++"),
            "-std=c++17",
            "-O1",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I" + str(work),
            str(root / "test/test_bitchat_bridge/test_startup.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True, timeout=30)
    subprocess.run(
        [
            os.environ.get("CXX", "clang++"),
            "-std=c++17",
            "-O1",
            "-DBRIDGE_UNPATCHED_CONTROL=1",
            "-I" + str(work),
            str(root / "test/test_bitchat_bridge/test_startup.cpp"),
            "-o",
            str(work / "control"),
        ],
        check=True,
    )
    subprocess.run([str(work / "control")], check=True, timeout=10)

#!/usr/bin/env python3
"""Compile and run bridge host tests against the same rweather Crypto library."""

import argparse
import json
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--crypto-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--no-sanitize", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    crypto = args.crypto_dir.resolve()
    metadata = json.loads((crypto / "library.json").read_text())
    if metadata.get("version") != "0.4.0":
        raise SystemExit("Expected rweather Crypto 0.4.0")
    build = args.build_dir or Path(tempfile.mkdtemp(prefix="bridge-host-"))
    build.mkdir(parents=True, exist_ok=True)
    vectors = json.loads(
        (root / "test/test_bitchat_bridge/official_vectors.json").read_text()
    )
    header = [
        "#pragma once",
        "struct Vector { const char *name; const char *wire; const char *canonical; const char *key; };",
        "static const Vector VECTORS[] = {",
    ]
    for case in vectors["cases"]:
        header.append(
            "{"
            + ",".join(
                json.dumps(case[k]) for k in ("name", "wire", "canonical", "public_key")
            )
            + "},"
        )
    header.append("};")
    (build / "vectors.h").write_text("\n".join(header) + "\n")
    compiler = os.environ.get("CXX", "clang++")
    command = [
        compiler,
        "-std=c++17",
        "-O1",
        "-g",
        "-ffunction-sections",
        "-fdata-sections",
        "-I" + str(root / "src"),
        "-I" + str(crypto),
        "-I" + str(build),
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
    ]
    if not args.no_sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    command.append(
        "-Wl,-dead_strip" if platform.system() == "Darwin" else "-Wl,--gc-sections"
    )
    command += [
        str(root / "src/bridge" / f)
        for f in (
            "BridgeCodec.cpp",
            "BridgeHistory.cpp",
            "BridgeState.cpp",
            "BridgeText.cpp",
            "BridgeTunnel.cpp",
        )
    ]
    command += [
        str(crypto / (name + ".cpp"))
        for name in (
            "Crypto",
            "Hash",
            "SHA256",
            "SHA512",
            "BigNumberUtil",
            "Curve25519",
            "Ed25519",
        )
    ]
    binary = build / "bridge-tests"
    command += [str(root / "test/test_bitchat_bridge/test_main.cpp"), "-o", str(binary)]
    print(
        "Compiling host tests; Crypto 0.4.0; sanitizers:",
        not args.no_sanitize,
        flush=True,
    )
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)
    # Cashu payments: inflater, token reader and the tunnel's parts, the same tests as the MeshCore bridge's.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import cashu_vectors

    cashu_vectors.header(build / "cashu_vectors.h")
    cashu = [
        c for c in command if not c.endswith(".cpp") and c not in ("-o", str(binary))
    ]
    cashu += [
        str(root / "src/bridge" / f)
        for f in (
            "BridgeCashu.cpp",
            "BridgeCodec.cpp",
            "BridgeInflate.cpp",
            "BridgePayTunnel.cpp",
        )
    ]
    cashu += [
        str(root / "test/test_bitchat_bridge/test_cashu.cpp"),
        "-o",
        str(build / "cashu-tests"),
    ]
    subprocess.run(cashu, check=True)
    subprocess.run([str(build / "cashu-tests")], check=True)


if __name__ == "__main__":
    main()

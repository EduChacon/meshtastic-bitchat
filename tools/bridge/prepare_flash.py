#!/usr/bin/env python3
"""Private read-only ESP32-S3 backup/preflight. Never writes or erases radio flash."""

import argparse
import importlib.metadata
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from verify_artifact import MODELS, digest, verify_dump, verify_package


def prepare(archive, model, port, directory, baud=460800, run=subprocess.run):
    manifest = verify_package(archive, model)
    if not re.fullmatch(r"COM[1-9][0-9]*|/dev/[A-Za-z0-9._/-]+", port):
        raise ValueError("Invalid serial port")
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    private = Path(tempfile.mkdtemp(prefix="backup-", dir=directory))
    print(f"Private backup directory: {private}", flush=True)
    base = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        str(baud),
        "--after",
        "no_reset",
    ]

    def read_command(arguments, log_name, timeout):
        result = run(
            base + arguments,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
            timeout=timeout,
            check=False,
        )
        (private / log_name).write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            raise ValueError(
                f"Serial read failed; private details: {private / log_name}"
            )
        return result.stdout

    print(
        "Checking ESP32-S3 and flash capacity; radio temporarily enters ROM download mode.",
        flush=True,
    )
    identity = read_command(["flash_id"], "flash-id-private.txt", 90)
    megabytes = manifest["flash_bytes"] // 2**20
    if not re.search(rf"Detected flash size:\s*{megabytes}\s*MB\b", identity):
        raise ValueError(
            f"{megabytes} MB flash not confirmed; no full read or write attempted"
        )
    if "Chip is ESP32-S3" not in identity:
        raise ValueError("ESP32-S3 not confirmed")
    backup = private / "flash-private.bin"
    print(
        f"Reading {megabytes} MB. This can take several minutes. No radio flash is being written.",
        flush=True,
    )
    read_command(
        ["read_flash", "0", hex(manifest["flash_bytes"]), str(backup)],
        "read-private.txt",
        1200,
    )
    data = backup.read_bytes()
    (private / "SHA256SUMS").write_text(
        f"{digest(data)}  {backup.name}\n", encoding="ascii"
    )
    verify_dump(data, manifest)
    (private / "preflight.txt").write_text(
        f"PASS: backup and supplied layout match. Declared model: {model}\n"
        f"Package SHA256: {digest(archive.read_bytes())}\n"
        "Physical model/band/antenna must be independently confirmed. No write authorized by this result.\n",
        encoding="utf-8",
    )
    print("PASS: private backup saved; package and layout match. Serial port released.")
    print(
        "Radio remains in ROM download mode. Use only Update in the official flasher after your final checks."
    )
    print(
        "Keep the entire private directory local. It may contain keys, messages and crash memory."
    )
    return private


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--model", choices=MODELS, required=True)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, choices=(115200, 460800), default=460800)
    parser.add_argument(
        "--read-backup",
        action="store_true",
        help="Authorize a read and temporary ROM reset only",
    )
    args = parser.parse_args()
    if not args.read_backup:
        parser.error(
            "--read-backup is required; this temporarily resets the radio but never writes flash"
        )
    try:
        if importlib.metadata.version("esptool") != "4.11.0":
            raise ValueError("Use esptool==4.11.0 in an isolated Python environment")
        if os.name == "nt":
            if not os.environ.get("LOCALAPPDATA"):
                raise ValueError(
                    "LOCALAPPDATA unavailable; no backup directory selected"
                )
            directory = Path(os.environ["LOCALAPPDATA"]) / "MeshtasticBitchat/backups"
        else:
            directory = Path.home() / ".local/share/meshtastic-bitchat/backups"
        prepare(args.archive, args.model, args.port, directory, args.baud)
    except (
        ValueError,
        KeyError,
        OSError,
        zipfile.BadZipFile,
        importlib.metadata.PackageNotFoundError,
        subprocess.TimeoutExpired,
    ) as error:
        raise SystemExit(
            f"STOP: {error}\nDo not erase or flash. Any completed backup remains private and preserved."
        ) from error


if __name__ == "__main__":
    main()

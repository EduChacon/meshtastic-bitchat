#!/usr/bin/env python3
"""Read-only preflight. This program has no serial, network, flash or erase code."""

import argparse
import hashlib
import json
import struct
import zipfile
from pathlib import Path

# Model: (Meshtastic hardware model, flash bytes). All are ESP32-S3 with the stock application slot at 0x10000.
MODELS = {
    "heltec-v3": (43, 8388608),
    "heltec-v4": (110, 16777216),
    "heltec-wsl-v3": (44, 8388608),
    "heltec-wireless-tracker": (48, 8388608),
    "heltec-wireless-paper": (49, 8388608),
    "tbeam-s3-core": (12, 8388608),
    "seeed-xiao-s3": (81, 8388608),
    "t-deck": (50, 16777216),
    "station-g2": (31, 16777216),
    "m5stack-cardputer-adv": (112, 8388608),
}
# Reviewed size of the first application slot for each flash size.
APP_SLOT = {8388608: 0x330000, 16777216: 0x640000}


def digest(value):
    return hashlib.sha256(value).hexdigest()


def verify_dump(data, manifest):
    if (
        manifest.get("flash_bytes") not in APP_SLOT
        or len(data) != manifest["flash_bytes"]
        or manifest.get("partition_table_bytes") != 3072
    ):
        raise ValueError(
            "Expected complete flash backup and reviewed partition table size"
        )
    if digest(data[0x8000:0x8C00]) != manifest["partition_table_sha256"]:
        raise ValueError("Partition layout differs; stop, do not erase")
    if digest(data[0xE000:0x10000]) != manifest["factory_boot_selection_sha256"]:
        raise ValueError("OTA selection differs; stop, do not overwrite boot metadata")


def verify_package(archive, model):
    with zipfile.ZipFile(archive) as package:
        entries = package.infolist()
        names = [p.filename for p in entries]
        if len(names) != len(set(names)) or len(names) > 8:
            raise ValueError("Duplicate or unexpected archive members")
        if any(Path(n).name != n or n.startswith(".") for n in names):
            raise ValueError("Unsafe archive name")
        if sum(p.file_size for p in entries) > 8 * 1024 * 1024:
            raise ValueError("Oversized archive")
        manifest = json.loads(package.read("bridge-manifest.json"))
        if (
            manifest.get("schema") != 1
            or manifest.get("model") != model
            or manifest.get("hardware_model") != MODELS[model][0]
        ):
            raise ValueError("Wrong model or manifest version")
        if (
            manifest.get("chip") != "esp32-s3"
            or manifest.get("flash_bytes") != MODELS[model][1]
        ):
            raise ValueError("Unreviewed chip or flash size")
        name = manifest["application"]
        if not name.startswith(f"firmware-{model}-bridge-") or not name.endswith(
            "-update.bin"
        ):
            raise ValueError("Not an application update")
        if set(names) != {
            name,
            "bridge-manifest.json",
            "SHA256SUMS",
            "LICENSE",
            "LEIA-ANTES.txt",
        }:
            raise ValueError(
                "Unexpected files; factory and erase packages are not accepted"
            )
        data = package.read(name)
        if len(data) != manifest.get("application_bytes") or digest(
            data
        ) != manifest.get("application_sha256"):
            raise ValueError("Application integrity mismatch")
        if (
            not 24 < len(data) <= APP_SLOT[manifest["flash_bytes"]]
            or data[0] != 0xE9
            or struct.unpack_from("<H", data, 12)[0] != 9
        ):
            raise ValueError("Invalid or wrong-chip image")
        if (
            manifest.get("environment") != model + "-bitchat"
            or manifest["environment"].encode() not in data
        ):
            raise ValueError("Missing environment marker")
        if manifest.get("update_offset") != 65536 or manifest.get("erase") is not False:
            raise ValueError("Unreviewed write mode")
        if (
            manifest.get("contains_bootloader") is not False
            or manifest.get("contains_partitions") is not False
        ):
            raise ValueError(
                "Bootloader or partition writing is prohibited by this package policy"
            )
        covered = set()
        for line in package.read("SHA256SUMS").decode().splitlines():
            checksum, filename = line.split("  ", 1)
            if (
                filename in covered
                or filename not in names
                or digest(package.read(filename)) != checksum
            ):
                raise ValueError("Archive integrity mismatch")
            covered.add(filename)
        if covered != set(names) - {"SHA256SUMS"}:
            raise ValueError("Incomplete archive checksums")
        return manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument(
        "--model",
        choices=MODELS,
        required=True,
        help="Model independently confirmed from the device; this program cannot identify hardware",
    )
    parser.add_argument("--package-only", action="store_true")
    parser.add_argument("--partition-table", type=Path)
    parser.add_argument("--otadata", type=Path)
    args = parser.parse_args()
    try:
        manifest = verify_package(args.archive, args.model)
        if args.package_only:
            print(
                "PASS: package integrity and declared target only. Hardware and installation NOT validated."
            )
            return
        if args.partition_table is None or args.otadata is None:
            raise ValueError(
                "Read-only device partition and OTA-selection dumps are required; no write authorized"
            )
        if (
            digest(args.partition_table.read_bytes())
            != manifest["partition_table_sha256"]
        ):
            raise ValueError(
                "Partition layout differs; stop for manual review, do not erase"
            )
        if (
            digest(args.otadata.read_bytes())
            != manifest["factory_boot_selection_sha256"]
        ):
            raise ValueError(
                "OTA selection differs; stop for manual review, do not overwrite boot metadata"
            )
        print(
            "PASS: package, declared model and supplied layout match. No radio accessed or written."
        )
        print(
            "Still required: actual device/revision/band, private backup, stable power, recovery and separate approval."
        )
    except (ValueError, KeyError, OSError, zipfile.BadZipFile) as error:
        raise SystemExit("STOP: " + str(error)) from error


if __name__ == "__main__":
    main()

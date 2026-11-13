"""Destructive-install prevention tests using synthetic images; no hardware access."""

import hashlib
import importlib.util
import json
import runpy
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "preflight", ROOT / "tools/bridge/verify_artifact.py"
)
PREFLIGHT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREFLIGHT)
PACKAGER = runpy.run_path(str(ROOT / "tools/bridge/package_release.py"))


class PackageTests(unittest.TestCase):
    def test_stale_local_build_rejected(self):
        source = "1234567" + "0" * 33
        filename = "firmware-heltec-v3-bitchat-2.7.26.1234567.bin"
        check = PACKAGER["check_build_provenance"]
        check(b"2.7.26.1234567\0", filename, "heltec-v3-bitchat", source)
        for image, name in (
            (b"2.7.26.abcdef0\0", filename),
            (b"2.7.26.1234567\0", filename.replace("1234567", "abcdef0")),
            (b"2.7.26.1234567-suffix\0", filename),
        ):
            with self.assertRaises(ValueError):
                check(image, name, "heltec-v3-bitchat", source)

    def archive(
        self,
        directory,
        model="heltec-v3",
        corrupt=False,
        extra=None,
        offset=65536,
        environment=None,
        checksums=True,
    ):
        image = bytearray(256)
        image[0] = 0xE9
        image[12] = 9
        image[40 : 40 + len(model) + 8] = (model + "-bitchat").encode()
        filename = f"firmware-{model}-bridge-test-update.bin"
        manifest = {
            "schema": 1,
            "model": model,
            "hardware_model": PREFLIGHT.MODELS[model],
            "chip": "esp32-s3",
            "flash_bytes": 8388608,
            "application": filename,
            "application_sha256": hashlib.sha256(image).hexdigest(),
            "application_bytes": len(image),
            "environment": model + "-bitchat",
            "update_offset": offset,
            "erase": False,
            "contains_bootloader": False,
            "contains_partitions": False,
        }
        if corrupt:
            image[-1] ^= 1
        if environment is not None:
            manifest["environment"] = environment
        entries = {
            filename: bytes(image),
            "bridge-manifest.json": json.dumps(manifest).encode(),
            "LICENSE": b"test only",
            "LEIA-ANTES.txt": b"test only",
        }
        entries["SHA256SUMS"] = "".join(
            f"{hashlib.sha256(data).hexdigest()}  {name}\n"
            for name, data in entries.items()
        ).encode()
        if not checksums:
            entries["SHA256SUMS"] = b""
        if extra:
            entries[extra] = b"unexpected"
        path = Path(directory) / "package.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in entries.items():
                archive.writestr(name, data)
        return path

    def test_matching_target(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(
                PREFLIGHT.verify_package(self.archive(directory), "heltec-v3")[
                    "hardware_model"
                ],
                43,
            )

    def test_wrong_board_even_same_chip(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                PREFLIGHT.verify_package(
                    self.archive(directory), "m5stack-cardputer-adv"
                )

    def test_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                PREFLIGHT.verify_package(
                    self.archive(directory, corrupt=True), "heltec-v3"
                )

    def test_factory_offset(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                PREFLIGHT.verify_package(self.archive(directory, offset=0), "heltec-v3")

    def test_missing_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                PREFLIGHT.verify_package(
                    self.archive(directory, environment=""), "heltec-v3"
                )

    def test_incomplete_checksums(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                PREFLIGHT.verify_package(
                    self.archive(directory, checksums=False), "heltec-v3"
                )

    def test_factory_and_traversal_files(self):
        for extra in (
            "firmware.factory.bin",
            "../firmware.bin",
            "erase.sh",
            "partitions.bin",
        ):
            with tempfile.TemporaryDirectory() as directory:
                with self.assertRaises(ValueError):
                    PREFLIGHT.verify_package(
                        self.archive(directory, extra=extra), "heltec-v3"
                    )


if __name__ == "__main__":
    unittest.main()

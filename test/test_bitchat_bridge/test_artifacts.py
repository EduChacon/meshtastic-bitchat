"""Destructive-install prevention tests using synthetic images; no hardware access."""

import hashlib
import importlib.util
import json
import re
import runpy
import sys
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
        flash=None,
    ):
        image = bytearray(256)
        image[0] = 0xE9
        image[12] = 9
        image[40 : 40 + len(model) + 8] = (model + "-bitchat").encode()
        filename = f"firmware-{model}-bridge-test-update.bin"
        manifest = {
            "schema": 1,
            "model": model,
            "hardware_model": PREFLIGHT.MODELS[model][0],
            "chip": "esp32-s3",
            "flash_bytes": flash or PREFLIGHT.MODELS[model][1],
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

    def test_every_listed_board_has_a_build_and_a_ci_job(self):
        ini = (ROOT / "variants/bridge/platformio.ini").read_text()
        built = set(re.findall(r"^\[env:(.+)-bitchat\]$", ini, re.M))
        self.assertEqual(built, set(PREFLIGHT.MODELS))
        self.assertEqual(set(PACKAGER["BOARDS"]), {m + "-bitchat" for m in built})
        workflow = (ROOT / ".github/workflows/bridge.yml").read_text()
        matrix = re.search(r"environment:\s*\[(.*?)\]", workflow, re.S).group(1)
        self.assertEqual(
            {name.strip() for name in matrix.split(",") if name.strip()},
            set(PACKAGER["BOARDS"]),
        )
        stock = "\n".join(
            p.read_text() for p in (ROOT / "variants/esp32s3").glob("*/platformio.ini")
        )
        for model in built:
            declared = re.search(
                rf"^\[env:{re.escape(model)}\][ \t]*\n(?:(?!\[).*\n)*?"
                r"custom_meshtastic_hw_model = (\d+)",
                stock,
                re.M,
            )
            # Not every stock environment declares its number; those that do must agree.
            if declared:
                self.assertEqual(
                    int(declared.group(1)), PREFLIGHT.MODELS[model][0], model
                )

    def test_single_zip_gives_the_flasher_one_file_per_board(self):
        sys.path.insert(0, str(ROOT / "tools/bridge"))
        bundler = runpy.run_path(str(ROOT / "tools/bridge/bundle_release.py"))
        with tempfile.TemporaryDirectory() as directory:
            release = Path(directory) / "release"
            release.mkdir()
            for model in PREFLIGHT.MODELS:
                name = f"firmware-{model}-bridge-9.9.9-abcdef0-update.bin"
                (release / name).write_bytes(model.encode())
            archive = bundler["bundle"](
                release, Path(directory), "9.9.9", ROOT / "LICENSE"
            )
            self.assertEqual(archive.name, "meshtastic-bitchat-9.9.9.zip")
            with zipfile.ZipFile(archive) as package:
                names = package.namelist()
                self.assertEqual(len(names), len(PREFLIGHT.MODELS) + 3)
                for model in PREFLIGHT.MODELS:
                    chosen = bundler["flasher_choice"](names, model)
                    self.assertEqual(len(chosen), 1, model)
                    self.assertEqual(package.read(chosen[0]), model.encode())
                # Colour-interface targets are not built: the flasher finds nothing for them.
                self.assertEqual(bundler["flasher_choice"](names, "t-deck-tft"), [])
                self.assertEqual(bundler["flasher_choice"](names, "heltec-v4-tft"), [])
                for line in package.read("SHA256SUMS").decode().splitlines():
                    checksum, name = line.split("  ", 1)
                    self.assertEqual(
                        hashlib.sha256(package.read(name)).hexdigest(), checksum
                    )
            (
                release / "firmware-heltec-v3-bridge-9.9.8-abcdef0-update.bin"
            ).write_bytes(b"x")
            with self.assertRaises(SystemExit):
                bundler["bundle"](release, Path(directory), "9.9.9", ROOT / "LICENSE")

    def test_each_board_accepts_only_its_own_package(self):
        for model, (hardware, flash) in PREFLIGHT.MODELS.items():
            with tempfile.TemporaryDirectory() as directory:
                archive = self.archive(directory, model=model)
                manifest = PREFLIGHT.verify_package(archive, model)
                self.assertEqual(
                    (manifest["hardware_model"], manifest["flash_bytes"]),
                    (hardware, flash),
                )
                for other in PREFLIGHT.MODELS:
                    if other != model:
                        with self.assertRaises(ValueError):
                            PREFLIGHT.verify_package(archive, other)

    def test_flash_size_must_match_the_board(self):
        for model, wrong in (("heltec-v4", 8388608), ("heltec-v3", 16777216)):
            with tempfile.TemporaryDirectory() as directory:
                with self.assertRaises(ValueError):
                    PREFLIGHT.verify_package(
                        self.archive(directory, model=model, flash=wrong), model
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

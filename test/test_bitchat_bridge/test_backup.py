"""Exercise real backup orchestration with a fake serial subprocess and synthetic data."""

import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace

import test_artifacts

PREFLIGHT, ROOT = test_artifacts.PREFLIGHT, test_artifacts.ROOT

sys.path.insert(0, str(ROOT / "tools/bridge"))
SPEC = importlib.util.spec_from_file_location(
    "backup", ROOT / "tools/bridge/prepare_flash.py"
)
BACKUP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BACKUP)


class BackupTests(unittest.TestCase):
    def fixture(self, directory, model="heltec-v3"):
        archive = test_artifacts.PackageTests().archive(directory, model=model)
        data = bytes(PREFLIGHT.MODELS[model][1])
        with zipfile.ZipFile(archive) as source:
            entries = {name: source.read(name) for name in source.namelist()}
        manifest = json.loads(entries["bridge-manifest.json"])
        manifest.update(
            partition_table_bytes=3072,
            partition_table_sha256=PREFLIGHT.digest(data[0x8000:0x8C00]),
            factory_boot_selection_sha256=PREFLIGHT.digest(data[0xE000:0x10000]),
        )
        entries["bridge-manifest.json"] = json.dumps(manifest).encode()
        entries["SHA256SUMS"] = "".join(
            f"{PREFLIGHT.digest(value)}  {name}\n"
            for name, value in entries.items()
            if name != "SHA256SUMS"
        ).encode()
        with zipfile.ZipFile(archive, "w") as target:
            for name, value in entries.items():
                target.writestr(name, value)
        return archive, data

    def test_read_only_success_and_private_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, data = self.fixture(directory)
            commands = []

            def run(command, **kwargs):
                commands.append(command)
                self.assertEqual(command[command.index("--after") + 1], "no_reset")
                if "flash_id" in command:
                    return SimpleNamespace(
                        returncode=0,
                        stdout="Chip is ESP32-S3\nDetected flash size: 8 MB\nsynthetic-private-id",
                    )
                self.assertIn("read_flash", command)
                Path(command[-1]).write_bytes(data)
                return SimpleNamespace(
                    returncode=0, stdout="synthetic private read log"
                )

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                private = BACKUP.prepare(
                    archive, "heltec-v3", "COM3", Path(directory) / "private", run=run
                )
            self.assertEqual(len(commands), 2)
            self.assertEqual((private / "flash-private.bin").read_bytes(), data)
            self.assertTrue((private / "preflight.txt").exists())
            self.assertNotIn("synthetic-private-id", output.getvalue())
            self.assertNotIn("write_flash", str(commands))
            self.assertNotIn("erase_flash", str(commands))

    def test_sixteen_megabyte_board_reads_its_whole_flash(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, data = self.fixture(directory, "t-deck")
            commands = []

            def run(command, **kwargs):
                commands.append(command)
                if "flash_id" in command:
                    return SimpleNamespace(
                        returncode=0,
                        stdout="Chip is ESP32-S3\nDetected flash size: 16 MB",
                    )
                Path(command[-1]).write_bytes(data)
                return SimpleNamespace(returncode=0, stdout="")

            with contextlib.redirect_stdout(io.StringIO()):
                private = BACKUP.prepare(
                    archive, "t-deck", "COM3", Path(directory) / "private", run=run
                )
            self.assertIn("0x1000000", commands[1])
            self.assertEqual(len((private / "flash-private.bin").read_bytes()), 2**24)
            # The same radio reporting 8 MB is refused before any full read.
            commands.clear()

            def small(command, **kwargs):
                commands.append(command)
                return SimpleNamespace(
                    returncode=0, stdout="Chip is ESP32-S3\nDetected flash size: 8 MB"
                )

            with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(
                ValueError
            ):
                BACKUP.prepare(
                    archive, "t-deck", "COM4", Path(directory) / "private", run=small
                )
            self.assertEqual(len(commands), 1)

    def test_wrong_chip_capacity_or_read_failure_stops(self):
        for identity, result in (
            ("Chip is ESP32-S3\nDetected flash size: 4 MB", 0),
            ("Chip is ESP32\nDetected flash size: 8 MB", 0),
            ("failed", 2),
        ):
            with tempfile.TemporaryDirectory() as directory:
                archive, _ = self.fixture(directory)
                commands = []

                def run(command, **kwargs):
                    commands.append(command)
                    return SimpleNamespace(returncode=result, stdout=identity)

                with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(
                    ValueError
                ):
                    BACKUP.prepare(
                        archive,
                        "heltec-v3",
                        "COM3",
                        Path(directory) / "private",
                        run=run,
                    )
                self.assertEqual(len(commands), 1)

    def test_incomplete_backup_and_layout_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, data = self.fixture(directory)
            manifest = PREFLIGHT.verify_package(archive, "heltec-v3")
            for altered in (
                data[:-1],
                data[:0x8000] + b"x" + data[0x8001:],
                data[:0xE000] + b"x" + data[0xE001:],
            ):
                with self.assertRaises(ValueError):
                    PREFLIGHT.verify_dump(altered, manifest)


if __name__ == "__main__":
    unittest.main()

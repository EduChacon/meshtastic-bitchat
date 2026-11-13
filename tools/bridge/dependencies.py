#!/usr/bin/env python3
"""Record resolved dependency contents and package corresponding library sources."""

import argparse
import hashlib
import json
import os
import subprocess
import tarfile
from pathlib import Path

from package_release import BOARDS


def source_files(directory):
    for path in sorted(directory.rglob("*")):
        relative = path.relative_to(directory)
        if (
            path.is_file()
            and not path.is_symlink()
            and not any(
                part in {".git", ".pio", "__pycache__", ".DS_Store"}
                for part in relative.parts
            )
            and path.name != ".piopm"
        ):
            yield path, relative.as_posix()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", choices=BOARDS, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    core = Path(os.environ["PLATFORMIO_CORE_DIR"])
    libraries = root / ".pio/libdeps" / args.environment
    records = []
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / f"dependencies-{args.environment}.tar.gz"
    with tarfile.open(archive, "w:gz") as bundle:
        for library in sorted(p for p in libraries.iterdir() if p.is_dir()):
            files = []
            for path, name in source_files(library):
                files.append(
                    {
                        "path": name,
                        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    }
                )
                bundle.add(
                    path, arcname=f"libraries/{library.name}/{name}", recursive=False
                )
            records.append({"directory": library.name, "files": files})
    packages = []
    for metadata in sorted((core / "packages").glob("*/package.json")):
        value = json.loads(metadata.read_text())
        packages.append(
            {
                "name": value.get("name"),
                "version": value.get("version"),
                "manifest_sha256": hashlib.sha256(metadata.read_bytes()).hexdigest(),
            }
        )
    report = {
        "schema": 1,
        "source_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root, text=True
        ).strip(),
        "environment": args.environment,
        "libraries": records,
        "packages": packages,
        "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "security_status": "Content inventory only; not a vulnerability-free certification",
    }
    (args.output / f"dependencies-{args.environment}.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )
    print(f"Recorded {len(records)} libraries and {len(packages)} tool packages")


if __name__ == "__main__":
    main()

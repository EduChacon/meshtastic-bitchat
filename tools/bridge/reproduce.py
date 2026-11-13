#!/usr/bin/env python3
"""Compare clean application builds; never invoke an upload target."""

import argparse
import hashlib
import subprocess
from pathlib import Path

from package_release import BOARDS


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", choices=BOARDS, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = root / ".pio/build" / args.environment

    def digest():
        images = [
            p
            for p in build.glob("firmware-*.bin")
            if not p.name.endswith(".factory.bin")
        ]
        if len(images) != 1:
            raise SystemExit("Expected one application image")
        return hashlib.sha256(images[0].read_bytes()).hexdigest()

    before = digest()
    subprocess.run(
        ["pio", "run", "-e", args.environment, "-t", "clean"], cwd=root, check=True
    )
    subprocess.run(
        ["pio", "run", "-e", args.environment, "-t", "buildprog"], cwd=root, check=True
    )
    after = digest()
    if before != after:
        raise SystemExit(f"FAIL: applications differ: {before} != {after}")
    print(f"PASS: clean rebuild identical: {args.environment}: {after}")


if __name__ == "__main__":
    main()

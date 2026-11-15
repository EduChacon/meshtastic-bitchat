#!/usr/bin/env python3
"""Country packages: the build flag, the title or the file-name form of an entry of regions.json."""

import argparse
import json
from pathlib import Path

REGIONS = json.loads(Path(__file__).with_name("regions.json").read_text(encoding="utf-8"))


def flag(name):
    return f"-DBRIDGE_FACTORY_REGION=meshtastic_Config_LoRaConfig_RegionCode_{REGIONS[name]['code']}"


def slug(name):
    return REGIONS[name]["title"].replace(" ", "-")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("region", nargs="?", choices=REGIONS)
    parser.add_argument("--list", action="store_true", help="print the region names")
    parser.add_argument("--slug", action="store_true", help="print the title as used in file names")
    args = parser.parse_args()
    if args.list:
        print(" ".join(REGIONS))
    elif not args.region:
        parser.error("a region is required")
    else:
        print(slug(args.region) if args.slug else flag(args.region))


if __name__ == "__main__":
    main()

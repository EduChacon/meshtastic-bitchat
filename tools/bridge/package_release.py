#!/usr/bin/env python3
"""Package application-only updates; never connect to a radio or include an eraser."""

import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_artifact import APP_SLOT, MODELS  # noqa: E402

BOARDS = {model + "-bitchat": (model, *target) for model, target in MODELS.items()}
VERSION = "1.0.0-beta.12"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def check_build_provenance(image, filename, environment, source):
    marker = f"2.7.26.{source[:7]}"
    if (
        filename != f"firmware-{environment}-{marker}.bin"
        or marker.encode() + b"\0" not in image
    ):
        raise ValueError(
            "Stale or mismatched application; rebuild the current commit before packaging"
        )


def partitions(data):
    result = []
    for offset in range(0, len(data), 32):
        entry = data[offset : offset + 32]
        if len(entry) != 32 or entry[:2] != b"\xaa\x50":
            break
        _, kind, subtype, address, size, label, flags = struct.unpack(
            "<HBBII16sI", entry
        )
        result.append(
            {
                "name": label.split(b"\0")[0].decode("ascii"),
                "type": kind,
                "subtype": subtype,
                "offset": address,
                "size": size,
                "flags": flags,
            }
        )
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", required=True, choices=BOARDS)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    parser.add_argument("--region", help="country package this build was made for (tools/bridge/regions.json)")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    regions = json.loads(Path(__file__).with_name("regions.json").read_text(encoding="utf-8"))
    if args.region and args.region not in regions:
        raise SystemExit(f"Unknown region {args.region}")
    country = regions.get(args.region)
    if subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=normal"],
        cwd=root,
        text=True,
    ).strip():
        raise SystemExit(
            "Refusing to package uncommitted source; commit and rebuild first"
        )
    build = root / ".pio/build" / args.environment
    model, hardware, flash = BOARDS[args.environment]
    candidates = sorted(build.glob("firmware-*.bin"))
    candidates = [p for p in candidates if not p.name.endswith(".factory.bin")]
    if len(candidates) != 1:
        raise SystemExit(
            "Expected exactly one application image; clean stale build outputs first"
        )
    application = candidates[0].read_bytes()
    table = (build / "partitions.bin").read_bytes()
    layout = partitions(table)
    app = next((p for p in layout if p["name"] == "app0"), None)
    if not app or app["offset"] != 0x10000 or app["size"] != APP_SLOT[flash]:
        raise SystemExit("Unreviewed partition layout")
    if not (24 < len(application) <= app["size"]) or application[0] != 0xE9:
        raise SystemExit("Invalid application image")
    if struct.unpack_from("<H", application, 12)[0] != 9:
        raise SystemExit("Expected ESP32-S3 image, not a factory or another chip image")
    if args.environment.encode() not in application:
        raise SystemExit("Build environment marker is absent")
    source = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True
    ).strip()
    check_build_provenance(application, candidates[0].name, args.environment, source)
    epoch = int(os.environ["SOURCE_DATE_EPOCH"])
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    boot_selection = (
        core / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    )
    libraries = []
    for metadata in sorted(
        (root / ".pio/libdeps" / args.environment).glob("*/library.json")
    ):
        value = json.loads(metadata.read_text())
        libraries.append(
            {
                "name": value.get("name"),
                "version": value.get("version"),
                "license": value.get("license"),
                "repository": value.get("repository"),
            }
        )
    filename = f"firmware-{model}-bridge-{VERSION}-{source[:7]}-update.bin"
    manifest = {
        "schema": 1,
        "version": VERSION,
        "source_commit": source,
        "upstream_commit": "54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb",
        "bitchat_reference_commit": "30c58eee349e172b37f69faa2113c9d5f2c76c23",
        "environment": args.environment,
        "model": model,
        "hardware_model": hardware,
        "chip": "esp32-s3",
        "image_chip_id": 9,
        "flash_bytes": flash,
        "application": filename,
        "application_sha256": sha256(application),
        "application_bytes": len(application),
        "update_offset": 65536,
        "partition_table_sha256": sha256(table),
        "partition_table_bytes": len(table),
        "partitions": layout,
        "factory_boot_selection_sha256": sha256(boot_selection.read_bytes()),
        "source_date_epoch": epoch,
        "platformio": "6.2.0",
        "platform": "espressif32@6.13.0",
        "libraries": libraries,
        "hardware_tested": False,
        "feature_default": "ON: local BLE and dedicated public Bitchat text channel; laboratory tunnel OFF",
        "install_status": "documented-not-tested; preflight and separate approval required",
        "contains_bootloader": False,
        "contains_partitions": False,
        "erase": False,
    }
    if country:
        manifest["factory_region"] = country["code"]
        manifest["factory_region_package"] = country["title"]
    args.output.mkdir(parents=True, exist_ok=True)
    payloads = {
        filename: application,
        "bridge-manifest.json": (
            json.dumps(manifest, indent=2, sort_keys=True) + "\n"
        ).encode(),
        "LICENSE": (root / "LICENSE").read_bytes(),
        "LEIA-ANTES.txt": (
            "VERSAO BETA. Nenhum ensaio desta revisao em hardware. BLE e texto publico Bitchat ativos por padrao.\n"
            "No primeiro boot desta revisao: cria canal publico Bitchat em espaco livre; nao exporta o primario.\n"
            "Todo texto publico do #mesh ao alcance e publicado no canal LoRa Bitchat. Leia docs/bridge/CHANGES.md.\n"
            "Sem hora confiavel, o primeiro telefone Bitchat que conecta fornece a hora.\n"
            f"Alvo exclusivo: {model}; ESP32-S3; {flash // 2**20} MB; app0 0x10000.\n"
            "Este ZIP contem somente atualizacao de aplicativo. Nao e instalacao de fabrica.\n"
            "Nao inclui apagamento integral; atualiza os setores da aplicacao. Falhas ainda podem impedir boot.\n"
            "Confirme modelo, banda, antena, backup privado, particoes, slot ativo e rollback.\n"
            "Use tools/bridge/verify_artifact.py antes de abrir o flasher oficial.\n"
            "Leia docs/bridge/INSTALL.md. Nunca envie backups ao repositorio.\n"
            + (
                f"PACOTE DO PAIS: {country['title']}. Em radio que nunca teve regiao, a ponte define a regiao "
                f"{country['code']} na primeira partida. Radio que ja tem regiao nao e alterado.\n"
                if country
                else ""
            )
        ).encode(),
    }
    checksums = "".join(
        f"{sha256(data)}  {name}\n" for name, data in sorted(payloads.items())
    )
    payloads["SHA256SUMS"] = checksums.encode()
    archive = args.output / f"meshtastic-bitchat-{model}-{VERSION}-{source[:7]}.zip"
    with zipfile.ZipFile(
        archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as output:
        for name, data in sorted(payloads.items()):
            info = zipfile.ZipInfo(name, (2026, 11, 13, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            output.writestr(info, data)
    (args.output / filename).write_bytes(application)
    (args.output / f"{model}-manifest.json").write_bytes(
        payloads["bridge-manifest.json"]
    )
    print(
        json.dumps(
            {
                "archive": archive.name,
                "sha256": sha256(archive.read_bytes()),
                "application": filename,
                "application_sha256": sha256(application),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()

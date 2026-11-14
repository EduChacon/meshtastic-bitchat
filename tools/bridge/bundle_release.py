#!/usr/bin/env python3
"""Gather the per-board application updates into the single ZIP given to the official flasher."""

import argparse
import hashlib
import re
import zipfile
from pathlib import Path

from verify_artifact import MODELS

NOTICE = (
    "VERSAO BETA. Nenhum ensaio desta versao em hardware.\n"
    "Um unico ZIP para todos os aparelhos: no flasher oficial, escolha a placa, carregue este\n"
    "arquivo e use Atualizar / Update. Nao use Apagar Flash e Instalar.\n"
    "O flasher grava somente a aplicacao da placa escolhida. Escolher a placa errada pode\n"
    "impedir o boot. Nas placas com opcao de interface colorida (MUI), deixe-a desmarcada.\n"
    "Aparelhos: {models}.\n"
    "Tudo vem configurado por padrao; os ajustes do menu sao opcionais. Leia docs/bridge/DEFAULTS.md.\n"
)


def flasher_choice(names, model):
    """File names the official flasher would take from this ZIP for one board's update."""
    pattern = re.compile(f"firmware-{model}-.+-update.bin")
    return [n for n in names if pattern.search(n) and n.endswith("update.bin")]


def bundle(release, output, version, license_file):
    payloads = {}
    for model in MODELS:
        found = sorted(release.glob(f"firmware-{model}-bridge-*-update.bin"))
        if len(found) != 1:
            raise SystemExit(f"Expected exactly one application update for {model}")
        payloads[found[0].name] = found[0].read_bytes()
    for model in MODELS:
        if len(flasher_choice(payloads, model)) != 1:
            raise SystemExit(f"The flasher could not pick a single file for {model}")
    payloads["LEIA-ANTES.txt"] = NOTICE.format(models=", ".join(MODELS)).encode()
    payloads["LICENSE"] = license_file.read_bytes()
    payloads["SHA256SUMS"] = "".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n"
        for name, data in sorted(payloads.items())
    ).encode()
    archive = output / f"meshtastic-bitchat-{version}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as target:
        for name, data in sorted(payloads.items()):
            info = zipfile.ZipInfo(name, (2026, 11, 14, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            target.writestr(info, data)
    return archive


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("release", type=Path, help="Directory with the verified files")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--license", type=Path, default=Path("LICENSE"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    print(bundle(args.release, args.output, args.version, args.license).name)


if __name__ == "__main__":
    main()

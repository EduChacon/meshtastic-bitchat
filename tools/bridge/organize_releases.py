#!/usr/bin/env python3
"""One-time, scoped release maintenance. Does not modify firmware, tags or radios."""

import argparse
import gzip
import hashlib
import io
import json
import os
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAN = json.loads((ROOT / "tools/bridge/release_cleanup_plan.json").read_text())
REPO = PLAN["repository"]
CURRENT = PLAN["current"]
TAG = CURRENT["tag"]
BUNDLE = "technical-package-0.1.0-experimental.3.tar.gz"
KEEP = {
    "meshtastic-bitchat-heltec-v3-0.1.0-experimental.3-d15a403.zip",
    "meshtastic-bitchat-m5stack-cardputer-adv-0.1.0-experimental.3-d15a403.zip",
    "firmware-heltec-v3-bridge-0.1.0-experimental.3-d15a403-update.bin",
    "firmware-m5stack-cardputer-adv-bridge-0.1.0-experimental.3-d15a403-update.bin",
    "prepare_flash.py",
    "verify_artifact.py",
    "WINDOWS_RECOVERY.md",
    "SHA256SUMS",
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def gh(*arguments):
    return subprocess.check_output(["gh", *arguments], text=True)


def api(endpoint, method="GET"):
    args = ["api", f"repos/{REPO}/{endpoint}"]
    if method != "GET":
        args += ["--method", method]
    value = gh(*args)
    return json.loads(value) if value.strip() else None


def current_release():
    release = api(f"releases/{CURRENT['id']}")
    if (
        release["tag_name"] != TAG
        or release["target_commitish"] != CURRENT["commit"]
        or not release["prerelease"]
    ):
        raise ValueError("Current prerelease identity changed")
    if api(f"git/ref/tags/{TAG}")["object"]["sha"] != CURRENT["commit"]:
        raise ValueError("Current source tag changed")
    return release


def verify_originals(directory):
    for asset in CURRENT["assets"]:
        data = (directory / asset["name"]).read_bytes()
        if len(data) != asset["size"] or digest(data) != asset["sha256"]:
            raise ValueError(f"Original asset integrity mismatch: {asset['name']}")


def recover_bundle(bundle, directory):
    expected = {"original-assets/" + a["name"]: a for a in CURRENT["assets"]}
    with tarfile.open(bundle, "r:gz") as archive:
        members = archive.getmembers()
        if len(members) != len(expected) or {m.name for m in members} != set(expected):
            raise ValueError("Unexpected technical bundle members")
        for member in members:
            asset = expected[member.name]
            if not member.isfile() or member.size != asset["size"]:
                raise ValueError("Unsafe technical bundle member")
            data = archive.extractfile(member).read()
            if digest(data) != asset["sha256"]:
                raise ValueError("Technical bundle integrity mismatch")
            (directory / asset["name"]).write_bytes(data)


def make_bundle(originals, destination):
    with destination.open("wb") as output, gzip.GzipFile(
        filename="", mode="wb", fileobj=output, mtime=0
    ) as compressed:
        with tarfile.open(
            fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT
        ) as archive:
            for asset in sorted(CURRENT["assets"], key=lambda a: a["name"]):
                data = (originals / asset["name"]).read_bytes()
                info = tarfile.TarInfo("original-assets/" + asset["name"])
                info.size, info.mode, info.mtime = len(data), 0o644, 1794528000
                archive.addfile(info, io.BytesIO(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    if not args.apply:
        print(
            json.dumps(
                {
                    "remove_releases": PLAN["remove"],
                    "keep_current_assets": sorted(KEEP),
                    "bundle": BUNDLE,
                },
                indent=2,
            )
        )
        return
    if os.environ.get("GITHUB_REPOSITORY") != REPO:
        raise SystemExit(
            "Apply is restricted to this repository's authorized Actions execution"
        )
    work = Path("/tmp/bridge-release-organization")
    originals = work / "original-assets"
    originals.mkdir(parents=True, exist_ok=True)
    report = {
        "source_commit": CURRENT["commit"],
        "removed_releases": [],
        "removed_artifacts": [],
        "removed_assets": [],
        "completed": False,
    }

    def save():
        (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")

    save()
    release = current_release()
    allowed = {a["name"] for a in CURRENT["assets"]} | {BUNDLE}
    if any(a["name"] not in allowed for a in release["assets"]):
        raise ValueError("Unknown current release assets; stop for review")
    assets = {a["name"]: a for a in release["assets"]}
    if not KEEP - {"SHA256SUMS"} <= set(assets):
        raise ValueError("Required user downloads are missing")
    for original in CURRENT["assets"]:
        present = assets.get(original["name"])
        if (
            present
            and original["name"] != "SHA256SUMS"
            and present["digest"] != "sha256:" + original["sha256"]
        ):
            raise ValueError("Current asset bytes changed; no maintenance permitted")
    for old in PLAN["remove"]:
        if api(f"git/ref/tags/{old['tag']}")["object"]["sha"] != old["commit"]:
            raise ValueError("Historical source tag changed")
    if BUNDLE in assets:
        gh(
            "release",
            "download",
            TAG,
            "--repo",
            REPO,
            "--pattern",
            BUNDLE,
            "--dir",
            str(work),
            "--clobber",
        )
        recover_bundle(work / BUNDLE, originals)
    else:
        if set(assets) != {a["name"] for a in CURRENT["assets"]}:
            raise ValueError("Original release is incomplete; no deletion permitted")
        gh(
            "release",
            "download",
            TAG,
            "--repo",
            REPO,
            "--dir",
            str(originals),
            "--clobber",
        )
    verify_originals(originals)
    bundle = work / BUNDLE
    if BUNDLE not in assets:
        make_bundle(originals, bundle)
    bundle_hash = digest(bundle.read_bytes())
    if BUNDLE in assets:
        if assets[BUNDLE]["digest"] != "sha256:" + bundle_hash:
            raise ValueError(
                "Existing technical archive differs from the reproducible package"
            )
    else:
        gh("release", "upload", TAG, str(bundle), "--repo", REPO)
    downloaded = work / "verified-download"
    downloaded.mkdir(exist_ok=True)
    gh(
        "release",
        "download",
        TAG,
        "--repo",
        REPO,
        "--pattern",
        BUNDLE,
        "--dir",
        str(downloaded),
        "--clobber",
    )
    if digest((downloaded / BUNDLE).read_bytes()) != bundle_hash:
        raise ValueError("Published technical archive failed verification")
    verify_originals(originals)
    report["technical_package_sha256"] = bundle_hash
    report["original_assets_verified"] = len(CURRENT["assets"])
    save()

    releases = {r["id"]: r for r in api("releases?per_page=100")}
    for old in PLAN["remove"]:
        if old["id"] not in releases:
            continue
        actual = releases[old["id"]]
        if (
            actual["tag_name"] != old["tag"]
            or actual["target_commitish"] != old["commit"]
        ):
            raise ValueError("Superseded release identity changed")
        api(f"releases/{old['id']}", "DELETE")
        report["removed_releases"].append({"tag": old["tag"], "commit": old["commit"]})
        save()

    for asset in current_release()["assets"]:
        if asset["name"] not in KEEP | {BUNDLE}:
            expected = next(a for a in CURRENT["assets"] if a["name"] == asset["name"])
            if asset["digest"] != "sha256:" + expected["sha256"]:
                raise ValueError("Asset changed before consolidation")
            api(f"releases/assets/{asset['id']}", "DELETE")
            report["removed_assets"].append(asset["name"])
            save()
    checksums = {
        a["name"]: a["sha256"]
        for a in CURRENT["assets"]
        if a["name"] in KEEP - {"SHA256SUMS"}
    }
    checksums[BUNDLE] = bundle_hash
    index = work / "SHA256SUMS"
    index.write_text(
        "".join(f"{checksum}  {name}\n" for name, checksum in sorted(checksums.items()))
    )
    gh("release", "upload", TAG, str(index), "--repo", REPO, "--clobber")
    gh(
        "release",
        "edit",
        TAG,
        "--repo",
        REPO,
        "--title",
        "Meshtastic ↔ Bitchat .3 — experimental / teste",
        "--prerelease",
        "--latest=false",
        "--notes-file",
        str(ROOT / "docs/bridge/RELEASE.md"),
    )

    page = 1
    candidates = []
    while True:
        records = api(f"actions/artifacts?per_page=100&page={page}")["artifacts"]
        candidates.extend(
            a
            for a in records
            if a["name"]
            in {"release-heltec-v3-bitchat", "release-m5stack-cardputer-adv-bitchat"}
            and a.get("workflow_run", {}).get("id") != CURRENT["run_id"]
            and a["created_at"] < CURRENT["published_at"]
        )
        if len(records) < 100:
            break
        page += 1
    for artifact in candidates:
        api(f"actions/artifacts/{artifact['id']}", "DELETE")
        report["removed_artifacts"].append(
            {
                "id": artifact["id"],
                "name": artifact["name"],
                "run": artifact.get("workflow_run", {}).get("id"),
            }
        )
        save()

    final = current_release()
    if {a["name"] for a in final["assets"]} != KEEP | {BUNDLE}:
        raise ValueError("Final asset inventory mismatch")
    expected_hashes = dict(checksums, SHA256SUMS=digest(index.read_bytes()))
    if any(
        a["digest"] != "sha256:" + expected_hashes[a["name"]] for a in final["assets"]
    ):
        raise ValueError("Final release hash mismatch")
    remaining = {r["id"] for r in api("releases?per_page=100")}
    if any(old["id"] in remaining for old in PLAN["remove"]):
        raise ValueError("Superseded release still published")
    for old in PLAN["remove"]:
        if api(f"git/ref/tags/{old['tag']}")["object"]["sha"] != old["commit"]:
            raise ValueError("Historical source tag changed")
    report["retained_assets"] = sorted(KEEP | {BUNDLE})
    report["completed"] = True
    save()
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()

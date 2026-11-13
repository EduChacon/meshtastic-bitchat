# Meshtastic ↔ Bitchat

[Português](README.md) | [English](README.en.md)

An experimental embedded bridge derived from the official Meshtastic firmware. It is designed
for the unmodified Bitchat and Meshtastic apps, with no computer, MQTT, Nostr or internet in the
operating path. A computer is used to build, install and diagnose the firmware.

**Experimental menu revision: [v0.1.0-experimental.4](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.4).**
Fixes the disappearing channel confirmation and enables local BLE plus dedicated public
text on first use. See the [quick start and defaults](docs/bridge/DEFAULTS.md#english)
and [menu guide](docs/bridge/MENU_FIX.md).
An operator reported Heltec boot on `.3`; `.4` requires new physical acceptance. Cardputer,
phone delivery, stability, preservation and rollback remain unaccepted on hardware.

**The `main` branch still contains earlier firmware.** Use the release tag and assets;
firmware merging remains pending. Read the [Windows guide](docs/bridge/WINDOWS_RECOVERY.md)
and [investigation record](docs/bridge/BOOT_INCIDENT.md).

**Status: experimental.** Software tests and builds do not establish field operation.
Consult the revision's CI results and limitations before installing.

## Independent modes

| Mode                  | Implemented behavior                                                                                           | Modified radios                                                  |
| --------------------- | -------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| A — local BLE         | Bounded Bitchat packet relay with signature checks, TTL and deduplication                                      | One per BLE area; mode A uses no LoRa airtime                    |
| B — shared text       | `!lora text` to native text on a dedicated Meshtastic channel; channel text to public Bitchat                  | One; other radios remain stock                                   |
| C — laboratory tunnel | Preserved packets on private port 256; discovery and `!bctun text`; separate opt-in for opaque private traffic | Two endpoints; stock intermediate nodes require separate testing |

**Local BLE and text integration are enabled by default in this revision.** On first boot,
including an upgrade, the bridge uses an already authorized text channel or creates the
dedicated public `Bitchat` channel in an empty slot. It does not select or replace existing
channels based on name or position. The primary channel is excluded. The [guide](docs/bridge/DEFAULTS.md#english)
publishes the interoperability key: it is not secret. Later OFF or BLE-only choices persist.
Laboratory mode C still requires separate activation.

B does not translate DMs, Noise sessions, position, telemetry, files, voice or administrative
commands. Selecting another channel requires confirmation that its text becomes public
on local Bitchat; the PSK does not protect that exported copy.

## Release targets

- `heltec-v3-bitchat`: Heltec WiFi LoRa 32 V3, ESP32-S3/SX1262, 8 MB flash.
- `m5stack-cardputer-adv-bitchat`: Cardputer **ADV** with **Cap LoRa-1262**, matching the
  upstream target revision, 8 MB flash. The original Cardputer and other accessories are not interchangeable.

T1000-E, RAK4631, T-Deck, T-Echo, other Heltec/LilyGO boards and other Meshtastic devices are
intended **stock-node** candidates for mode B when radio, band, region, preset, frequency and
channel match. That does not make these bridge binaries installable on them. Bridge ports
for T1000-E/nRF52 and T-Deck are not included in this release.
[Compatibility matrix](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.4/docs/bridge/COMPATIBILITY.md).

The protocol core is shared, but firmware is built **for each board model**. The same CPU
does not imply the same pins, radio, flash, partitions or bootloader. There is no universal
ESP32 or nRF52 image; installing another board's image can prevent booting.

## Downloads

Available release: [**v0.1.0-experimental.4**](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.4), for recovery testing after private
backup and preflight. Choose the ZIP beginning with the correct board name and containing `.4`.

| Board                         | Package for the official flasher                                                                                 |
| ----------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| **Heltec V3**                 | [**Download Heltec V3 ZIP**](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.4) |
| Cardputer ADV + Cap LoRa-1262 | [Download Cardputer ADV ZIP](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.4) |

1. Follow the [Windows private-backup procedure](https://github.com/EduChacon/meshtastic-bitchat/blob/main/docs/bridge/WINDOWS_RECOVERY.md).
2. Check the download against [SHA256SUMS](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.4/SHA256SUMS).
3. In the [official Web Flasher](https://flasher.meshtastic.org/), select the board and ZIP,
   then **Update**. Do not select **Erase Flash and Install**.

Both backup/preflight scripts and the guide remain attached. Each board also has a matching
`-update.bin` alternative; **Source code** is not an installable firmware package.
A merge is not required to install the release assets.

The [technical package](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.4/technical-package-0.1.0-experimental.4.tar.gz) contains corresponding source, dependencies, licenses,
ELF/map symbols, manifests, test evidence and the release attachments with their hashes.
It is not a flasher input. Source history, tags and the
[investigation record](docs/bridge/BOOT_INCIDENT.md) are preserved.

## Use

After installation and first-boot checks, connect Meshtastic to set the clock. Without valid
time the radio automatically advertises Meshtastic; with valid time it returns to Bitchat.
Share only the `Bitchat` channel with the remote stock radio and allow 30 seconds.
See the [quick start](docs/bridge/DEFAULTS.md#english) for the public key and full-channel cases.

Send ordinary text **on that Meshtastic channel**; send `!lora text` in Bitchat `#mesh`
for the opposite direction. Keep Bitchat open nearby for the first test.
To change operation, open `System → Bitchat bridge`:

1. **Local Bitchat:** Bluetooth relaying, with bridge text translation and LoRa tunnel disabled.
2. **Bitchat + LoRa:** select a secondary channel and confirm public export to local Bitchat.
   Allow 30 seconds before the first message.

**More options** contains power profiles, app discovery, status and lab settings. Basic modes disable the
tunnel; enabling it later requires its own selection and consent.

See the [menu guide](docs/bridge/MENU_FIX.md); the [previous guide](docs/bridge/USAGE.en.md)
documents `.3`. Automatic activation also applies to a confirmed screenless Heltec V3;
Meshtastic pairing and valid time are still required. This is implemented and host-tested,
**not validated on a screenless board**. No custom USB configurator or simultaneous-discovery
guarantee is included.

## Build and documentation

Base: Meshtastic **2.7.26.54e0d8d**, commit
`54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`, the Beta listed as the stable download on the
[official downloads page](https://meshtastic.org/downloads/) checked on 2026-11-13.
This release does not use the Alpha 2.8.2 line.
Bitchat reference: iOS **1.8.0**, commit `30c58eee349e172b37f69faa2113c9d5f2c76c23`.

To build the correction, check out the tag in a clean working copy:

```sh
git fetch origin tag v0.1.0-experimental.4
git checkout --detach v0.1.0-experimental.4
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.2.0 intelhex==2.3.0
git submodule update --init protobufs
SOURCE_DATE_EPOCH=1794528000 .venv/bin/pio run -e heltec-v3-bitchat -t buildprog
```

These commands build software; they do not upload it. Use the pinned official sources and
tools. Initial preparation requires dependency downloads. Detailed engineering documents
are currently in Portuguese:

- [Architecture, limits and falsifiable hypotheses](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.4/docs/bridge/ARCHITECTURE.md)
- [Compatibility and feature coverage](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.4/docs/bridge/COMPATIBILITY.md)
- [Installation, verifier and recovery](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.4/docs/bridge/INSTALL.md)
- [Threats, privacy and limitations](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.4/docs/bridge/SECURITY.md)
- [Tests, bench procedure, power measurements and acceptance criteria](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.4/docs/bridge/TESTING.md)
- [Sources, provenance and licenses](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.4/docs/bridge/SOURCES.md)
- [Comparison with other bridges and evidence limits](docs/bridge/COMPARISON.md)
- [Implementation and validation record](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.4/docs/bridge/WORKLOG.md)
- [Original firmware README](docs/bridge/README.upstream.md)

Official upstream history and the original project history are preserved. This is not a
MeshCore project. Firmware license: [GPL-3.0](LICENSE); dependencies retain their own licenses.

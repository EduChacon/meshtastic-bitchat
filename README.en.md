# Meshtastic ↔ Bitchat

[Português](README.md) | [English](README.en.md)

An experimental embedded bridge derived from the official Meshtastic firmware. It is designed
for the unmodified Bitchat and Meshtastic apps, with no computer, MQTT, Nostr or internet in the
operating path. A computer is used to build, install and diagnose the firmware.

**Startup-fix prerelease available: [v0.1.0-experimental.3](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.3).**
The corrections passed software checks; real-device boot remains unverified.
Earlier prereleases were removed from downloads after the reported Heltec V3 boot failure.
Cardputer hardware is also untested.

**The `main` branch still contains the earlier firmware.** To test or build the corrections,
use the assets and source tagged `v0.1.0-experimental.3`; merging remains pending.
Follow the [Windows private-backup procedure](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/WINDOWS_RECOVERY.md)
and read the [investigation record](docs/bridge/BOOT_INCIDENT.md).

**Status: correction verified in software; `.3` physical acceptance is still pending.**
A successful build does not establish BLE discovery, delivery, power consumption, boot,
update preservation or recovery. This is not a field-validated product.
See the executed checks and remaining gaps in [TESTING](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/TESTING.md).

## Independent modes

| Mode                  | Implemented behavior                                                                                           | Modified radios                                                  |
| --------------------- | -------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| A — local BLE         | Bounded Bitchat packet relay with signature checks, TTL and deduplication                                      | One per BLE area; mode A uses no LoRa airtime                    |
| B — shared text       | `!lora text` to native text on a dedicated Meshtastic channel; channel text to public Bitchat                  | One; other radios remain stock                                   |
| C — laboratory tunnel | Preserved packets on private port 256; discovery and `!bctun text`; separate opt-in for opaque private traffic | Two endpoints; stock intermediate nodes require separate testing |

**Every bridge mode is disabled by default.** B does not translate DMs, Noise sessions,
position, telemetry, files, voice or administrative commands. Exporting a PSK-protected
channel makes its text public on the local Bitchat mesh after explicit consent. The PSK does
not protect that text on the Bitchat side.

## Release targets

- `heltec-v3-bitchat`: Heltec WiFi LoRa 32 V3, ESP32-S3/SX1262, 8 MB flash.
- `m5stack-cardputer-adv-bitchat`: Cardputer **ADV** with **Cap LoRa-1262**, matching the
  upstream target revision, 8 MB flash. The original Cardputer and other accessories are not interchangeable.

T1000-E, RAK4631, T-Deck, T-Echo, other Heltec/LilyGO boards and other Meshtastic devices are
intended **stock-node** candidates for mode B when radio, band, region, preset, frequency and
channel match. That does not make these bridge binaries installable on them. Bridge ports
for T1000-E/nRF52 and T-Deck are not included in this release.
[Compatibility matrix](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/COMPATIBILITY.md).

The protocol core is shared, but firmware is built **for each board model**. The same CPU
does not imply the same pins, radio, flash, partitions or bootloader. There is no universal
ESP32 or nRF52 image; installing another board's image can prevent booting.

## Downloads

Available release: [**v0.1.0-experimental.3**](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.3), for recovery testing after private
backup and preflight. Organizing the downloads did not change the firmware bytes.

| Board                         | Package for the official flasher                                                                                                                                                                |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Heltec V3**                 | [**Download Heltec V3 ZIP**](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/meshtastic-bitchat-heltec-v3-0.1.0-experimental.3-d15a403.zip)             |
| Cardputer ADV + Cap LoRa-1262 | [Download Cardputer ADV ZIP](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/meshtastic-bitchat-m5stack-cardputer-adv-0.1.0-experimental.3-d15a403.zip) |

1. Follow the [Windows private-backup procedure](https://github.com/EduChacon/meshtastic-bitchat/blob/main/docs/bridge/WINDOWS_RECOVERY.md).
2. Check the download against [SHA256SUMS](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/SHA256SUMS).
3. In the [official Web Flasher](https://flasher.meshtastic.org/), select the board and ZIP,
   then **Update**. Do not select **Erase Flash and Install**.

Both backup/preflight scripts and the guide remain attached. Each board also has a matching
`-update.bin` alternative; **Source code** is not an installable firmware package.
A merge is not required to install the release assets.

The [technical package](https://github.com/EduChacon/meshtastic-bitchat/releases/download/v0.1.0-experimental.3/technical-package-0.1.0-experimental.3.tar.gz) contains corresponding source, dependencies, licenses,
ELF/map symbols, manifests, test evidence and the original `.3` attachments with their hashes.
It is not a flasher input. Source history, tags and the
[investigation record](docs/bridge/BOOT_INCIDENT.md) are preserved.

## Safe first setup

1. Identify the physical model/revision, band and antenna; record app and OS versions.
2. Establish bidirectional stock-radio communication. Keep a private backup and stock recovery files.
3. Read [installation and rollback](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/INSTALL.md). Packages contain only the
   application update, with no eraser, bootloader or partition table.
4. After installation and first-boot verification, use `Menu → System → Bitchat bridge`.
   First configure a secondary test channel with its own 32-byte PSK in the Meshtastic app.
5. Enable A first. For B, explicitly select the channel and read the public-export notice.
   Allow the 30-second observation period for other bridges. Then try `!lora Hello`.
6. `Meshtastic BLE: 5 min` temporarily advertises the native service for administration and
   clock synchronization, then returns to Bitchat advertising. Simultaneous operation needs
   testing on the exact iOS/app versions.

The display shows BLE connections, peers (P), drops (D), TX/RX counters and the LoRa queue (Q).
Large TX/RX counters use rounded-down k/M/G units. They do not acknowledge delivery to a
phone. Line spacing follows the board font; physical navigation and readability remain untested.

After a power loss, a Heltec without a trustworthy clock may need to reconnect to the
Meshtastic app to obtain the time. The display shows `Open Meshtastic: clock`. Opening only
Bitchat does not resolve this condition. There is no internet fallback.

## Build and documentation

Base: Meshtastic **2.7.26.54e0d8d**, commit
`54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`, the Beta listed as the stable download on the
[official downloads page](https://meshtastic.org/downloads/) checked on 2026-11-13.
This release does not use the Alpha 2.8.2 line.
Bitchat reference: iOS **1.8.0**, commit `30c58eee349e172b37f69faa2113c9d5f2c76c23`.

To build the correction, check out the tag in a clean working copy:

```sh
git fetch origin tag v0.1.0-experimental.3
git checkout --detach v0.1.0-experimental.3
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.2.0 intelhex==2.3.0
git submodule update --init protobufs
SOURCE_DATE_EPOCH=1794528000 .venv/bin/pio run -e heltec-v3-bitchat -t buildprog
```

These commands build software; they do not upload it. Use the pinned official sources and
tools. Initial preparation requires dependency downloads. Detailed engineering documents
are currently in Portuguese:

- [Architecture, limits and falsifiable hypotheses](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/ARCHITECTURE.md)
- [Compatibility and feature coverage](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/COMPATIBILITY.md)
- [Installation, verifier and recovery](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/INSTALL.md)
- [Threats, privacy and limitations](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/SECURITY.md)
- [Tests, bench procedure, power measurements and acceptance criteria](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/TESTING.md)
- [Sources, provenance and licenses](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/SOURCES.md)
- [Implementation and validation record](https://github.com/EduChacon/meshtastic-bitchat/blob/v0.1.0-experimental.3/docs/bridge/WORKLOG.md)
- [Original firmware README](docs/bridge/README.upstream.md)

Official upstream history and the original project history are preserved. This is not a
MeshCore project. Firmware license: [GPL-3.0](LICENSE); dependencies retain their own licenses.

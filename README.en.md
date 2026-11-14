# Meshtastic ↔ Bitchat

[Português](README.md) | [English](README.en.md)

An experimental embedded bridge derived from the official Meshtastic firmware. It is designed
for the unmodified Bitchat and Meshtastic apps, with no computer, MQTT, Nostr or internet in the
operating path. A computer is used to build, install and diagnose the firmware.

**Current prerelease: [v0.1.0-experimental.6](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.6).** Drops the mandatory `!lora`, fixes
message losses in group chat, recovers the clock without the Meshtastic app, simplifies the
menu and adds eight ESP32-S3 devices. [What changed and why](docs/bridge/CANDIDATE.md#english).

**Status: experimental, no hardware acceptance.** This version was checked by host simulation
and compilation only. None of the ten targets has been run with this firmware; eight of them
never ran any version of the bridge. Back up before installing and read the
[limits](docs/bridge/CANDIDATE.md#english) and the [bench procedure](docs/bridge/TESTING.md).

## How it works

| Mode                  | Behavior                                                                                       | Modified radios                              |
| --------------------- | ---------------------------------------------------------------------------------------------- | -------------------------------------------- |
| A — Bitchat only      | Bounded Bitchat packet relay over Bluetooth with signature checks, TTL and deduplication       | One per Bluetooth area; uses no LoRa airtime |
| B — Bitchat + LoRa    | Public `#mesh` text ↔ native text on a dedicated Meshtastic channel                            | One; other radios keep stock firmware        |
| C — laboratory tunnel | Bitchat packets preserved on private port 256 between two bridges; separate opt-in and consent | Two                                          |

On first boot the bridge enables A and B and creates the public `Bitchat` channel in an empty
slot, without touching the primary or any existing channel. That channel's key is public and
listed in the [guide](docs/bridge/DEFAULTS.md#english): **its text appears in Bitchat `#mesh`,
and every public `#mesh` message within reach of the bridge is published on that LoRa channel.**

B does not translate DMs, Noise sessions, private groups, position, telemetry, files, voice or
administrative commands.

## Devices

| Device                           | File name starts with                         | Flash |
| -------------------------------- | --------------------------------------------- | ----- |
| Heltec V3                        | `meshtastic-bitchat-heltec-v3-`               | 8 MB  |
| Heltec V4 (OLED)                 | `meshtastic-bitchat-heltec-v4-`               | 16 MB |
| Heltec Wireless Stick Lite V3    | `meshtastic-bitchat-heltec-wsl-v3-`           | 8 MB  |
| Heltec Wireless Tracker V1.1     | `meshtastic-bitchat-heltec-wireless-tracker-` | 8 MB  |
| Heltec Wireless Paper            | `meshtastic-bitchat-heltec-wireless-paper-`   | 8 MB  |
| LILYGO T-Beam Supreme            | `meshtastic-bitchat-tbeam-s3-core-`           | 8 MB  |
| Seeed XIAO ESP32-S3 + Wio-SX1262 | `meshtastic-bitchat-seeed-xiao-s3-`           | 8 MB  |
| LILYGO T-Deck                    | `meshtastic-bitchat-t-deck-`                  | 16 MB |
| Station G2                       | `meshtastic-bitchat-station-g2-`              | 16 MB |
| Cardputer ADV + Cap LoRa-1262    | `meshtastic-bitchat-m5stack-cardputer-adv-`   | 8 MB  |

Firmware is built **for each board model**. The same CPU does not imply the same pins, radio,
flash, partitions or bootloader; another board's image can prevent booting. T1000-E, RAK4631,
T-Echo, classic T-Beam, T-LoRa32 and T3-S3 **have no bridge binary** but work as stock nodes in
mode B. [Matrix and conditions](docs/bridge/COMPATIBILITY.md).

## Install

1. From [v0.1.0-experimental.6](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v0.1.0-experimental.6), download the ZIP for the exact model,
   `prepare_flash.py`, `verify_artifact.py` and `SHA256SUMS`.
2. Follow the [Windows private-backup procedure](docs/bridge/WINDOWS_RECOVERY.md) and check the hash.
3. In the [official Web Flasher](https://flasher.meshtastic.org/), select the board and the ZIP
   and use **Update**. Do not select **Erase Flash and Install**.

Each board also has a matching `-update.bin`; **Source code** is not firmware. To return to
stock, use the official Meshtastic 2.7.26 image for the same model.

## Use

1. Open **Bitchat** near the radio. If the radio has no clock (for instance after a power
   cut), the first Bitchat phone that connects supplies it; the Meshtastic app and GPS also
   do, and take precedence.
2. On the remote stock radio, add only the `Bitchat` channel with the public key from the
   [guide](docs/bridge/DEFAULTS.md#english), keeping region, preset and frequency compatible.
3. Chat normally: `#mesh` text reaches the channel as `nick: text`; channel text reaches
   `#mesh` as `[MT node name] text`.

On a radio with a screen, `System → Bitchat bridge` offers `Bitchat only`, `Bitchat + LoRa`,
`Turn bridge off`, `Status` and `Settings`. Under `Settings`, `LoRa: all messages` can be
switched to `LoRa: only !lora`, so that only text starting with `!lora ` leaves over LoRa. The
status screen says in plain words what is happening and what to do. Without a screen the
defaults apply.

## Build and documentation

Base: Meshtastic **2.7.26.54e0d8d**, commit `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`.
Bitchat reference: iOS **1.8.0**, commit `30c58eee349e172b37f69faa2113c9d5f2c76c23`.

```sh
git fetch origin tag v0.1.0-experimental.6
git checkout --detach v0.1.0-experimental.6
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.2.0 intelhex==2.3.0
git submodule update --init protobufs
SOURCE_DATE_EPOCH=1794528000 .venv/bin/pio run -e heltec-v3-bitchat -t buildprog
```

These commands build software; they do not upload it. Detailed engineering documents are
in Portuguese:

- [Changes, limits and evidence for this version](docs/bridge/CANDIDATE.md#english)
- [Default setup and the public channel key](docs/bridge/DEFAULTS.md#english)
- [Compatibility and feature coverage](docs/bridge/COMPATIBILITY.md)
- [Architecture and decisions](docs/bridge/ARCHITECTURE.md)
- [Installation, verifier and recovery](docs/bridge/INSTALL.md)
- [Threats, privacy and limitations](docs/bridge/SECURITY.md)
- [Tests, bench procedure and acceptance criteria](docs/bridge/TESTING.md)
- [Sources, provenance and licenses](docs/bridge/SOURCES.md)
- [Comparison with other bridges](docs/bridge/COMPARISON.md)
- [Work record](docs/bridge/WORKLOG.md) and [`.2` boot investigation](docs/bridge/BOOT_INCIDENT.md)
- [Original firmware README](docs/bridge/README.upstream.md)

This is not a MeshCore project. Firmware license: [GPL-3.0](LICENSE); dependencies keep their
own licenses.

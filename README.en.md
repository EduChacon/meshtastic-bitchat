# Meshtastic ↔ Bitchat

[Português](README.md) | [English](README.en.md)

An embedded bridge derived from the official Meshtastic firmware. It is designed
for the unmodified Bitchat and Meshtastic apps, with no computer, MQTT, Nostr or internet in the
operating path. A computer is used to build, install and diagnose the firmware.

**Current version (beta): [v1.0.0-beta.16](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v1.0.0-beta.16).** Drops the mandatory `!lora`, fixes
message losses in group chat, recovers the clock without the Meshtastic app, simplifies the
menu and adds eight ESP32-S3 devices. [What changed and why](docs/bridge/CHANGES.md#english).

**Status: beta, with one device reported working.** On a Heltec V3 with Bitchat iOS 1.8.0,
versions up to `1.0.0-beta.3` did not exchange messages with the app: the phone connected and
subscribed but sent nothing to the radio. With `1.0.0-beta.4` the operator reported the phone
recognised (`Phones 1/1`, `People 2`) and text in both directions (`LoRa out 2 in 1`). The
`!bridge` report of that session showed the new path in use (`path 9`, 230 frames received and
59 sent through the phone's own service, no write from the phone to the bridge service), with
the Meshtastic app on the same link. It is one session on one device, with no endurance test. The other nine targets were only compiled.
`1.0.0-beta.5` added the commands for radios without a screen and changed the Bluetooth
advertisement; `1.0.0-beta.6` added the link with MeshCore bridges through Bitchat;
`1.0.0-beta.7` added the text command for `Battery saver` (`!bridge economy on` and `off`);
`1.0.0-beta.8` added the guide inside the bridge (`!bridge guide`, `!ponte guia`);
`1.0.0-beta.9` added one package per country; `1.0.0-beta.10` keeps the text from LoRa for
phones that arrive later; `1.0.0-beta.11` added `!bridge who`, the names heard on LoRa;
`1.0.0-beta.12` also keeps what phones write near the radio; `1.0.0-beta.13` adds a house
note, battery, radios in range, help requests, range tests and `!bridge prefix`; `1.0.0-beta.14` fixes
security and privacy flaws found in a review; `1.0.0-beta.15` fixes the T1000-E Bluetooth
advertisement, which kept Bitchat from finding the bridge; `1.0.0-beta.16` adds experimental
[Cashu payments over LoRa](docs/bridge/CASHU.md) (guide in Portuguese). These changes
were simulated and compiled, **not tested on a device**. Back up before
installing and read the
[limits](docs/bridge/CHANGES.md#english) and the [bench procedure](docs/bridge/TESTING.md).

## How it works

| Mode                  | Behavior                                                                                       | Modified radios                              |
| --------------------- | ---------------------------------------------------------------------------------------------- | -------------------------------------------- |
| A — Bitchat only      | Bounded Bitchat packet relay over Bluetooth with signature checks, TTL and deduplication       | One per Bluetooth area; uses no LoRa airtime |
| B — Bitchat + LoRa    | Public `#mesh` text ↔ native text on a dedicated Meshtastic channel                           | One; other radios keep stock firmware        |
| C — laboratory tunnel | Bitchat packets preserved on private port 256 between two bridges; separate opt-in and consent | Two                                          |

On first boot the bridge enables A and B and creates the public `Bitchat` channel in an empty
slot, without touching the primary or any existing channel. That channel's key is public and
listed in the [guide](docs/bridge/DEFAULTS.md#english): **its text appears in Bitchat `#mesh`,
and every public `#mesh` message within reach of the bridge is published on that LoRa channel.**

B does not translate DMs, Noise sessions, private groups, position, telemetry, files, voice or
administrative commands.

## Devices

| Device                           | Target in the flasher     | Flash |
| -------------------------------- | ------------------------- | ----- |
| Heltec V3                        | `heltec-v3`               | 8 MB  |
| Heltec V4 (OLED)                 | `heltec-v4`               | 16 MB |
| Heltec Wireless Stick Lite V3    | `heltec-wsl-v3`           | 8 MB  |
| Heltec Wireless Tracker V1.1     | `heltec-wireless-tracker` | 8 MB  |
| Heltec Wireless Paper            | `heltec-wireless-paper`   | 8 MB  |
| LILYGO T-Beam Supreme            | `tbeam-s3-core`           | 8 MB  |
| Seeed XIAO ESP32-S3 + Wio-SX1262 | `seeed-xiao-s3`           | 8 MB  |
| LILYGO T-Deck                    | `t-deck`                  | 16 MB |
| Station G2                       | `station-g2`              | 16 MB |
| Cardputer ADV + Cap LoRa-1262    | `m5stack-cardputer-adv`   | 8 MB  |

Firmware is built **for each board model**. The same CPU does not imply the same pins, radio,
flash, partitions or bootloader; another board's image can prevent booting. RAK4631, T-Echo,
classic T-Beam, T-LoRa32 and T3-S3 **have no bridge binary** but work as stock nodes in mode B.
[Matrix and conditions](docs/bridge/COMPATIBILITY.md).

The **SenseCAP T1000-E** has a separate **experimental** file attached to the release: it
compiles, but it has never run on a device and is outside the release checks.
[Status, installation and procedure](docs/bridge/T1000E.md) (in Portuguese).

## Install

1. From [v1.0.0-beta.16](https://github.com/EduChacon/meshtastic-bitchat/releases/tag/v1.0.0-beta.16), download
   **`meshtastic-bitchat-1.0.0-beta.16.zip`**. It is a single file for all ten devices. For a radio
   that never had a region set there is also one file per country (`…-Brasil.zip`, `…-EUA.zip`,
   `…-Europa-e-Reino-Unido.zip`), which makes the Meshtastic app unnecessary.
2. In the [official Web Flasher](https://flasher.meshtastic.org/), select the board, load the
   ZIP and use **Update**. Do not select **Erase Flash and Install**. If the board offers the
   colour interface (MUI), leave it unticked.

The flasher writes only the selected board's application; choosing the wrong board can prevent
booting. Nothing else is required: the bridge comes configured and every adjustment is
optional. A [backup procedure](docs/bridge/WINDOWS_RECOVERY.md) is available for those who
want a copy of the radio first. To return to stock, use the official Meshtastic 2.7.26 image
for the same model.

## Use

After flashing, the bridge is already on and the public `Bitchat` channel exists:

1. Open **Bitchat** near the radio and chat in `#mesh`. A radio without a clock takes the time
   from the first Bitchat phone that connects.
2. On the remote stock radio, add the `Bitchat` channel: open [this link](https://meshtastic.org/e/?add=true#CisSIMXrNZ7S8cg9qWpGbzZahdf-jswEx8THkKp3ToygVmRbGgdCaXRjaGF0) on the phone
   with the Meshtastic app and confirm that the channel is **added**, or type the name and key
   from the [guide](docs/bridge/DEFAULTS.md#english). This is the only step outside the
   bridge, because that radio does not run the bridge firmware.
3. `#mesh` text reaches the channel as `nick: text`; channel text reaches `#mesh` as
   `[MT node name] text`.

**The default channel is common to every bridge.** Its key is public: every bridge within LoRa
range shares the same room. For a closed group, create a secondary channel with its own key in
the Meshtastic app and select it under `System → Bitchat bridge → Bitchat + LoRa`.

**Testing the bridge.** In the Meshtastic app connected to the bridge radio, send `!bridge`
(or `!ponte`) on the `Bitchat` channel: the bridge answers there, to that app only, with its
state, the measurements of each Bluetooth link, how many Bitchat phones are actually talking
to it and what to do. In Bitchat, send
`!bridge` in `#mesh`: the `[bridge] …` answer proves Bluetooth works both ways.
`!bridge reset`, from the Meshtastic app only, drops the Bluetooth links and tells phones to
read the radio's services again. The command text itself travels over LoRa like any channel
message, and other bridges ignore it.

Optional adjustments on a radio with a screen: `Bitchat only`, `Bitchat + LoRa`,
`Turn bridge off`, `Status` and `Settings` (`LoRa: all messages` or `LoRa: only !lora`,
`Battery saver`, `Find Meshtastic app`, `Reconnect phones`). The status screen says in plain words what is
happening and what to do.

**Radio without a screen.** The same modes, as text, from the Meshtastic app connected to the
radio: `!bridge off`, `!bridge on`, `!bridge local` (Bluetooth only) and `!bridge lora`
(Bitchat + LoRa on the channel where the command is sent). `!bridge economy on` and
`!bridge economy off` turn `Battery saver` on and off. `!bridge help` lists the commands.
`!bridge guide` (or `!ponte guia`, in Portuguese) explains the bridge in four short messages and
also works in Bitchat `#mesh`. `!bridge who` (or `!ponte quem`) lists the names heard on LoRa in the last
six hours, in both places too.
Coming from another radio or from Bitchat, these commands change nothing. Daily use does not
need the Meshtastic app; the exception is the LoRa region, which a radio that was never
configured must receive once.

## Build and documentation

Base: Meshtastic **2.7.26.54e0d8d**, commit `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`.
Bitchat reference: iOS **1.8.0**, commit `30c58eee349e172b37f69faa2113c9d5f2c76c23`.

```sh
git fetch origin tag v1.0.0-beta.16
git checkout --detach v1.0.0-beta.16
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.2.0 intelhex==2.3.0
git submodule update --init protobufs
SOURCE_DATE_EPOCH=1794528000 .venv/bin/pio run -e heltec-v3-bitchat -t buildprog
```

These commands build software; they do not upload it. Detailed engineering documents are
in Portuguese:

- [Documentation index](docs/bridge/README.md)
- [Changes, limits and evidence for this version](docs/bridge/CHANGES.md#english)
- [Default setup and the public channel key](docs/bridge/DEFAULTS.md#english)
- [Compatibility and feature coverage](docs/bridge/COMPATIBILITY.md)
- [Architecture and decisions](docs/bridge/ARCHITECTURE.md)
- [Installation, verifier and recovery](docs/bridge/INSTALL.md)
- [Threats, privacy and limitations](docs/bridge/SECURITY.md)
- [Tests, bench procedure and acceptance criteria](docs/bridge/TESTING.md)
- [Sources, provenance and licenses](docs/bridge/SOURCES.md)
- [Comparison with other bridges](docs/bridge/COMPARISON.md)
- [Work record](docs/bridge/WORKLOG.md) and [`.2` boot investigation](docs/bridge/BOOT_INCIDENT.md)
- [Original firmware README](docs/bridge/history/README.upstream.md)

An independent project, not affiliated with or endorsed by Meshtastic LLC or the Bitchat
project. Meshtastic® is a registered trademark of Meshtastic LLC. This is not a MeshCore
project. Firmware license: [GPL-3.0](LICENSE); dependencies keep their own licenses.

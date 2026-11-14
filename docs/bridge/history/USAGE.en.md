# Using experimental release .3

[Português](USAGE.md) | [English](USAGE.en.md)

This guide applies to `v0.1.0-experimental.3`, commit `d15a403`. Operator-reported Heltec boot
does not establish message delivery. Compatibility and bench-test requirements still apply.

## Start with local Bluetooth

Connect the Meshtastic app first to set the radio clock, with Bluetooth enabled and stock
power saving that disables BLE turned off. Open `System → Bitchat bridge → Start BLE bridge`,
then open Bitchat nearby with Bluetooth permission. Manual pairing in iOS Settings is not
required. If the bridge shows `Open Meshtastic: clock`, select `Meshtastic BLE: 5 min`, connect
Meshtastic to set the clock, then wait for Bitchat advertising to resume.

On a fresh setup only BLE relaying is enabled. Starting the bridge also resumes previously
configured B/C channels; disable those in their selectors for a BLE-only test. Local relaying
does not require a LoRa channel.

| Current menu option                        | Meaning                                                                                                                 |
| ------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------- |
| `Back`                                     | Return to System.                                                                                                       |
| `Start BLE bridge` / `Stop bridge`         | Enable or disable the bridge, preserving configured channels.                                                           |
| `B: public text channel`                   | Select the Meshtastic channel shared with public Bitchat.                                                               |
| `C: tunnel channel (lab)`                  | Experimental tunnel between two modified radios, separate from stock text translation.                                  |
| `Profile: economy`                         | **Switch to economy**; this is the action, not the current profile.                                                     |
| `Profile: fixed`                           | **Switch to fixed**, the reference profile for a powered bench test.                                                    |
| `Meshtastic BLE: 5 min`                    | Advertise the native Meshtastic service for five minutes, then return to Bitchat advertising.                           |
| `C: private lab opt-in` / `C: private OFF` | Allow or stop opaque private tunnel traffic. Does not translate or decrypt DMs. Leave disabled for the first text test. |
| `Status`                                   | Show state or a blocking condition; availability is not a delivery receipt.                                             |

Economy reduces advertising frequency and the LoRa budget. Its actual energy, latency and
delivery tradeoffs require measurement.

## Meshtastic to Bitchat text

Use a consented test network: **channel text becomes public on local Bitchat even when the
Meshtastic channel uses a PSK**. Configure the same named secondary channel and private
32-byte PSK on both radios, with MQTT uplink/downlink disabled and matching RF settings.
Select that channel in `B: public text channel` on the modified radio. Primary channels and
a channel already used for C are not valid choices.

The public-export warning must be explicitly confirmed. If it disappears before confirmation,
do not assume configuration succeeded. Enable the bridge, allow its 30-second observation
period, and keep Bitchat open nearby. In the remote stock radio's Meshtastic app, select
**the same channel** and send ordinary short text such as `Test 1`. It is not a DM; no prefix
is needed in this direction. In the opposite direction, send `!lora Test 2` in local public
Bitchat. Check actual receipt in the remote Meshtastic app.

`BLE` counts clients subscribed to Bitchat notifications; `P` counts peers; `D` counts drops;
`TX/RX` count LoRa integration packets; `Q` is the LoRa queue. `BLE0` means no subscribed
Bitchat client. Counters and LoRa ACKs do not prove phone delivery.

`OFF - stock BLE` requires activation. `Open Meshtastic: clock` requires time from Meshtastic.
`Meshtastic window 5m` requires waiting for Bitchat advertising to resume. `B listening: wait 30s`
is the initial observation period. A bridge conflict or changed-channel block requires reviewing
the configuration; do not bypass it. `BC fixed (available)` or `BC economy (available)` still
requires checking connections and receipt in the apps.

## Heltec V3 without a screen

If the board is confirmed as a Heltec WiFi LoRa 32 V3, ESP32-S3/SX1262, 8 MB and appropriate
band, removing or omitting its display does not require a different radio target. Follow
the [backup and installation guide](../WINDOWS_RECOVERY.md) with the Heltec V3 package. Do not
use it on Heltec V2/V4, Wireless Tracker or Wireless Stick.

**Release .3 limitation:** the bridge defaults to OFF and depends on the local menu for setup.
There is no USB bridge configurator or bridge setting in the official Meshtastic app yet.
Flashing a screenless board does not solve activation. Headless deployment still needs a
verified local setup path; a computer must not be required during operation. Do not edit NVS
or erase flash as a workaround.

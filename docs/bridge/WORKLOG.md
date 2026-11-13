# Implementation record

## Authorization and boundaries

The owner authorized implementation, necessary isolated build dependencies and publication to
`EduChacon/meshtastic-bitchat` on 2026-11-13. Keep the existing repository private. No radio
has been opened, configured, reset or flashed. Flashing still requires the separately specified
device/revision/band/artifact/backup/consequences/rollback confirmation. Do not run the upstream
hardware harness: its default bake step can flash attached radios.

## References

- Meshtastic 2.7.26.54e0d8d Beta: `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`.
- Protobufs: `6b1ded439633cd03d4af85b44231b91d1d106278`.
- Bitchat iOS 1.8.0: `30c58eee349e172b37f69faa2113c9d5f2c76c23`.
- Original project commit: `9040e45db23acfd906753d7c3c28dd121d280c8b`.
- History-preserving integration: `78464ecac093adba7e927ce15e8b7f4f37074d0a`.
- Import CI: https://github.com/EduChacon/meshtastic-bitchat/actions/runs/43434050474

## Hardware evidence supplied by the owner

An app screenshot identifies Heltec V3, PlatformIO `heltec-v3`, ESP32-S3, firmware 2.7.19,
and intended region ANZ. Physical subrevision, antenna, effective frequency and preset remain
unconfirmed. SenseCAP T1000-E and Cardputer ADV + Cap LoRa-1262 remain provisional.
The owner accepts the latest stable/beta when useful; Alpha 2.8.2 is not the implementation base.

## Current work

- Read root AGENTS.md and the complete canonical `.github/copilot-instructions.md`.
- Obtained official firmware history and separate Bitchat source reference.
- Imported both histories on a remote integration branch; main is still the original commit.
- Archived imported upstream workflows to avoid unrelated schedules and release automation.
- Created an isolated Python environment with PlatformIO 6.2.0 and intelhex 2.3.0.
- Building unmodified Heltec firmware in a separate stock worktree.
- Next: bounded portable codecs/queues/policies; official-client vectors; BLE and Meshtastic
  adapters; on-device controls; builds, defensive tests, packaging and reviewed publication.

## Evidence levels

Do not describe this firmware as hardware validated. No iPhone, Android, BLE, RF, power,
installation, configuration preservation or rollback test has been executed. Host unit tests,
simulation, compilation and physical measurements must have separate result records.

## Known constraints to preserve

Signed Bitchat canonical data is padded even when the transmitted public frame is unpadded.
Only TTL and the context-bound RSR bit are excluded from the signature; routes are signed.
REQUEST_SYNC is link-local. The bridge must not invent RSR context. A raw opaque Noise packet
cannot be classified as text versus private media. LongFast is unsuitable for the current live
voice bitrate. One configured text exporter per overlapping destination domain is required.
Cold boot needs trustworthy local time before the bridge originates timestamped messages.

## Latest scope and software evidence

The owner asked to maximize device coverage, and specifically requested that the T1000-E
itself can be a Bitchat bridge and asked about T-Deck. Implement the nRF52 adapter/storage
and headless physical controls; do not treat stock-node compatibility as the requested port.
Add T-Deck (classic target; not Pro) and mainstream ESP32 variants. No hardware authorization
has been supplied; every board remains physically untested.

Portable host tests currently pass 497 checks, 19 official Bitchat vectors and 100000 malformed
inputs with ASan/UBSan. A first Heltec bridge binary compiled (RAM159416, flash2047721);
subsequent owner-proof change needed an explicit algorithm include, now fixed. Final build
and regression checks must be rerun. Stock Heltec build passed (RAM132096, flash2109737).
Trunk1.25.0 official archive SHA256 2cff4f1cc63916db290359a058b88167beeac07a496682d9307a1804b498f225;
clang-format16.0.3 obtained through its pinned plugin. Trunk fmt --ci checks only; applying
its installed formatter directly then checking is needed. Use TRUNK_TELEMETRY=off.

ESP bridge, stock source imports and source reference clones remain only under /private/tmp.
Remote main is still unchanged. No release yet. No runtime hardware test.

The owner then explicitly froze further additions to prioritize completing the project to
the stated quality requirements. Freeze release targets at Heltec V3 and Cardputer ADV;
T1000-E bridge, T-Deck and additional firmware ports are deferred. Stock Meshtastic
interoperability remains a core goal for all compatible radio families. Do not present
unimplemented ports as supported. nRF52 dependency preparation does not count as a port.

The owner requires avoiding bricking risk. Explain that flashing cannot have a zero-risk
guarantee; no hardware has been touched. Release only application updates for explicitly
identified ESP32-S3 targets, not factory/bootloader/partition erase packages. Require a
matching private backup, partition preflight and separate device-specific authorization
before any future write. No eFuse, bootloader, visibility or hardware changes.

## Final local review before integration CI

Both release environments compiled after the per-link BLE queue and owner-conflict fixes.
Heltec: RAM 163424 / 327680, flash 2060885 / 3342336 bytes. ADV: RAM 167808 / 327680,
flash 2176605 / 3342336 bytes. These are static linker figures, not free heap under load.
Host tests passed 523 assertions, 19 official-client vectors and 100000 malformed inputs
under ASan/UBSan. Seven Python artifact tests passed. Cppcheck traversed all seven bridge
translation units without reporting defects. Trunk formatting and workflow actionlint passed.

The strongest unresolved evidence is the absence of physical testing: no boot, iOS discovery,
Android, LoRa interoperability, update preservation, rollback, energy or prolonged-run result
exists. Clockless automatic recovery and transparent historical synchronization are not
fulfilled by this version. C private sessions remain experimental, with likely LoRa timeout
constraints. Do not promote this prerelease to a hardware-validated release on CI success.

The CI re-executes compilation, host tests, static analysis, clean rebuild comparison and a
feature-disabled compile before producing updates. Its artifacts record the exact final
source commit and results. The next useful physical step remains the stock baseline and
device identification, followed by the separately authorized reversible update experiment.

The owner asked about operation from the Heltec display. The existing System menu exposes
bridge controls through the stock physical-button navigation. A source-metric review found
wide counters and fixed row spacing unsuitable for all displays. The panel now abbreviates
large TX/RX counts and derives spacing from the selected font. Extreme counter strings fit
the actual Arial 10/16 glyph metrics at 128/240 pixels; the last row ends at 61/91 pixels.
Both target builds passed after this correction. This is a layout calculation, not a display
or button test on hardware.

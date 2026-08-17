# MicroWriter

**MicroWriter** is a BLE-keyboard writing firmware for e-paper devices,
plus a small patch system that pairs it, at build time, into a
**dual-boot** bundle with an e-reader of your choice — sharing the
device's flash and switching between the two from a menu entry, no
reflashing required.

This repo holds:

- **`editor/`** — the writer itself, built on
  [MicroSlate](https://github.com/Josh-writes/microslate-firmware) (via a
  personal [US-International fork](https://github.com/fperuzzo72/microslate-firmware-US-International)).
  **For the full feature list, keybindings, and usage guide, see
  [`editor/README.md`](editor/README.md)** — it carries this project's own
  fixes at the top, followed by the *complete, unedited* original
  MicroSlate README (Bluetooth keyboard support, note management, writing
  modes, WiFi sync, auto-save, etc.), preserved here on purpose rather
  than just linked to the [upstream repo](https://github.com/Josh-writes/microslate-firmware#readme):
  if MicroSlate changes or drops functionality upstream that this project
  hasn't picked up, the reference for what's actually running here stays
  intact regardless. Its own independent firmware, own `platformio.ini`,
  builds and flashes on its own.
- **`patches/`** — one subdirectory per supported reader
  ([CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader),
  [CrossInk](https://github.com/uxjulia/CrossInk),
  [CPR-vCodex](https://github.com/franssjz/cpr-vcodex)). Each is a set of
  scripts that edit a checkout of that reader — provided by you, not
  carried in this repo — to add the dual-boot "call to the editor" and
  protect the reader's own firmware self-update from overwriting the
  editor's OTA slot.

There's no reader source in this tree. You pick a reader, point the
patch scripts at your own checkout of it, build both, and merge the two
resulting binaries into one flashable image. See `patches/<reader>/README.md`
for the exact steps and the upstream tag each was last verified against.

**Stable release: MicroWriter 0.3.** First release under this
structure — see `editor/README.md` for the editor's own changelog
(PgUp/PgDn, the browser file manager, mDNS/OTA rename to "MicroWriter").
The `patches/` scripts themselves are new this version; see `NOTICE.md`
for exactly what changed and why.

## Why dual-boot instead of one integrated firmware

This project first tried a single integrated firmware (reader + writer +
BLE keyboard host in one binary, one input model). Hands-on testing found
that NimBLE's own runtime heap footprint (~90-100KB, invisible to static
size analysis) starves the reader's heap whenever it's initialized,
causing intermittent crashes, broken cover thumbnails, and full
page-render failures. Deferring BLE init to only run while the writer was
open worked, but at that point the writer no longer needed anything from
the reader's own codebase — so this settled on the dual-boot split
instead: two independent, purpose-built firmwares, each free to use its
full heap, switching over a warm reboot (a few seconds).

## How the switch works

All three supported readers share the same `ota_0`/`ota_1` partition
layout the editor uses (same CrossPoint-lineage ancestry), and each
already ships (or, for CPR-vCodex, easily reuses) the low-level
otadata-write primitive needed to switch boot partitions on X4-class
hardware — `esp_ota_set_boot_partition()` itself fails there with a bogus
efuse-blk-rev verification error, so both sides write the otadata
partition directly instead, same trick the web flasher uses.

On top of that, every `patches/<reader>/` set injects an `OtaApps.h`
(ported from the editor's own dual-boot code, itself adapted from
CrossInk's) that does three things:

- `registerOtaAppName(...)` — each firmware writes its own display name to
  shared NVS (`ota_names`), keyed by which OTA slot it's running from, at
  boot.
- `detectOtaApps(...)` — scans the other OTA slot(s) and reads back
  whatever name is registered there, so a menu can show "MicroWriter" (or
  whichever reader's name) instead of a generic "OTA Slot N".
- `switchToOtaApp(...)` — writes the target slot into otadata and calls
  `esp_restart()`.

Each patch set also adds a self-update guard: before that reader's own
firmware self-update writes anything, it compares the next-update
partition's embedded app descriptor against the running app's own and
refuses if they differ — protecting the editor's slot from being silently
overwritten by an unrelated firmware update. See `NOTICE.md` for how this
guard turned out to be the same literal patch across all three readers.

## Roadmap

- [x] Editor + patch system split from the old fixed reader+editor bundle
- [x] Dual-boot call + self-update guard for CrossPoint, CrossInk, and
      CPR-vCodex
- [ ] Icons for the reader-switch shortcuts (currently placeholder text)
- [ ] Port to other e-paper hardware (Paper S3, LilyGO T5S3, X4 Pro) — the
      editor/patch split exists specifically to make this a matter of new
      build tags, not a new fork
- [ ] Third boot slot: a Game Boy emulator, driven by physical buttons
      only, no BLE — needs a 3-slot partition table, deferred

## Hardware

Verified so far on the **Xteink X4** — ESP32-C3, 380KB RAM (no PSRAM),
800×480 1-bit e-ink, 5-way d-pad + power button, BLE 5.0, SD card.

## Building

```bash
# Editor
cd editor
IDF_COMPONENT_MANAGER=0 pio run -e xteink_x4
pio run -e xteink_x4 -t upload
```

For a reader, pick one of `patches/crosspoint/`, `patches/crossink/`, or
`patches/cpr-vcodex/` and follow that directory's own `README.md` — it
clones the upstream reader at a known-good tag, applies the patch
scripts, and builds it.

To flash a complete dual-boot image from scratch:

```bash
esptool.py --chip esp32c3 merge_bin \
  --flash_mode dio --flash_size 16MB \
  -o dualboot-full.bin \
  0x0      path/to/reader/bootloader.bin \
  0x8000   path/to/reader/partitions.bin \
  0xe000   path/to/boot_app0.bin \
  0x10000  path/to/reader/firmware.bin \
  0x650000 path/to/editor/firmware.bin
esptool.py --chip esp32c3 write_flash 0x0 dualboot-full.bin
```

Don't want a reader at all — just the writer, standalone, on its own
device? Run the **"Build MicroWriter Standalone"** workflow from the
Actions tab (`workflow_dispatch`, no inputs required) — it builds
`editor/` alone and publishes a ready-to-flash release, no dual-boot, no
reader pairing.

## License

MIT — see [LICENSE](LICENSE). Third-party attribution in
[NOTICE.md](NOTICE.md). Full project history and session-continuity notes
(why things are structured this way, what's verified vs. not, ideas not
yet acted on) in [docs/DEVELOPMENT_LOG.md](docs/DEVELOPMENT_LOG.md).

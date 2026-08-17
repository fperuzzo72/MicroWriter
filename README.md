# MicroWriter X4

Two-firmware **dual-boot** bundle for the **Xteink X4** e-paper device:
an EPUB reader and a Bluetooth-keyboard-driven writer, sharing the same
16MB flash and switching between each other from a menu entry — no
reflashing required.

**Stable release: MicroWriter 0.1.** Reader half of the MicroWriter X4
(MicroSlate) / CPR-vCodex pairing. Confirmed working together on physical
Xteink X4 hardware — both firmwares register their correct dual-boot menu
names ("MicroWriter", not "MicroWriter X4" — see `NOTICE.md` for why),
the self-update guard is in place, and the editor's word-wrap renders
correctly at every font size and orientation (see the editor half's own
`README.md` for that, plus its new browser-based file manager).
Paired editor commit: [microslate-firmware-US-International@51c0631](https://github.com/fperuzzo72/microslate-firmware-US-International/commit/51c0631).

This project first tried building a single integrated firmware (reader +
writer + BLE keyboard host in one binary, one input model). Hands-on
testing found that NimBLE's own runtime heap footprint (~90-100KB,
invisible to static size analysis) starves the EPUB reader's heap whenever
it's initialized, causing intermittent crashes, broken cover thumbnails,
and full page-render failures. Deferring BLE init to only run while the
writer was open worked, but at that point the writer no longer needed
anything from the reader's own codebase — so this settled on the
dual-boot split instead: two independent, purpose-built firmwares, each
free to use its full heap, switching over a warm reboot (a few seconds).

## The two firmwares

| | Reader (this repo) | Editor |
|---|---|---|
| Base | [CPR-vCodex](https://github.com/franssjz/cpr-vcodex) `1.5.0.9`, Lyra theme | [MicroSlate US-International](https://github.com/fperuzzo72/microslate-firmware-US-International) |
| Does | EPUB reading, reading stats, KOReader sync, OPDS, dictionary, flashcards, file transfer | BLE keyboard host, dead-key (US-International) text notes |
| Switches to the other via | Home/Apps shortcut **"MicroSlate"** | Home menu entry (auto-named from this firmware's own registration — see below) |

Both firmwares are built from their own source trees (MicroSlate's isn't
copied into this repo) and merged into one flashable image at the binary
level, the same way CrossInk + MicroSlate's own existing dual-boot release
does it.

## How the switch works

Both projects already share the exact same `partitions.csv` layout
(`ota_0` at `0x10000`, `ota_1` at `0x650000`, both 6.25MB — same
CrossPoint-lineage ancestry) and CPR-vCodex already ships the low-level
otadata-write primitive needed to switch boot partitions on this hardware
(`src/network/OtaBootSwitch.h/.cpp` — `esp_ota_set_boot_partition()`
itself fails here with a bogus efuse-blk-rev verification error, so both
sides write the otadata partition directly instead, same trick the web
flasher uses).

On top of that, `src/util/OtaApps.h` (ported from MicroSlate's own
dual-boot code, itself adapted from CrossInk's) does three things:

- `registerOtaAppName(...)` — each firmware writes its own display name to
  shared NVS (`ota_names`), keyed by which OTA slot it's running from, at
  boot.
- `detectOtaApps(...)` — scans the other OTA slot(s) and reads back
  whatever name is registered there, so a menu can show "MicroSlate" or
  "MicroWriter X4" instead of a generic "OTA Slot N".
- `switchToOtaApp(...)` — writes the target slot into otadata and calls
  `esp_restart()`.

**Known gap, not yet guarded against**: CPR-vCodex's own firmware
self-update (Settings > Check for Updates / SD Firmware Update) always
targets "the other OTA slot" with no awareness that the editor firmware
lives there — using either update path will silently overwrite it.

## Roadmap

- [x] Reader + writer dual-boot switch (this)
- [ ] Icons for the "MicroSlate"/reader-return shortcuts (currently a
      placeholder text icon)
- [x] Guard CPR-vCodex's self-update from overwriting the editor slot
- [ ] More editor functionality on the MicroSlate side (this project's
      name anticipates growing beyond stock MicroSlate here)
- [ ] Third boot slot: a Game Boy emulator (SUMI's `src/plugins/gb/` is
      the known-working reference) driven by physical buttons only, no
      BLE — needs a 3-slot partition table, deferred, not a current
      priority

## Hardware

Xteink X4 — ESP32-C3, 380KB RAM (no PSRAM), 800×480 1-bit e-ink, 5-way
d-pad + power button, BLE 5.0, SD card.

## Building

This repo holds both firmwares — the reader at the repo root, the editor
at `editor/` — as two independent PlatformIO projects. Build each from its
own directory:

```bash
# Reader
pio run -e default
pio run -e default -t upload

# Editor
cd editor
IDF_COMPONENT_MANAGER=0 pio run -e xteink_x4
pio run -e xteink_x4 -t upload
```

`editor/` was imported from
[microslate-firmware-US-International](https://github.com/fperuzzo72/microslate-firmware-US-International)
at its `microwriter-0.1` tag — that repo stays up as the frozen historical
record, but active editor development now happens here, at `editor/`, not
there. See `NOTICE.md` for the full attribution.

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

Reader's own `platformio.ini` pins a specific pioarduino
`platform-espressif32` build. If building outside PlatformIO's own managed
Python env, you may need `pip install littlefs-python fatfs-ng pyyaml` —
those aren't declared dependencies of a vanilla `platformio` install, only
of this specific pioarduino platform release.

## License

MIT — see [LICENSE](LICENSE). Third-party attribution in
[NOTICE.md](NOTICE.md).

# MicroWriter X4

Integrated firmware for the **Xteink X4** e-paper device: EPUB reader,
Bluetooth-keyboard-driven writer, and (later) a Game Boy emulator, in a
single binary with one input model.

This is not a dual-boot bundle of separate firmwares switching OTA slots —
reader and writer share the same Activity-based UI, the same button
mapping, and (once integrated) the same BLE keyboard connection.

## Status

Base firmware in place, writer/BLE integration next. Roadmap:

- [x] Pick a base after hands-on comparison on real hardware (see
      NOTICE.md's "Previously explored" section) — **CPR-vCodex**, for its
      CrossPoint-standard button mapping (front four = Back/Select/Left/
      Right, side buttons = Up/Down page-turn), incremental (not
      full-screen) UI updates, and its Lyra theme's per-book reading
      stats (time spent, session count) alongside a single-book Home,
      which is what this project wants.
- [ ] Port MicroSlate's writer in as a plugin/Activity (dead-key
      US-International layout, typewriter/pagination modes, autosave)
- [ ] BLE keyboard integration: connect to a keyboard and drive menu
      navigation, EPUB page-turns, and the writer from it
- [ ] Auto-connect to the nearest/strongest BLE HID keyboard in pairing
      mode on boot (in addition to reconnecting known keyboards)
- [ ] Game Boy emulator — deferred, not a current priority

## Where each piece comes from

This is not a fork of any single project — no shared git history, no
upstream remote pointing anywhere. Code was **copied in** from a few MIT-
licensed sources and adapted; see [NOTICE.md](NOTICE.md) for full
attribution, original license text, and what was tried and dropped before
landing here.

| Piece | Source |
|---|---|
| Activity-based UI, reading engine, rendering stack, reading statistics, dictionary, flashcards, KOReader sync, themes | [CPR-vCodex](https://github.com/franssjz/cpr-vcodex), itself a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) |
| Low-level display/hardware SDK | [open-x4-sdk](https://github.com/crosspoint-reader/community-sdk) (CPR-vCodex's own dependency, flattened here instead of kept as a submodule) |
| Writer plugin (planned) | [MicroSlate](https://github.com/Josh-writes/microslate-firmware) ([US-International fork](https://github.com/fperuzzo72/microslate-firmware-US-International)) |

## Hardware

Xteink X4 — ESP32-C3, 380KB RAM (no PSRAM), 800×480 1-bit e-ink, 5-way
d-pad + power button, BLE 5.0, SD card.

## Building

```bash
pio run -e default
pio run -e default -t upload
```

Uses a pinned pioarduino `platform-espressif32` build (see
`platformio.ini`). If building outside PlatformIO's own managed Python
env, you may need `pip install littlefs-python fatfs-ng pyyaml` — those
aren't declared dependencies of a vanilla `platformio` install, only of
this specific pioarduino platform release.

## License

MIT — see [LICENSE](LICENSE). Third-party attribution in
[NOTICE.md](NOTICE.md).

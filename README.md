# MicroWriter X4

Integrated firmware for the **Xteink X4** e-paper device: EPUB reader,
Bluetooth-keyboard-driven writer, and a Game Boy emulator, in a single
binary with one input model.

This is not a dual-boot bundle of separate firmwares switching OTA slots —
reader, writer, and emulator share one state machine, one plugin host, and
one BLE keyboard connection, driven the same way from the home menu, the
reader, and every plugin.

## Status

Early scaffolding. The source tree currently starts from a copy of
[SUMI](https://github.com/psychoplath9450/SUMI) as-is. Work in progress:

- [ ] Home screen reskinned to a CrossPoint-style layout (last book read on
      top, menu list directly below — no decorative art)
- [ ] MicroSlate's writer ported in as a plugin (dead-key US-International
      layout, typewriter/pagination modes, autosave)
- [ ] Auto-connect to the nearest/strongest BLE HID keyboard in pairing mode
      on boot (in addition to reconnecting known keyboards, which already
      works)
- [ ] Game Boy plugin: fill the keyboard-input gap for A/Select/Start
      (D-pad and B already work from a keyboard; A/Select/Start currently
      only work from the physical buttons)

## Where each piece comes from

This is not a fork of any single project — no shared git history, no
upstream remote pointing anywhere. Code was **copied in** from a few MIT-
licensed sources and adapted; see [NOTICE.md](NOTICE.md) for the full
attribution and the original license text of each.

| Piece | Source |
|---|---|
| State machine, plugin host, BLE HID keyboard host, rendering stack, EPUB engine, Game Boy emulator | [SUMI](https://github.com/psychoplath9450/SUMI), itself built on [Papyrix](https://github.com/bigbag/papyrix-reader) and [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) |
| Writer plugin (text editor core, US-International dead keys, note file handling) | [MicroSlate](https://github.com/Josh-writes/microslate-firmware) ([US-International fork](https://github.com/fperuzzo72/microslate-firmware-US-International)) |
| Home screen layout (book-on-top, menu-list-below) | Visual arrangement only, matching CrossPoint's built-in `CLASSIC` home theme as used in [CrossInk](https://github.com/uxjulia/CrossInk) — reimplemented from scratch, no code copied (SUMI's state machine and CrossPoint's Activity/ActivityManager are different architectures) |

## Hardware

Xteink X4 — ESP32-C3, 380KB RAM (no PSRAM), 800×480 1-bit e-ink, 5-way
d-pad + power button, BLE 5.0, SD card. Same target as all the projects
above.

## Building

Standard PlatformIO project, same toolchain as SUMI/Papyrix/CrossPoint:

```bash
pio run
pio run -t upload
```

See `docs/` for the inherited architecture notes (state machine, memory
model, plugin authoring) — accurate to the current codebase since the
firmware core hasn't diverged from SUMI yet.

## License

MIT — see [LICENSE](LICENSE). Third-party attribution in
[NOTICE.md](NOTICE.md).

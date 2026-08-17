# Third-party notices

MicroWriter X4 is not a fork of any of the projects below — no shared git
history, no upstream remote. Its source tree started as a copy of
CPR-vCodex's files (replacing an earlier SUMI-based starting point — see
git history for that first attempt and why it was dropped). This file
preserves the original copyright notices as required by the MIT license on
each source.

This repo carries both halves of the two-firmware dual-boot bundle: the
**reader** at the repo root, and the **editor** (MicroSlate) at `editor/`
— two independent firmwares, each with its own `platformio.ini`, merged
only at the binary level (flashed to separate OTA slots), not sharing
source. See the "MicroSlate" section below for `editor/`'s own origin and
license notices.

## CPR-vCodex — base firmware (Activity architecture, reading engine,
## rendering stack, reading statistics, dictionary, flashcards, KOReader
## sync, themes, OTA-partition-switch primitive)

Source tree copied from <https://github.com/franssjz/cpr-vcodex> (`1.5.0.9`).

```
MIT License

Copyright (c) 2025 Dave Allie

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

CPR-vCodex is itself a personal fork of **CrossPoint Reader**
(<https://github.com/crosspoint-reader/crosspoint-reader>) by Dave Allie,
maintained by franssjz — MIT licensed, same notice as above (CPR-vCodex's
own `LICENSE` file carries Dave Allie's original copyright, unchanged by
the fork, which is standard MIT-fork practice).

`src/network/OtaBootSwitch.h/.cpp` (the low-level otadata-write primitive
this project's dual-boot switch relies on) ships natively in CPR-vCodex —
not ported in by this project — and is used here as-is via CPR-vCodex's own
codebase, under the same license above.

## open-x4-sdk — low-level display/hardware SDK

CPR-vCodex depends on this as a git submodule
(<https://github.com/crosspoint-reader/community-sdk>); flattened into a
plain copied directory here (`open-x4-sdk/`) rather than kept as a
submodule, per this project's own "copy files, don't link to other repos"
convention.

```
MIT License

Copyright (c) 2025 Open X4 E-Paper Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## OtaApps.h — dual-boot sibling-app name registry/detection/switch (`src/util/OtaApps.h`)

The register/detect/switch scheme (not the underlying otadata-write, which
is CPR-vCodex's own — see above) is ported from **MicroSlate**
(<https://github.com/Josh-writes/microslate-firmware>, used via the
US-International fork <https://github.com/fperuzzo72/microslate-firmware-US-International>),
whose own `src/main.cpp` implements the identical
`registerOtaAppName`/`detectOtaApps`/`switchToOtaApp` functions. MicroSlate
itself adapted this from a build-time patch (`scripts/patch-crossink/`)
that CrossInk (<https://github.com/uxjulia/CrossInk> by uxjulia, MIT,
copyright Dave Allie — same family as CrossPoint/CPR-vCodex) applies to
its own `src/activities/home/HomeActivity.cpp` to support dual-booting with
MicroSlate.

## MicroSlate — the editor half of this dual-boot pair

Lives at `editor/` in this repo — its own independent firmware (own
`platformio.ini`, builds separately, flashed to the other OTA slot) — as
of MicroWriter 0.1, imported from
<https://github.com/fperuzzo72/microslate-firmware-US-International>
(a personal US-International-keyboard-layout fork of
<https://github.com/Josh-writes/microslate-firmware>) at its own
`microwriter-0.1` tag. That original repo stays up, frozen at that same
tag, as the historical record and MIT attribution source (`editor/LICENSE`
is preserved unchanged in the copy) — but going forward, all editor
development happens here, at `editor/`, not there.

On-device, this editor registers itself via `registerOtaAppName` as
"MicroWriter" (the name this reader's Home/Apps shortcut shows for it, and
also the mDNS hostname its webserver advertises — see that repo's own
README.md), not "MicroSlate" — a display-name choice for the bundle as a
whole, not a change of attribution. The underlying codebase remains
MicroSlate, credited above and in that repo's own notices.

The "X4" suffix is deliberately dropped from this runtime identifier (it
still appears in this repo's own name and docs, since this integration is
X4-specific): the mDNS name and OTA-registered app name are the pieces
that would need to stay meaningful if this codebase is ever ported to a
different device, so they're named after the product, not the hardware.

## Previously explored, not used

Two earlier bases were built, flashed to a physical Xteink X4, and compared
hands-on before this project settled on CPR-vCodex:

- **SUMI** (<https://github.com/psychoplath9450/SUMI>) — used as this
  project's *first* starting point (see early git history). Dropped after
  hands-on testing: its state-machine UI redraws the full screen on every
  menu selection change, and its button mapping doesn't match the
  CrossPoint/CrossInk conventions this project wants to keep. No SUMI code
  remains in the current tree.
- **Papyrix** (<https://github.com/bigbag/papyrix-reader>) — SUMI's own
  base, inspected only for research (never built or copied from directly).

This project also briefly tried a **single integrated firmware** (reader +
BLE-keyboard writer + WriterActivity in one CPR-vCodex binary, no dual-boot)
before settling on the dual-boot split above. NimBLE's runtime heap
footprint made that approach unreliable for reading; see the README and
git history (commits around "Fix heap starvation" and the base-reset that
followed) for the full investigation. No code from that attempt remains in
the current tree.

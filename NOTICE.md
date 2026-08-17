# Third-party notices

MicroWriter is not a fork of any of the projects mentioned below — no
shared git history, no upstream remote, and (as of this version) no
third-party source code embedded in this repo's tree at all.

This repo is the **editor** (`editor/`, the MicroSlate-derived writing
firmware) plus a **patch system** (`patches/`) that pairs it with a
reader firmware of your choice at build time — see "Patches" below.
There is no reader source checked into this repo; you provide your own
checkout of whichever reader you want to pair with, and the patch
scripts modify *that* checkout.

## History: this repo used to carry a full reader copy

Through the `microwriter-0.2` tag, this repo's root *was* a copy of
[CPR-vCodex](https://github.com/franssjz/cpr-vcodex) `1.5.0.9` (itself a
personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
by Dave Allie, maintained by franssjz), plus a flattened copy of
[open-x4-sdk](https://github.com/crosspoint-reader/community-sdk) (MIT,
Open X4 E-Paper Contributors) that CPR-vCodex depends on. Both are gone
from the current tree — replaced by the `patches/cpr-vcodex/` and
`patches/crosspoint/` scripts below, which apply the same dual-boot
integration to a checkout you provide instead of a copy this repo
carries. Full MIT attribution for that source lives in its own
`LICENSE`/history at those tags, not duplicated here.

An even earlier version briefly used [SUMI](https://github.com/psychoplath9450/SUMI)
as its starting point before switching to CPR-vCodex — see early git
history. No SUMI code remains, and never made it past that first attempt.

This project also briefly tried a **single integrated firmware** (reader +
BLE-keyboard writer + WriterActivity in one CPR-vCodex binary, no
dual-boot) before settling on the dual-boot split. NimBLE's runtime heap
footprint made that approach unreliable for reading; see git history
(commits around "Fix heap starvation" and the base-reset that followed).

## MicroSlate — the editor (`editor/`)

Its own independent firmware (own `platformio.ini`, builds and flashes
separately from whichever reader it's paired with) — imported in full at
MicroWriter 0.1 from
<https://github.com/fperuzzo72/microslate-firmware-US-International>
(a personal US-International-keyboard-layout fork of
[MicroSlate](https://github.com/Josh-writes/microslate-firmware)) at that
repo's own `microwriter-0.1` tag. That original repo stays up, frozen at
that tag, as the historical record and MIT attribution source
(`editor/LICENSE` is preserved unchanged in the copy) — active editor
development happens only here, at `editor/`, from MicroWriter 0.1 onward.

On-device, the editor registers itself via `registerOtaAppName` as
"MicroWriter" (also the mDNS hostname its webserver advertises), not
"MicroSlate" — a display-name choice for the product, not a change of
attribution. The underlying codebase remains MicroSlate, credited above
and in that repo's own notices. The "X4" suffix is deliberately dropped
from this runtime identifier — unlike this project's own name, which
stays put — so it keeps meaning the same thing regardless of which
reader, or which device, it ends up paired with.

## Patches (`patches/`)

Each subdirectory is a set of scripts that inject dual-boot support into
a checkout of the named reader — not a copy of that reader's source, a
set of edits applied to a copy *you* provide (see each directory's own
`README.md` for the exact upstream tag it was built and verified
against, and how to run it). Two things get patched into every target:

1. **The call to the editor** — `OtaApps.h` (register/detect/switch,
   originally ported into MicroSlate from a CrossInk build-time patch,
   itself adapted from CrossInk's own `HomeActivity.cpp` dual-boot
   support — see `patches/crossink/`) plus a menu entry wired to it.
2. **Self-update protection** — before that reader's own firmware
   self-update writes anything, it compares the next-update partition's
   embedded app descriptor against the running app's own and refuses to
   proceed if they differ, so a self-update can never overwrite the
   editor's OTA slot (or leave otadata pointing somewhere the dual-boot
   switch no longer makes sense of). Originally hand-written directly
   against CPR-vCodex; the same anchor turned out to be byte-identical
   across CrossPoint, CrossInk, and CPR-vCodex, so the same patch logic
   covers all three.

Current targets:

- **`patches/crosspoint/`** — [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader),
  MIT, copyright Dave Allie.
- **`patches/crossink/`** — [CrossInk](https://github.com/uxjulia/CrossInk),
  MIT, copyright Dave Allie (same family as CrossPoint/CPR-vCodex).
- **`patches/cpr-vcodex/`** — [CPR-vCodex](https://github.com/franssjz/cpr-vcodex),
  MIT — see "History" above.

None of these patches embed or redistribute the target project's source;
they only edit a checkout you provide. Attribution for each project's own
code stays in that project's own `LICENSE`.

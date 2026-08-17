# Development log / session continuity

This file exists so work on MicroWriter can resume from a fresh Claude
Code session — a different computer, a new install, no memory of prior
conversations — without losing context. It's the "less important, but not
lost" home for history that doesn't belong in the top-level `README.md`
(kept lean, current-state-only) but is genuinely useful for whoever
touches this project next, human or AI.

Update this file at the end of any session that changes the project's
direction, not just its code — a new feature is usually fine documented
by its own commit message; an architecture decision, a rename, a "why did
we do it this way" belongs here too.

## Current state, in one paragraph

MicroWriter is a BLE-keyboard writing firmware for e-paper devices
(`editor/`, built on MicroSlate), paired at build time — via patch
scripts, not a copied/forked source tree — with a reader of the user's
choice (`patches/crosspoint/`, `patches/crossink/`, `patches/cpr-vcodex/`).
There is no reader source checked into this repo. The two firmwares are
built independently and merged at the binary level into one flashable
dual-boot image (reader at OTA slot `app0`/`0x10000`, editor at
`app1`/`0x650000`). Verified hands-on on physical Xteink X4 hardware,
paired with CPR-vCodex, through MicroWriter 0.2. The patch system (0.3)
has been verified by applying it to real, current upstream checkouts —
see "What's NOT verified" below.

## How the project got here (chronological)

### 1. Origin: MicroWriter-X4, an integrated reader+writer attempt

Started as a fork-flavored copy of CPR-vCodex (a personal fork of
CrossPoint Reader) with a BLE-keyboard writer bolted directly into the
same binary as the EPUB reader. Abandoned: NimBLE's runtime heap
footprint (~90-100KB, invisible to static analysis) starved the reader
whenever BLE was initialized, causing crashes and broken rendering — even
when BLE only ran while the writer screen was open. Once the writer no
longer needed anything from the reader's own codebase, the project
switched to a **dual-boot split** instead: two independent firmwares,
each free to use its full heap, switching via a warm reboot. This is
still the current architecture's foundation.

### 2. The dual-boot pairing: CPR-vCodex (reader, this repo) + MicroSlate (editor, separate repo)

The reader repo (then named `MicroWriter-X4`) was reset to a pristine
CPR-vCodex 1.5.0.9 copy, plus `src/util/OtaApps.h` (register/detect/switch
of OTA sibling apps — ported from MicroSlate's own dual-boot code, itself
adapted from a CrossInk build-time patch) and a "MicroSlate" shortcut
wired through `ShortcutRegistry.h`/`AppsActivity.cpp`/`HomeActivity.cpp`.
The editor was a *separate* repo,
[microslate-firmware-US-International](https://github.com/fperuzzo72/microslate-firmware-US-International)
(a personal US-International-dead-key fork of
[MicroSlate](https://github.com/Josh-writes/microslate-firmware)),
referenced only by commit hash in docs/tags — no git-level link (no
submodule, no shared history) between the two repos.

Fixes made directly against this CPR-vCodex copy during this phase (all
still applicable, now living in `patches/cpr-vcodex/`):
- **Pixel-accurate word-wrap** (editor side): word-wrap was estimating
  characters-per-line from one sample string's average glyph width;
  any line averaging wider than that sample overflowed the screen and got
  silently clipped. Fixed by summing real glyph advance widths per line
  instead of estimating.
- **UTF-8 backspace fix** (editor side, predates this session): deleting
  an accented character (dead-key composed, 2+ UTF-8 bytes) left garbage
  — the original code tested the wrong byte offset for continuation bytes.
- **Self-update guard** (reader side): CPR-vCodex's own firmware
  self-update always targets "the other OTA slot" with no awareness the
  editor lives there. `destHoldsForeignApp()` compares the next-update
  partition's embedded app descriptor against the running app's own and
  refuses the update before erasing anything if they differ.
- **Browser file manager** (editor side): replaced/augmented the
  Python-script-based WiFi sync with a served-at-`/` HTML file manager
  (upload/download/delete), reusing CPR-vCodex's own webserver UI as
  inspiration but rebuilt lean (the source page is an EPUB/manga
  conversion tool with a jszip dependency, not a generic file manager —
  none of that applies to a flat folder of small `.txt` notes).
- **Selection, clipboard, PgUp/PgDn** (editor side): Shift+arrow selection
  (anchor + live cursor, not WordStar-style Ctrl-K mark-begin/mark-end),
  single-slot clipboard, Ctrl+A select-all, and mode-aware PgUp/PgDn (a
  real screen-sized jump even in Typewriter mode, which only ever draws
  one line — see `editorGetPageJumpLines()` vs `editorGetStoredVisibleLines()`
  in `editor/src/text_editor.cpp`/`ui_renderer.cpp`).

Version tags from this phase: `microwriter-x4-stable` (first stable
pairing), `microwriter-0.1` (selection + clipboard), `microwriter-0.2`
(PgUp/PgDn). The annotated tag messages themselves are the authoritative
per-version changelog — this doc only summarizes.

### 3. Naming: dropping the "X4" suffix from runtime identifiers

The reader's own `registerOtaAppName` call and the editor's mDNS hostname
both said "MicroWriter X4" / `microwriter-x4.local`. Renamed to just
"MicroWriter" / `microwriter.local` — the hardware-specific suffix was
deliberately dropped from these two *runtime* identifiers (not the
project's own name, which stayed `MicroWriter-X4` at the time) so they'd
stay meaningful if this codebase were ever paired with different hardware.
The GitHub repo itself was later renamed `MicroWriter-X4` → `MicroWriter`
to match (old URL still 301-redirects). The editor repo
(`microslate-firmware-US-International`) was deliberately *not* renamed —
its name describes its own distinguishing feature (US-International dead
keys), not the MicroWriter brand.

### 4. Consolidation: editor imported into this repo, upstream frozen

Per an explicit decision: `microslate-firmware-US-International` (the
separate editor repo) was declared **frozen** at its `microwriter-0.1`
tag — no more direct edits there — and its full source was imported into
*this* repo at `editor/` (a plain file copy at that tag, not a git
subtree/submodule; NOTICE.md documents the provenance). All editor
development moved to `editor/` from that point on. This is why `editor/`
has its own independent `platformio.ini`/git-untracked build output/etc.
— it's meant to be a complete, standalone firmware project, just living
inside this repo instead of its own.

### 5. The big pivot: drop the reader source, add a patch system (this session, MicroWriter 0.3)

Through `microwriter-0.2`, this repo's root *was* a full copy of
CPR-vCodex 1.5.0.9 (the reader), same pattern as step 2 but now living
here instead of a separate repo — meaning every reader-side fix
(self-update guard, etc.) was hand-applied to a permanently-diverged
fork, with no way to pick up upstream CPR-vCodex fixes without redoing
the diff by hand, and no way to pair with a *different* reader (CrossPoint,
CrossInk) without a from-scratch fork.

The fix: this repo already contained (unused) precedent for a better
approach — two CI pipelines, `editor/.github/workflows/build-crosspoint.yml`
(patches embedded inline in YAML) and `editor/scripts/patch-crossink/` +
`build-crossink.yml` (patches as standalone Python scripts, the cleaner of
the two), that clone an upstream reader fresh, apply a small set of
literal-text-anchored patches (`assert old in src`, fails loud on a
mismatch rather than silently corrupting), build it, and merge with the
editor into a flashable image. Neither workflow was *active* CI though —
both lived under `editor/.github/workflows/`, and GitHub only recognizes
workflows at the repo root.

What changed this session:
- Reader source (`src/`, `lib/`, `open-x4-sdk/`, `platformio.ini`, etc.)
  removed from the repo root entirely. Still in git history through the
  `microwriter-0.2` tag — nothing destroyed, just off the current tree.
- All four workflow files moved from `editor/.github/workflows/` to the
  repo root's `.github/workflows/` (where GitHub actually reads them),
  paths fixed for the new layout.
- `editor/scripts/patch-crossink/` moved to `patches/crossink/` (it
  patches a *different* project than the editor, so nesting it under
  `editor/` never made sense).
- `patches/crosspoint/` — new: the inline-YAML patches from
  `build-crosspoint.yml` extracted into standalone scripts (same content,
  plus one correctness fix: `switchToOtaApp` now uses
  `ota_boot::switchTo()` instead of calling `esp_ota_set_boot_partition()`
  directly, which fails on X4-class hardware with the same efuse-blk-rev
  bug the CrossInk/editor code already works around — the original inline
  version had this bug and it went unnoticed because it was never
  actually flash-tested against real hardware, only CI-compiled).
- `patches/cpr-vcodex/` — new, 8 scripts (not 4-5 like the other two):
  CPR-vCodex routes shortcuts through a `ShortcutRegistry`/
  `ShortcutDefinition` abstraction with settings fields and i18n-driven
  strings, not a raw `switch`/`case` in `HomeActivity.cpp` like
  CrossPoint/CrossInk — so it needs its own patch shape, derived fresh
  from a pristine CPR-vCodex 1.5.0.9 checkout (*not* replayed from this
  session's own historical git diff, which was tangled up with unrelated
  changes from the abandoned single-integrated-firmware attempt).
- Self-update guard (`destHoldsForeignApp`) added as a *new* patch step to
  all three targets. Confirmed while building this: `FirmwareFlasher.cpp`'s
  `flashFromSdPath()` — specifically the `esp_ota_get_next_update_partition()`
  call and its surrounding comment/null-check — is **byte-for-byte
  identical** across CrossPoint, CrossInk, and CPR-vCodex (same ancestry),
  so the same literal guard patch applies to all three unchanged.
- Root `README.md`/`NOTICE.md` rewritten to describe the new reality:
  MicroWriter is the editor + a patch system, not a fixed reader+editor
  bundle. GitHub repo description updated to match (was still describing
  the very first SUMI-based single-integrated-firmware idea from the
  project's actual origin, long superseded).

## What's verified vs. NOT verified for the 0.3 patch system

**Verified:** all three patch sets (`patches/crosspoint/`,
`patches/crossink/`, `patches/cpr-vcodex/`) were run against real, current,
tagged upstream checkouts —

- CrossPoint `1.4.1` (`~/github/crosspoint-reader`)
- CrossInk `v1.3.4` (`~/github/CrossInk` — note: HEAD/`main` is v1.5.0-era
  and `03_patch_home_activity_cpp.py` does *not* apply cleanly there,
  meaning CrossInk's `HomeActivity.cpp` menu-building code changed between
  1.3.4 and main; the other four CrossInk scripts still matched current
  `main` when checked)
- CPR-vCodex `1.5.0.9-cpr-vcodex` (`~/github/cpr-vcodex`)

Every script applied cleanly and the resulting source greps clean for all
the expected symbols (`otaAppCount`, `registerOtaAppName`,
`destHoldsForeignApp`, `SIBLING_APP_PROTECTED`, etc. — see each
`patches/<target>/README.md` for the exact list). These three repos are
cloned locally at `~/github/` for exactly this kind of reference/testing;
they're pristine (unpatched) clones, not modified.

**NOT verified: a full `pio run` compile of any patched checkout.** Every
attempt hit the same environment-level failure —
`~/.platformio/platforms/espressif32/builder/penv_setup.py`'s
`install_python_deps()` calls out to `uv pip install` for a fixed list of
build-tool dependencies, and that step failed for two independent
reasons encountered back to back:
1. `uv` itself wasn't installed in the Python venv being used to drive
   `pio` (fixed by installing it there).
2. One of the pinned dependencies is a direct GitHub zip download
   (`pioarduino/platformio-core` at a fixed tag) that hit a `429 Too Many
   Requests` from `codeload.github.com` — likely from this session's own
   heavy GitHub usage (many clones/downloads). Installing the same
   version from PyPI instead didn't help, since the platform's dependency
   check appears to always re-fetch URL-pinned specs regardless of what's
   already installed under that package name.

Separately, CrossInk 1.3.4's `platformio.ini` pins pioarduino
`platform-espressif32` release `55.03.37`, which `00_pin_stable_pioarduino_platform.py`'s
own docstring already documents as having a real packaging bug
(`framework-arduinoespressif32` fails to resolve) — that patch downgrades
to `55.03.36-1` specifically to work around it. Pristine CPR-vCodex
1.5.0.9 pins that same buggy `55.03.37` release directly, with no
equivalent workaround patch yet.

**Next step for whoever picks this up:** retry `pio run` locally once the
GitHub rate limit clears (probably just needs time), or trust the
`.github/workflows/build-*.yml` CI runs (`workflow_dispatch`, run them
from the GitHub Actions tab) — they run in a clean environment without
this session's accumulated rate-limit exposure, and are the workflows
these patches are actually meant to be validated by. If CPR-vCodex's own
build also needs a pioarduino downgrade like CrossInk's, add a
`00_pin_stable_pioarduino_platform.py`-equivalent to `patches/cpr-vcodex/`.

## Ideas raised but not acted on

- **Porting to other hardware** (Paper S3, LilyGO T5S3, X4 Pro — user has
  the first two on hand, planning to acquire the third): the whole
  editor/patch split exists to make this feasible later, but no work has
  started. Would need new PlatformIO build environments (board configs,
  pin mappings, display drivers) in `editor/platformio.ini`, and the
  patch scripts would need per-reader-*and*-per-device awareness if the
  same reader is ever built for multiple boards.
- **Formalizing the reader↔editor commit pairing**: right now "which
  editor commit is this reader version tested against" is tracked only in
  prose (README) and git tag annotation messages, updated by hand each
  time. A pinned-commit manifest file or git submodule was discussed as a
  future option but explicitly deferred — not needed now that both live
  in one repo anyway (this concern mostly applied to the old two-repo
  split, which no longer exists).
- **Icons for the reader-switch shortcuts** — currently placeholder text
  (`UIIcon::Text`) in all three patch sets and in `editor/`'s own shortcut
  list.
- **Third boot slot**: a Game Boy emulator, physical-buttons-only (no
  BLE), referenced from the project's very first SUMI-based attempt
  (`src/plugins/gb/` there was the known-working reference). Needs a
  3-slot partition table. Not a current priority.

## Where things live

- `editor/` — the writer firmware. Independent PlatformIO project.
- `patches/<crosspoint|crossink|cpr-vcodex>/` — patch scripts + a
  `README.md` per target documenting the verified-against tag and usage.
- `.github/workflows/build-<target>.yml` — CI: clone reader at a chosen
  tag, apply patches, build both firmwares, merge, publish a release.
  `build.yml`/`release.yml` are the editor's own standalone
  build/release, unrelated to dual-boot pairing (the latter also pushes
  built firmware to an external `typeslate-website` repo using a
  `WEBSITE_PAT` secret that almost certainly isn't configured on *this*
  repo — left in place but flagged with a comment in the workflow file;
  decide whether to keep, remove, or configure it).
- `NOTICE.md` — third-party attribution, plus the fullest account of *why*
  things are structured this way (patches vs. copied source, naming
  decisions, etc.) — read this alongside this file for the complete
  picture.
- `~/github/crosspoint-reader`, `~/github/CrossInk`, `~/github/cpr-vcodex`
  (sibling directories, not inside this repo) — pristine reference clones
  of the three supported readers, used to build and verify the patches
  above. Not part of this repo; recreate with a plain `git clone` if
  they're not present on a new machine.

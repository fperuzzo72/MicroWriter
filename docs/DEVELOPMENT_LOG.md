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
choice (`patches/crosspoint/` for CrossPoint `1.4.1`,
`patches/crosspoint-1.5.0/` for CrossPoint `v1.5.0`, `patches/crossink/`,
`patches/cpr-vcodex/`). There is no reader source checked into this repo.
The two firmwares are built independently and merged at the binary level
into one flashable dual-boot image (reader at OTA slot `app0`/`0x10000`,
editor at `app1`/`0x650000`). Verified hands-on on physical Xteink X4
hardware, paired with CPR-vCodex, through MicroWriter 0.2. The patch
system (0.3) has been verified against real, current upstream checkouts of
all four reader targets (three distinct readers, two CrossPoint versions),
including a full compile of each — see "What's verified" below. The
editor's own build (with the 0.3 branding fixes) has been flash-verified
on physical hardware as a slot-only OTA-slot update; the patch-built
*readers* themselves have not yet been flash-tested on physical hardware
(0.2's hardware verification predates the patch system; CPR-vCodex was
hand-edited then, not patch-built) — only CI/local `pio run` compiles.

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

### 6. Branding cleanup, SD settings migration, CrossPoint 1.5.0, and the "always wakes to the reader" finding (this session)

**Remaining "MicroSlate" branding leaks fixed** (commit `c0d6977`): the
Home-screen header (`editor/src/ui_renderer.cpp`'s `drawMainMenu()`), the
BLE-advertised device name (`NimBLEDevice::init(...)` in
`editor/src/ble_keyboard.cpp`), the sleep-screen title, and two debug log
lines all still said "MicroSlate" — renamed to "MicroWriter". The
attribution comment explaining *why* the codebase itself is credited as
MicroSlate in `NOTICE.md` was deliberately left alone; only user-visible
runtime strings changed.

**SD settings folder migrated `/microslate` → `/microwriter`**, with a
safe rename-in-place fallback rather than a blind rename: a new
`ensureSettingsDir()` helper (`editor/src/sd_backup.h`) checks for
`/microwriter` first, then for a pre-migration `/microslate` to rename in
place (preserving BLE pairing / WiFi credentials / UI prefs already saved
there), and only creates a fresh empty `/microwriter` if neither exists.
Wired into `main.cpp`, `ble_keyboard.cpp`, and `wifi_sync.cpp` (all three
previously had their own hand-rolled "does this dir exist" checks against
the old path).

**CrossPoint's patch set split by version.** CrossPoint's latest stable is
`v1.5.0`, not `1.4.1` (what `patches/crosspoint/` was built and verified
against). `v1.5.0` refactored `HomeActivity::loop()`'s menu-activation
code — the `switch (indexToMenuItem(...))` that patch 3 anchors on moved
from being directly nested inside `if (mappedInput.wasReleased(Confirm))`
into a shared lambda (`activateSelection`, now also called by new
touch/swipe handlers `v1.5.0` added) — which broke patch 3's anchor
(patches 1, 2, 4, 5 were unaffected: none of their anchor points moved).
Rather than replace `patches/crosspoint/`, a new, separate
`patches/crosspoint-1.5.0/` was created — patch 3 rewritten for the new,
shallower brace nesting the switch now sits inside (the switch/case logic
itself is byte-identical to 1.4.1's), patches 1/2/4/5 copied over
unchanged. `patches/crosspoint/` (1.4.1) is untouched and still verified
working. See `patches/crosspoint-1.5.0/README.md` for the exact diff.
Note also: as of `v1.5.0` CrossPoint's SDK submodule was renamed
`open-x4-sdk` → `freeink-sdk` (`1.4.1` still uses `open-x4-sdk`) — both
patch sets' own `git submodule update --init --recursive` step handles
this transparently, but it's a trap if you're diffing checkouts by hand.

**"Always wakes into the reader" tracked down to a flashing-procedure
issue, not a firmware bug.** One physical device consistently resumed
whichever firmware (reader or editor) was active before a power-button
sleep/wake cycle; another, that had received several full-image
reflashes over time, always came back up on the reader regardless of what
was active before. Neither the editor's `enterDeepSleep()`
(`editor/src/main.cpp`) nor CPR-vCodex's own deep-sleep path touch
`otadata` at all — deep-sleep wake on ESP32 is a full reset through the
bootloader, which reads `otadata` to pick the boot partition, so as long
as nothing touches `otadata` during sleep/wake, waking naturally resumes
whatever was active before. Nothing does, on either firmware — sleep/wake
itself was never the actual cause.

The real cause: every `*-full.bin` dual-boot image (all four
`.github/workflows/build-*.yml`) is produced by `esptool merge_bin`
writing `boot_app0.bin` at `0xe000` — the same offset as the `otadata`
partition. `boot_app0.bin` is ESP-IDF's stock "initialize otadata to boot
`ota_0`" blob, so **every full-image reflash silently resets the active
OTA slot back to the reader**, discarding whatever slot (reader or
editor) was actually last active. The `*-slot-only.bin` artifacts (item 4
and 5 in every release) never touch `0x0`-`0x10000`, so they leave
`otadata` — and therefore "which firmware resumes on next boot/wake" —
completely alone.

Practical fix, no source change needed: **prefer slot-only updates for
any device that already has dual-boot provisioned; reserve the
`*-full.bin` images for first-time setup**, where there's no prior
`otadata` state worth preserving. Added this guidance to all four
release-notes bodies (`build-crosspoint.yml`, `build-crosspoint-1.5.0.yml`,
`build-crossink.yml`, `build-cpr-vcodex.yml`). Confirmed on hardware while
verifying this: read the connected device's actual on-flash partition
table via `esptool read_flash` before touching it (matched
`editor/partitions.csv` exactly), then flashed only `0x650000` (the
editor's own OTA slot) with the current build — `otadata` was
byte-for-byte identical before and after (same two sequence numbers), and
the editor's own embedded version string confirmed the new build
(`microwriter-0.3-2-gc0d6977`) took.

**All four reader/editor pairings now build clean** — CPR-vCodex
`1.5.0.9-cpr-vcodex`, CrossInk `v1.3.4`, CrossPoint `1.4.1`, CrossPoint
`v1.5.0` — each in a disposable `git worktree`, patched, and compiled
(`pio run`) with today's editor build; artifacts refreshed in
`dualboot_artifacts/` (slot-only + full image per target) and `artifacts/`
(standalone editor).

### 7. The *real* sleep/wake culprit: ESP-IDF app rollback, not otadata resets

The `boot_app0.bin`/full-flash explanation above was real but incomplete —
it explains why a full reflash discards the active slot, but a second
device kept waking into the reader even after a **slot-only** editor
update (`otadata` confirmed byte-identical before/after by direct flash
read). Something was resetting the boot target on a plain sleep/wake
cycle, with no flash of any kind involved.

Root cause, confirmed by reading the two test devices' otadata directly
(`ota_state` field, offset 24 of each 32-byte `SelectEntry` — see
`OtaBootSwitch.h`'s struct — which the earlier investigation hadn't
inspected, only `ota_seq`/`crc`): `ota_boot::switchTo()` (the shared
otadata-write primitive both our dual-boot switch *and* each reader's own
genuine firmware self-update reuse, since `esp_ota_set_boot_partition()`
fails on this hardware) always writes the newly-selected slot with state
**`NEW`** — ESP-IDF's own "OTA app rollback" bookkeeping, meant to give a
freshly-written, unverified firmware image one boot to prove itself
(via `esp_ota_mark_app_valid_cancel_rollback()`) before the *next* reset
rolls it back automatically. Neither the editor nor any reader ever calls
that confirmation function. On hardware where the physically-flashed
bootloader has this rollback feature enabled, a slot switched into and
then put to sleep — before anything confirms it — gets silently rolled
back to the sibling slot on the very next reset, indistinguishable from a
plain wake.

Bootloader is a shared, rarely-rewritten partition (`0x0`, only touched by
a full-image flash), so *which* bootloader — and therefore whether this
protection is even compiled in — depends entirely on which project last
did a full flash on a given device, not on which reader or editor version
is currently running. Confirmed hands-on on the two physical test
devices: the one that never had the bug has a bootloader byte-identical
(bar a 38-byte version-string/hash difference) to the editor's own
`bootloader.bin`, which explicitly ships `BOOTLOADER_APP_ROLLBACK_ENABLE`
disabled — its otadata showed **both** entries stuck at `NEW`, including
the currently-*active* one, meaning that bootloader was never even
transitioning the state field, i.e. never enforcing rollback at all. The
device that exhibited the bug had one entry `VALID` (the reader) and the
other `ABORTED` (the editor, mid-rollback) — the literal fingerprint of a
rollback having fired.

**Rejected fix:** calling `esp_ota_mark_app_valid_cancel_rollback()` from
each app's own `setup()`/entry point (editor and all four reader patch
sets) — technically works, but treats the symptom in five separate places
for something that's really one shared mechanism's problem, and was
flagged as such during review ("parece ser mais algo relativo à estrutura
do Ota/boot").

**Actual fix:** leave `ota_boot::switchTo()` itself completely untouched
— it's upstream reader code (`network/OtaBootSwitch.h`/`.cpp`, native to
each reader, reused rather than duplicated) and its `NEW`-state behavior
is *correct* for what it was written for, a reader's own genuine
self-update (`FirmwareFlasher.cpp`'s `flashFromSdPath()` calls the exact
same function to switch into freshly-downloaded, unverified firmware,
where rollback protection is exactly the right safety net). Our dual-boot
switch is a different operation in disguise — it only ever points at an
*already-flashed, previously-working* sibling slot, never at new code —
so a new `confirmLastOtaSwitch()` helper, added right next to
`switchToOtaApp()` in each of the four `patches/<target>/01_create_otaapps_h.py`
scripts and in `editor/src/main.cpp`'s own `switchToOtaApp()`, re-reads
the otadata entry `switchTo()` just wrote (identified as whichever of the
two has the higher `ota_seq`) and flips just its `ota_state` from `NEW` to
`VALID` (a second erase+rewrite of that one 4KB sector — `ota_state` isn't
covered by the entry's CRC, but flash bits can only be cleared, not set,
without an erase, so a same-sector partial write isn't possible; state
`0`→`2` needs the erase). `flashFromSdPath()`'s own call to `switchTo()`
is never touched by this, so a genuine reader self-update still gets full
rollback protection — only our own manual dual-boot switch skips the
pending-verify window.

All four targets rebuilt clean with this fix. Flashed to the device that
had shown the bug: CrossPoint `v1.5.0` (with the fix) written slot-only to
`app0` (`0x10000`) — bootloader and `otadata` both left untouched,
confirmed byte-identical before/after by direct flash read, same as every
other slot-only update this session. **Not yet confirmed by an actual
physical sleep/wake test** — that's the next thing to verify before
considering this closed. If you're reading this later: check whether that
test happened and passed before assuming this fix works in practice, not
just in the otadata bookkeeping.

## What's verified for the 0.3 patch system

**Fully verified now, including a real `pio run` compile of all three
patched targets** (this was the one open item after the 0.3 session; see
below for the compile blocker that delayed it and how it resolved).

All four patch sets (`patches/crosspoint/`, `patches/crosspoint-1.5.0/`,
`patches/crossink/`, `patches/cpr-vcodex/`) were run against real,
current, tagged upstream checkouts, then built end to end:

- CrossPoint `1.4.1` (`~/github/crosspoint-reader`) — `pio run -e
  gh_release` → **SUCCESS** (5.2MB firmware.bin)
- CrossPoint `v1.5.0` (`~/github/crosspoint-reader`) — `pio run -e
  gh_release` → **SUCCESS** (5.5MB firmware.bin); SDK submodule is
  `freeink-sdk` at this tag, not `open-x4-sdk`
- CrossInk `v1.3.4` (`~/github/CrossInk` — note: HEAD/`main` is v1.5.0-era
  and `03_patch_home_activity_cpp.py` does *not* apply cleanly there,
  meaning CrossInk's `HomeActivity.cpp` menu-building code changed between
  1.3.4 and main; the other four CrossInk scripts still matched current
  `main` when checked) — `pio run -e tiny` → **SUCCESS** (6.35MB
  firmware.bin)
- CPR-vCodex `1.5.0.9-cpr-vcodex` (`~/github/cpr-vcodex`) — `pio run -e
  gh_release` → **SUCCESS** (6.1MB firmware.bin)

Every patch script applied cleanly and the resulting source greps clean
for all the expected symbols (`otaAppCount`, `registerOtaAppName`,
`destHoldsForeignApp`, `SIBLING_APP_PROTECTED`, etc. — see each
`patches/<target>/README.md` for the exact list). These three repos are
cloned locally at `~/github/` for exactly this kind of reference/testing;
they're pristine (unpatched) clones, not modified — each build was done
in a disposable `git worktree` checked out at the target tag, built, and
removed afterward.

**Two local-environment gotchas hit along the way, both fixed, neither a
patch-correctness issue:**

1. The first build attempts (during the 0.3 session) failed with `Error:
   Failed to install Python dependencies into penv` from
   `~/.platformio/platforms/espressif32/builder/penv_setup.py`. Root
   cause, in order: `uv` wasn't installed in the Python venv driving
   `pio` (fixed by installing it there), then a `429 Too Many Requests`
   from `codeload.github.com` fetching a pinned `pioarduino/platformio-core`
   zip — almost certainly this session's own heavy GitHub usage tripping
   a rate limit, not a real dependency problem. It cleared on its own
   after enough time passed; a same-day retry succeeded with no code
   changes needed. **If this recurs, just wait and retry** — it is not
   caused by anything in this repo.
2. `open-x4-sdk` is a git submodule of each reader (not just CPR-vCodex —
   CrossPoint and CrossInk depend on it too). A plain `git worktree add`
   doesn't initialize submodules; each build needed a
   `git submodule update --init --recursive` first. The CI workflows
   already handle this correctly (`submodules: recursive` on the
   `actions/checkout` step) — this only bit local worktree testing.

CrossInk 1.3.4's `platformio.ini` pins pioarduino `platform-espressif32`
release `55.03.37`, which `00_pin_stable_pioarduino_platform.py`'s own
docstring describes as having a real packaging bug
(`framework-arduinoespressif32` fails to resolve) and downgrades to
`55.03.36-1` to work around. Pristine CPR-vCodex 1.5.0.9 pins that same
`55.03.37` release *directly, with no downgrade*, and still built fine —
so that "packaging bug" may in fact have been this same GitHub rate-limit
issue misdiagnosed at the time, not a real defect in `55.03.37` itself.
Left the CrossInk downgrade patch in place regardless (harmless either
way, and it's what that patch set was verified against) rather than
remove it on a guess.

## Open bug: phantom RIGHT button presses

The d-pad's RIGHT button registers presses nobody made. In a menu this is a
minor annoyance (the selection jumps a row on its own); in a text or program
editor it is destructive, because it silently moves the cursor mid-line and
corrupts what is being typed.

Investigated at length in the sibling MicroBASIC project (see that repo's
`docs/DEVELOPMENT_LOG.md` for the full trail); recorded here because the cause
is almost certainly shared -- `editor/` is the same codebase in both, with the
same `platformio.ini` settings.

### What is established

- Confirmed real and reproducible: a `DBG_PRINTF` at the point where
  `processPhysicalButtons()` turns a button into a key event shows
  `btnRight=1` with nobody touching the device.
- **Not** a keyboard or BLE problem: raw BLE HID reports were logged during
  reproductions and are always clean. The corruption people first notice
  ("a space I didn't type") is actually a phantom cursor *move* -- the skipped
  grid cell reads back as a space.
- **Not** one unit's wear: reproduces on a second, different X4.
- **Not** the historical `analogRead()`/dual-framework bug. That one was
  diagnosed and fixed in `microslate-firmware-US-International` (commit
  `1d186ae`) and describes exactly this symptom, but the fix is already
  present here: `InputManager` and `BatteryMonitor` both use the ESP-IDF ADC
  API directly (`adc1_get_raw`), never `analogRead()`. Verified by grepping
  the whole `editor/` tree, and by diffing `lib/InputManager/` against the
  `v2.0.3-usi` tag -- byte-for-byte identical.
- Present in original MicroSlate too, in reduced form: only at boot when
  arriving from an OTA switch, never on sleep/wake within the same slot.
- Absent from CrossPoint, CrossInk and CPR-vCodex.

### Tried and ruled out

- **More debounce.** `InputManager`'s `DEBOUNCE_DELAY` raised from 5ms to
  30ms: no change. To 120ms: *worse*, going from occasional to near-continuous.
  Reverted. That result looked backwards for a long time -- see below, it is
  actually the strongest clue.
- **Suppressing physical-button reads for 1.5s after a BLE connect.** The
  first firings clustered around connection setup, which looked like a smoking
  gun. It kept happening well outside that window. Reverted.

### Current hypothesis: dynamic frequency scaling under the ADC

The buttons are not on individual GPIOs. They are a **resistor ladder on a
shared ADC pin**, classified by voltage band, and RIGHT sits at the extreme
bottom of the range -- anything under 750 counts of 4095. Nothing else on the
ladder is that exposed, so *any* reading biased low classifies as RIGHT.

Comparing this firmware against the readers, which do not show the bug:

| | this firmware | CrossPoint / CPR-vCodex |
|---|---|---|
| framework | `arduino, espidf` | `arduino` |
| power management | `esp_pm_configure()`, 10-80MHz DFS + auto light sleep | none at all |

The readers never call `esp_pm_configure`. `editor/src/main.cpp` enables
dynamic frequency scaling across an 8x range plus automatic light sleep. The
ESP32-C3's SAR ADC is timed off the APB clock, so a conversion taken during or
just after a frequency transition can come out wrong.

This accounts for every observation, including the two that previously made no
sense:

- Absent on the readers -- they have no DFS.
- Present across different units -- it is firmware, not hardware.
- Worse at boot after an OTA switch -- a different clock/PM state path.
- **Why more debounce made it worse.** Debounce filters brief spikes. These
  are not spikes; they are systematically wrong conversions during clock
  transitions, so a longer window integrates more of them instead of
  rejecting them.

### How to confirm

One edit, in `editor/src/main.cpp`'s `pm_config`: set
`min_freq_mhz = max_freq_mhz = 80` and `light_sleep_enable = false`, then use
the device normally. If the phantom presses stop, it is confirmed.

That is a diagnostic, not the fix -- it costs battery life. The proper fix, if
confirmed, is to hold an `esp_pm_lock` across ADC reads so the frequency
cannot move under a conversion, leaving DFS enabled everywhere else.

Not run yet.

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
- `patches/<crosspoint|crosspoint-1.5.0|crossink|cpr-vcodex>/` — patch
  scripts + a `README.md` per target documenting the verified-against tag
  and usage. CrossPoint has two patch sets (`1.4.1` and `v1.5.0`) because
  their `HomeActivity.cpp` menu code diverged enough to need different
  patch-3 anchors — see section 6 above.
- `.github/workflows/build-<target>.yml` — CI: clone reader at a chosen
  tag, apply patches, build both firmwares, merge, publish a release.
  `build-crosspoint.yml` targets `1.4.1`-family tags,
  `build-crosspoint-1.5.0.yml` targets `v1.5.0`-family tags.
- `.github/workflows/build.yml` — "Build MicroWriter Standalone",
  `workflow_dispatch` only, builds *just* `editor/` (no reader, no
  dual-boot) for anyone who wants the writer on its own device. Renamed
  and cleaned up from what used to be a `v*`-tag-triggered workflow with
  stale "MicroSlate"-only branding; no longer fires on tag push
  specifically so it can't race `release.yml` (below) for the same tag.
- `.github/workflows/release.yml` — a more elaborate pre-existing
  pipeline (still `v*`-tag-triggered): builds the editor, attempts a
  dual-boot merge against whatever CrossPoint build typeslate.com hosts
  (not this repo's own `patches/crosspoint/`), and pushes the result to
  an external `typeslate-website` repo using a `WEBSITE_PAT` secret that
  almost certainly isn't configured on *this* repo. Left in place but
  flagged with a comment in the workflow file — this looks like it
  belongs to whoever runs typeslate.com's own publishing flow; decide
  whether to keep, remove, or configure it before relying on it.
- `NOTICE.md` — third-party attribution, plus the fullest account of *why*
  things are structured this way (patches vs. copied source, naming
  decisions, etc.) — read this alongside this file for the complete
  picture.
- `~/github/crosspoint-reader`, `~/github/CrossInk`, `~/github/cpr-vcodex`
  (sibling directories, not inside this repo) — pristine reference clones
  of the three supported readers, used to build and verify the patches
  above. Not part of this repo; recreate with a plain `git clone` if
  they're not present on a new machine.

## Fixes brought back from MicroBASIC

MicroBASIC is this firmware plus a BASIC environment -- it started as a plain
copy of `editor/`, so every file it did not replace is still this one. A long
session of hardware debugging over there turned up eight bugs that all live in
shared code, which means they were all live here too. Verified against this
tree before porting, not assumed.

The direction of travel is worth stating, because it is the opposite of what
MicroBASIC's README originally said: MicroWriter is not upstream of MicroBASIC
in any live sense, but MicroBASIC exercises this code harder (more RAM
pressure, more time on the sync screen) and finds things first. What it finds
comes back here.

### The file browser lost accented letters

`titleToFilename()` walked the title byte by byte and kept only `a-z0-9`. A
title is UTF-8, so an accented letter is two bytes and *neither* is in that
set: naming a note "Ação" produced `aao.txt`. Not a truncation anyone would
notice as a bug -- it just looks like an odd filename.

This matters more here than in MicroBASIC. This is a writing app, and the
titles people give notes in Portuguese are full of accents.

The filename has to be ASCII regardless: SdFat rejects any byte with the high
bit set in a long file name (`lfnLegalChar()`), so a UTF-8 name fails to open
outright. `ascii_fold.h` folds a codepoint to the nearest plain letter, so
"Ação" becomes `acao.txt` -- the accent goes, the letter stays. The *title*
is untouched and stays UTF-8; only the derived filename folds.

### Renaming a note without changing it renamed it anyway

`deriveUniqueFilename()` bumps a name to `_2` when it already exists, and the
file being renamed always exists. So opening the title editor and confirming
without changing anything renamed `note.txt` to `note_2.txt`. It now takes an
`except` argument naming the file that does not count as a collision.

### 16KB of clipboard sitting in .bss

`clipboard` was a static `char[TEXT_BUFFER_SIZE]`. Static `.bss` sits below
the heap, so that was 16KB permanently removed from the largest contiguous
block -- on the device whose BLE connect task needs 20KB *in one piece* and
has already been documented failing to get it. It is allocated on the first
copy now. A copy that cannot allocate simply does not copy, which is a much
better failure than a keyboard that will not reconnect.

### "Save password?" answered itself

The prompt is entered when the connection succeeds -- by something finishing,
not by the user pressing anything. Whatever is in the input queue at that
moment was typed at a *different* screen: the Enter that submitted the
password, or a keyboard auto-repeat of it. Enter on this screen means "yes,
save". So the screen appeared and vanished before it could be read, and the
password was never saved.

The panel also takes ~700ms to display the question, so any key pressed in
that window answers something nobody has read -- and this device generates
spurious button presses (see the phantom RIGHT section), which makes a
dismissable-before-visible prompt the worst possible shape.

Both prompts now go through `openPrompt()`: discard the queued input on entry,
ignore keys for 900ms.

Separately, `usedSavedPassword` and `autoConnectAttempted` are statics that
outlive a sync session and were never reset. Answering "yes" to FORGET_PROMPT
rescans, which left `usedSavedPassword` true from the auto-connect that had
just failed -- and the save prompt only appears when it is false.

### The file page could arrive truncated

`server->send(200, "text/html", FILES_PAGE_HTML)` copies the whole ~8KB page
into a String first: one contiguous allocation, on a device that has been
observed with 7KB as its largest free block. And even when that succeeds,
handing the whole body to one `write()` is not safe -- `WiFiClient::write()`
gives up after a fixed number of retries, and WiFi modem sleep parks the radio
between DTIM beacons, so a body this size can run out of them partway.

Nothing reports that. The HTML that arrived renders fine and what did not
arrive is the `<script>` at the end of the file, so the page appears with no
file list and buttons that do nothing. It goes out one 1440-byte segment at a
time now, each with its own retry budget.

### The sync screen races power management

This firmware runs automatic light sleep with 10-80MHz DFS. A WiFi scan is a
timed sequence of channel hops and should not run on a core that may drop to
10MHz or sleep between them. The clock is pinned at 80MHz with light sleep off
for as long as the sync screen is open, and handed back afterwards -- after
`WiFi.mode(WIFI_OFF)`, not before, because releasing it first tore the radio
down with DFS already back on and did not always finish ("timeout when WiFi
un-init").

**Do not extend this to `WiFi.setSleep(false)`.** That was tried in MicroBASIC
and aborts the firmware:

    wifi:Set ps type: 0
    E wifi:Error! Should enable WiFi modem sleep when both WiFi and
      Bluetooth are enabled!!!!!!
    abort() was called at PC 0x420c849b on core 0

WiFi and BLE share one radio here and coexistence requires the WiFi side to
keep sleeping. Nor can BLE be shut down for the duration -- the keyboard is
BLE and the sync screen needs it.

### "Coração.txt" virava coracaotxt.txt

Primeiro teste no aparelho depois do port, e o achado não é um bug antigo --
é uma armadilha criada hoje. O campo aqui pede um *título*, o ponto não é
caractere de título, então ele sumia e o `.txt` era acrescentado depois:
`coracaotxt.txt`.

O que torna isso previsível em vez de excêntrico: o MicroBASIC ganhou nesta
mesma sessão uma tela quase idêntica onde o campo **é** o nome do arquivo e a
extensão é escolha do usuário. Quem usa os dois vai digitar a extensão aqui
por hábito, e estará certo em esperar que funcione.

`titleToFilename()` agora reconhece um `.txt` no fim do título e o descarta
antes de derivar o nome, então "Coração.txt" e "Coração" dão o mesmo
`coracao.txt`. Insensível a maiúsculas. A guarda é `len > 4`, de propósito:
uma nota genuinamente chamada `txt` continua virando `txt.txt` em vez de ser
engolida. O rótulo do campo também passou a dizer "(.txt is added for you)".

Conferido fora do aparelho com uma tabela de títulos reais -- acentuados, com
extensão, com extensão maiúscula, só pontuação, e o caso limite `txt`.

### Testar no aparelho

Gravado no slot `app1` (0x650000). O que precisa de confirmação:

- [x] Nomear uma nota com acento — "Ação", "Coração" — produz `acao.txt`,
      `coracao.txt`. Antes saía `aao.txt`, comendo a letra. **Confirmado.**
- [ ] Digitar "Coração.txt" no campo de título também dá `coracao.txt`, não
      mais `coracaotxt.txt`.
- [x] Abrir o editor de título e confirmar **sem mudar nada** deixa o nome
      igual. Antes virava `nome_2.txt`. **Confirmado.**
- [x] Copiar e colar continuam funcionando (o clipboard agora é alocado na
      primeira cópia, não mais estático). **Confirmado** — e são 16KB a mais
      de bloco contíguo, que é exatamente o que a task de conexão BLE precisa
      conseguir alocar de uma vez.
- [ ] Sync: a página carrega inteira, com a lista de arquivos.
- [ ] Sync: "Save password?" fica na tela até você responder, e responder
      Enter faz a rede ser reconhecida na próxima vez sem perguntar.
- [ ] Sync: a sessão sobrevive alguns minutos de leitura sem cair.
- [ ] **O A/B do botão fantasma**, que agora não custa nada: enquanto a tela
      de Sync está aberta o clock fica fixo em 80MHz sem light sleep. Se os
      toques espúrios pararem ali e voltarem ao sair, o gerenciamento de
      energia está confirmado como causa.

### Two notes for whoever debugs this next

**`-DRELEASE_BUILD` compiles every `DBG_PRINTF` out.** None of this firmware's
own logging reaches the serial port in a normal build. The ESP-IDF log is a
separate mechanism and is still there, and it is what actually diagnosed the
WiFi problems above -- one line, `total sleep time: 65986964 us / 76529859
us`, explained both a slow page and a truncated one. Comment the flag out in
platformio.ini for a diagnostic build.

**The phantom RIGHT is still RIGHT.** It has been reported as "the menu
scrolls down on its own", which sounds like a different button and is not: in
this UI the d-pad's RIGHT moves a selection *down* and LEFT moves it *up*.
That strengthens the ADC hypothesis rather than complicating it -- RIGHT sits
in the lowest band of the shared resistor ladder, so a reading biased low
lands there specifically, which is what a conversion sampled across a DFS
frequency transition would produce. It is also why more debounce made it
worse: debounce averages more samples into a window where the clock is moving.

And there is now a free A/B for it, costing nothing to run: **the sync screen
already pins the clock at 80MHz with light sleep off.** If the spurious
presses stop while that screen is open and resume on leaving it, the power
management is confirmed as the cause without writing a line of code.

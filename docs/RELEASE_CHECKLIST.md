# SUMI Release Checklist

Steps to take a tested commit and ship it to users via the web flasher.
Treat this as a forcing function: skip a step and the bug list at the
bottom shows what tends to bite.

---

## 0. Preflight

- [ ] `git status` clean (no uncommitted changes that didn't make the
      commit you're shipping)
- [ ] You're on `master` (or whatever release branch you're cutting)
- [ ] `git log -1` shows the commit you intend to ship; note its hash

---

## 1. Bump the version

The version string lives in `platformio.ini`'s `build_flags` for the
`default` and `gh_release` envs:

```
-DSUMI_VERSION=\"<MAJOR>.<MINOR>.<PATCH>[-tag]\"
```

Convention: `default` carries the `-dev` suffix while `gh_release` is
the clean release string. The version string also gets baked into the
artefact filename via the `merge_firmware.py` script (e.g.
`sumi-v0.6.0-ramfix-full.bin`), so changing it here propagates.

- [ ] `default` env version bumped
- [ ] `gh_release` env version bumped
- [ ] `git diff platformio.ini` shows only the version change

---

## 2. Watch for font-ID drift warnings

`src/config.h` has a block of literal `#define <FONT_NAME>_ID <hash>`
constants used as cache keys (`/.sumi/cache/<book>/pages_<fontId>.bin`).
Changing any of them invalidates every existing user's EPUB cache and
forces a full re-parse on next reader open (10-30 s per long book).

Every build runs `scripts/check_font_ids.py` which compares the
literals to IDs derived from the actual font binaries in
`lib/EpdFont/builtinFonts/`. If a font binary has been updated without
re-issuing the macro, the build prints `[font-ids] WARN: drift
detected ...` with each diff.

If you intentionally updated a font binary:

- [ ] Decide whether the cache invalidation is acceptable (it always
      is for a major release; less so for a hotfix).
- [ ] If accepting: `python scripts/check_font_ids.py --regenerate` to
      rewrite the literals in src/config.h, then commit `src/config.h`.
- [ ] Document the cache invalidation in the release notes.
- [ ] Boot once on hardware to confirm no "Font N not found" warnings.

If you didn't update any font binary, the warning means the literals
are stale relative to the binaries — leave them as-is unless you
specifically want to regenerate (audit #54).

---

## 3. Build both environments

```sh
pio run -e gh_release  # 16 MB partition, real hardware target (release variant)
```

- [ ] `pio run -e gh_release` SUCCESS — produces a SUMI-only merged bin
      at `sumi-v<VERSION>-full.bin` plus its `.sha256` sidecar
- [ ] No new warnings vs. the previous release

The post-build `merge_firmware.py` packs bootloader + partition table +
app into a single flat image loadable at flash offset 0 — that's what
the web flasher actually writes. **Never** ship `.pio/build/<env>/firmware.bin`
standalone — it omits the bootloader and bricks devices.

The `env:default` env (no `-e` flag) builds with `SUMI_VERSION=0.6.0-ramfix-dev`
and is used for day-to-day developer flashing. It produces no merged
bin (only `firmware.bin`); don't ship it.

### 3a. Merge the dual-bin (SUMI + SumiBoy)

The gh_release env produces SUMI alone. The flashable release bin must
also include the SumiBoy emulator at offset 0x650000 (the app1 OTA
partition) so the dual-boot dispatch from `SumiBoyRomPicker::launchRom`
has something to switch to. The merge is currently a manual step on
top of the `pio run -e gh_release` output:

```sh
python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32c3 merge_bin \
  -o sumi-v0.6.0-ramfix-full.bin \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0      .pio/build/gh_release/bootloader.bin \
  0x8000   .pio/build/gh_release/partitions.bin \
  0x10000  .pio/build/gh_release/firmware.bin \
  0x650000 /path/to/your/staging/sumiboy-v1.1.0.bin
```

The SumiBoy raw payload (`sumiboy-v1.1.0.bin`, 1,685,392 bytes) is
staged at `firmware/sumiboy-v1.1.0.bin`.
Built from the X4_emulator project against SUMI's `partitions.csv`
(6.25 MB app slots) — do NOT use the standalone X4_emulator
`sumiboy-v*-full.bin` files, those are built against a 3 MB-slot
partition table and won't fit SUMI's layout.

After the merge, verify the dual-app structure:

```sh
python -c "
d=open('sumi-v0.6.0-ramfix-full.bin','rb').read()
print(d[0:1].hex(), d[0x10000:0x10001].hex(), d[0x650000:0x650001].hex())
# Expect: e9 e9 e9
"
```

- [ ] Dual-merged bin has `e9` magic at 0x0, 0x10000, AND 0x650000
- [ ] Size ≈ 8.3 MB (vs ~3.2 MB for SUMI-only)
- [ ] `.sha256` sidecar regenerated to match the dual-merged contents

**TODO (future):** `merge_firmware.py` could be taught to do the dual
merge automatically when `firmware/sumiboy-v*.bin` is present. Currently
it only writes the 0x0/0x8000/0x10000 parts.

---

## 4. Run the host test suite

```sh
cd test/build
ninja          # rebuilds anything stale
cd bin
for t in *.exe; do "./$t" || echo "FAIL: $t"; done
```

- [ ] All 44/44 host tests pass
- [ ] No new "FAIL" lines

---

## 5. Smoke-test on real hardware

Validate end-to-end on a connected device before publishing.

- [ ] Flash the freshly-built dual-bin to a connected device
- [ ] Boot — about screen shows the new version string
- [ ] Open a book; turn through 10+ pages; no `[ERROR]` lines in serial
- [ ] Enter Settings → Bluetooth, pair a HID device; confirm input
- [ ] Long-press Power on Home → enters Sleep cleanly
- [ ] Wake from Sleep → resumes prior screen
- [ ] No new `Guru Meditation` / `Backtrace` / `abort()` in the log

---

## 6. Atomic-write recovery test

The Batch 1 atomic-write protocol survives mid-rotation power loss in
theory; this is the empirical confirmation.

Manual:

1. On a real device, edit a setting and let it persist
2. Reboot — confirm setting persisted
3. Pull the device's USB while a save is in flight (or otherwise
   force a brown-out mid-write)
4. Reboot. Boot logs should show `[SD] recover: ... — promoted .bak`
   or `discarded stale .tmp` on the path that was mid-rotation
5. Confirm the canonical file is intact (settings still load with the
   pre-write state OR the post-write state, never empty)

- [ ] At least one recovery path observed in logs since the last release
- [ ] No "[SD] recover: ... promote of .bak FAILED" lines

The eight `.bin` formats covered by atomic writes are in
[BOOT_FLOW.md](BOOT_FLOW.md) under "Crash-safe state files".

---

## 7. Verify the SHA256 sidecar

`merge_firmware.py` automatically writes a sidecar next to the merged
firmware (`sumi-v<VERSION>-full.bin.sha256`) at the end of step 3's
gh_release build. The web flasher uses it to verify the download
before flashing — a truncated download bricks devices.

- [ ] `.sha256` file present next to the `.bin` (build output prints the
      digest in the banner)
- [ ] `sha256sum -c <file>.sha256` returns OK

If the sidecar is missing, the gh_release build didn't run
merge_firmware all the way through — go back to step 3.

---

## 8. Commit the build artefacts

The convention in this repo is two commits per release:

```
audit batch N — <one-line summary>     ← code commit
build: flashable artefacts at <hash>    ← .bin + .sha256 commit
```

Keeping the artefact in its own commit means the code commit's diff
isn't dominated by binary noise, and reverting the binary doesn't
revert the source.

- [ ] Code commit landed on `master` with the audit/feature description
- [ ] Build commit landed with `flashable artefacts at <code-hash>`
- [ ] If applicable, the mirror branch has a parallel pair of
      commits with `— mirror <hash>` suffix

---

## 9. Smoke-test the artefact on real hardware

The web flasher path is the canonical user install. Verify it works
before announcing.

- [ ] Web flasher loads the new `.bin` successfully (SHA256 verifies)
- [ ] Device boots to the expected version on the about screen
- [ ] One round-trip through the home screen + reader + settings cycle
      with no [WARN] or [ERROR] lines on serial

---

## 10. Tag and announce

- [ ] `git tag v<VERSION>` (annotated tags preferred: `git tag -a -m "..."`)
- [ ] Push the tag if you push tags: `git push --tags`
- [ ] Update the version pin in any release notes / changelog
- [ ] Announce per the usual channel (GitHub release, Discord, etc.)

---

## Bugs the checklist exists to prevent

- **Bricked devices via `firmware.bin` instead of merged image.** The
  unmerged binary lacks the bootloader. The web flasher writes it at
  offset 0, the bootloader is gone, the device won't even reach the
  reset vector again. Recovery requires a USB serial flash via esptool
  with a known-good full image. Step 3 calls this out explicitly.

- **Font IDs drift from binaries.** Update a font, forget to regenerate
  the IDs, ship: every device that auto-updates renders "?" glyphs
  for every text drawn through the affected font. Step 2 has the gate.

- **Atomic-write recovery silently broken.** A pre-rotation `.bak` left
  behind never gets promoted because the recovery scan misses the
  directory. Step 6 forces an empirical check.

- **SHA256-less artefact.** Network mirror corrupts a single byte; web
  flasher writes garbage; brick. `merge_firmware.py` now writes the
  sidecar automatically (audit #52 fix); step 7 just verifies it.

- **Real-hardware smoke skipped.** A regression that's invisible in
  the unit tests slips through. Step 5 catches it on a real device
  before users see it.

---

## Cross-references

- [AUDIT_PLAN.md](AUDIT_PLAN.md) — outstanding items still open
- [BOOT_FLOW.md](BOOT_FLOW.md) — what `setup()` does and why
- [ATOMIC_WRITE_DESIGN.md](ATOMIC_WRITE_DESIGN.md) — 3-rename rotation
  details + the 8-state recovery rules step 6 is verifying

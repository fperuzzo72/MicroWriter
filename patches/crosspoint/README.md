# CrossPoint patch set

Adds MicroWriter dual-boot support to a
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
checkout: the OTA "call to the editor" (`OtaApps.h` +
`HomeActivity.h`/`.cpp` menu wiring) and a self-update guard that keeps
CrossPoint's own firmware-update feature from overwriting the editor's OTA
slot.

**Verified against:** CrossPoint `1.4.1` — all five patches apply, the
resulting source greps clean (`otaAppCount`, `detectOtaApps`,
`switchToOtaApp`, `registerOtaAppName`, `SIBLING_APP_PROTECTED`,
`destHoldsForeignApp` all present), and `pio run -e gh_release` builds
successfully (remember `git submodule update --init --recursive` first —
CrossPoint depends on `open-x4-sdk` as a submodule).

CrossPoint's latest stable is now `v1.5.0`, not `1.4.1` — this patch set
is kept as-is for anyone pinned to `1.4.1`, but new installs should
prefer `patches/crosspoint-1.5.0/`. `1.5.0` reworked
`HomeActivity.cpp`'s menu-activation code enough that patch 3 here no
longer applies to it (see `patches/crosspoint-1.5.0/README.md` for what
changed).

`01_create_otaapps_h.py` uses `ota_boot::switchTo()` (from CrossPoint's own
`src/network/OtaBootSwitch.h`) rather than calling
`esp_ota_set_boot_partition()` directly — the latter fails on X4-class
hardware with a bogus efuse-blk-rev verification error. This differs from
this patch set's own history: it started as Python embedded directly in
`.github/workflows/build-crosspoint.yml` (see git history), which *did*
call `esp_ota_set_boot_partition()` directly and would have hit that same
bug — fixed here to match the approach `patches/crossink/` and `editor/`
already use.

Each script fails loudly (`assert old in src`) rather than silently
applying a partial patch.

## Usage

```bash
git clone --branch 1.4.1 https://github.com/crosspoint-reader/crosspoint-reader.git crosspoint
for f in patches/crosspoint/*.py; do python3 "$f"; done
cd crosspoint
pio run -e gh_release   # see crosspoint/platformio.ini for other envs
```

Then merge `crosspoint/.pio/build/gh_release/firmware.bin` with the
editor's own build into one flashable image — see the root `README.md`.

See `.github/workflows/build-crosspoint.yml` for the CI version of this
same flow (clones a chosen tag, applies these patches, builds both
firmwares, merges, and publishes a release).

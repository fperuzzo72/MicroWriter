# CrossInk patch set

Adds MicroWriter dual-boot support to a [CrossInk](https://github.com/uxjulia/CrossInk)
checkout: the OTA "call to the editor" (`OtaApps.h` +
`HomeActivity.h`/`.cpp` menu wiring) and a self-update guard that keeps
CrossInk's own firmware-update feature from overwriting the editor's OTA
slot.

**Verified against:** CrossInk `v1.3.4` — all five patches apply, the
resulting source greps clean (`otaAppCount`, `SIBLING_APP_PROTECTED`,
`destHoldsForeignApp` all present), and `pio run -e tiny` builds
successfully (remember `git submodule update --init --recursive` first —
CrossInk depends on `open-x4-sdk` as a submodule). Newer CrossInk versions
may need the text anchors in `03_patch_home_activity_cpp.py` re-checked —
that one failed against the current `main` branch (v1.5.0-era) when this
was last tested, meaning `HomeActivity.cpp`'s menu-building code has
changed since 1.3.4. The other four scripts' anchors (OtaApps.h creation,
`HomeActivity.h`, `main.cpp`, and the self-update guard) still matched
current `main` at time of writing.

Each script fails loudly (`assert old in src`) rather than silently
applying a partial patch, so a version mismatch shows up immediately as a
clear error naming which anchor didn't match.

## Usage

```bash
git clone --branch v1.3.4 https://github.com/uxjulia/CrossInk.git crossink
for f in patches/crossink/*.py; do python3 "$f"; done
cd crossink
pio run -e tiny   # or teensy / xlarge — see crossink/platformio.ini
```

Then merge `crossink/.pio/build/<variant>/firmware.bin` with the editor's
own build into one flashable image — see the root `README.md`.

See `.github/workflows/build-crossink.yml` for the CI version of this same
flow (clones a chosen tag, applies these patches, builds both firmwares,
merges, and publishes a release).

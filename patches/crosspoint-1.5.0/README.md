# CrossPoint patch set (v1.5.0)

Same purpose as `patches/crosspoint/` (the OTA "call to the editor" +
self-update guard for a [CrossPoint
Reader](https://github.com/crosspoint-reader/crosspoint-reader) checkout),
kept as a **separate** patch set because CrossPoint's `1.4.1` → `v1.5.0`
jump changed `HomeActivity.cpp`'s menu-handling structure enough that
`patches/crosspoint/`'s patch 3 no longer applies — see below. `1.4.1`'s
own patch set is untouched and still targets `1.4.1`.

**Verified against:** CrossPoint `v1.5.0` — all five patches apply, the
resulting source greps clean (same checks as `patches/crosspoint/`), and
both `pio run` (env `default`) and `pio run -e gh_release` build
successfully (remember `git submodule update --init --recursive` first —
as of `v1.5.0` CrossPoint's SDK submodule is named `freeink-sdk`, renamed
from `open-x4-sdk` used at `1.4.1`).

## What changed vs. the 1.4.1 patch set

Only patch 3 (`03_patch_home_activity_cpp.py`) differs. Patches 1, 2, 4,
and 5 are byte-identical to `patches/crosspoint/` — 1.5.0 didn't touch
`OtaApps.h`'s insertion points, `HomeActivity.h`'s fields, `main.cpp`'s
registration call, or `FirmwareFlasher.cpp`'s self-update guard anchor.

1.5.0 refactored `HomeActivity::loop()`: the menu-activation
`switch (indexToMenuItem(...))` that used to sit directly inside
`if (mappedInput.wasReleased(Confirm)) { ... }` was extracted into a
lambda (`activateSelection`), now shared by the new touch/swipe handlers
1.5.0 also added. The switch itself and all its `case`s are unchanged —
only the brace nesting around it changed (the lambda closes with `};`
right after the switch, instead of the four-level
if/switch/function nesting `1.4.1` had). Patch 3 was rewritten to anchor
on the new, shallower closing pattern; the OTA-handling logic inserted
into the `default:` case is otherwise identical to the 1.4.1 patch.

Each script fails loudly (`assert old in src`) rather than silently
applying a partial patch.

## Usage

```bash
git clone --branch v1.5.0 --recurse-submodules \
  https://github.com/crosspoint-reader/crosspoint-reader.git crosspoint
for f in patches/crosspoint-1.5.0/*.py; do python3 "$f"; done
cd crosspoint
pio run -e gh_release   # see crosspoint/platformio.ini for other envs
```

Then merge `crosspoint/.pio/build/gh_release/firmware.bin` with the
editor's own build into one flashable image — see the root `README.md`.

See `.github/workflows/build-crosspoint-1.5.0.yml` for the CI version of
this same flow.

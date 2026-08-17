# CPR-vCodex patch set

Adds MicroWriter dual-boot support to a
[CPR-vCodex](https://github.com/franssjz/cpr-vcodex) checkout: a
"MicroSlate" shortcut (Home menu, same `ShortcutRegistry` mechanism as
every other CPR-vCodex shortcut) that switches to the editor, and a
self-update guard that keeps CPR-vCodex's own firmware-update feature
from overwriting the editor's OTA slot.

**Verified against:** CPR-vCodex `1.5.0.9-cpr-vcodex` — all eight patches
apply and the resulting source greps clean (`ShortcutId::MicroSlate`,
`microslateShortcut`, `switchToFirstOtaApp` in both `AppsActivity.cpp` and
`HomeActivity.cpp`, `registerOtaAppName`, the `STR_MICROSLATE` i18n key,
`SIBLING_APP_PROTECTED`, `destHoldsForeignApp` all present).

Unlike `patches/crosspoint/` and `patches/crossink/` — which patch a raw
`switch`/`case` directly in `HomeActivity.cpp` — CPR-vCodex routes every
shortcut through a `ShortcutRegistry`/`ShortcutDefinition` abstraction
(`src/util/ShortcutRegistry.h`) with its own settings fields
(`src/CrossPointSettings.h`) and i18n-driven display strings
(`lib/I18n/translations/english.yaml`). That's why this target has 8
patch scripts instead of 4-5, and why they're not shared with the other
two targets despite CPR-vCodex itself being a CrossPoint fork.

Each script fails loudly (`assert old in src`) rather than silently
applying a partial patch.

## Usage

```bash
git clone --branch 1.5.0.9-cpr-vcodex https://github.com/franssjz/cpr-vcodex.git cpr-vcodex
for f in patches/cpr-vcodex/*.py; do python3 "$f"; done
cd cpr-vcodex
pio run -e default   # or gh_release / slim — see cpr-vcodex/platformio.ini
```

Then merge `cpr-vcodex/.pio/build/<env>/firmware.bin` with the editor's
own build into one flashable image — see the root `README.md`.

See `.github/workflows/build-cpr-vcodex.yml` for the CI version of this
same flow.

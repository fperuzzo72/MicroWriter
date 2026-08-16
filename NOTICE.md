# Third-party notices

MicroWriter X4 is not a fork of any of the projects below — no shared git
history, no upstream remote. Its source tree started as a copy of SUMI's
files, and further files were copied in from MicroSlate as the writer
plugin was ported. This file preserves the original copyright notices as
required by the MIT license on each source.

## SUMI — base firmware (state machine, plugin system, BLE HID host,
## rendering stack, EPUB engine, GameBoy emulator)

Source tree copied from <https://github.com/psychoplath9450/SUMI>.

```
MIT License

Copyright (c) 2025 Dave Allie (CrossPoint Reader / Papyrix)
Copyright (c) 2025-2026 SUMI Contributors

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

SUMI itself is built on **Papyrix** (<https://github.com/bigbag/papyrix-reader>)
by Pavel Liashkov, which is a fork of **CrossPoint Reader**
(<https://github.com/crosspoint-reader/crosspoint-reader>) by Dave Allie —
both MIT licensed, covered by the same notice above. The EPUB engine, page
rendering, and state-machine core in this codebase trace back to that
lineage.

## MicroSlate — writer plugin (text editor, dead-key layout, note storage)

`text_editor.cpp/.h`, `ui_renderer.cpp`, `file_manager.cpp`, and
`dead_keys.h` were ported from
<https://github.com/Josh-writes/microslate-firmware> (used via
<https://github.com/fperuzzo72/microslate-firmware-US-International>, a
personal US-International-layout fork) into a new SUMI plugin. MicroSlate's
own BLE keyboard host (`ble_keyboard.cpp`) and input-mapping layer
(`input_handler.cpp`) were **not** ported — this firmware uses SUMI's own
BLE HID host and plugin input interface instead.

```
MIT License

Copyright (c) 2026 Joshua Hinton

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

## CrossPoint / CrossInk — design reference only, no code copied

The home screen layout (last-read book on top, menu list directly below,
no decorative art) follows the arrangement used by CrossPoint Reader's
built-in `CLASSIC` home theme and carried into its **CrossInk** fork
(<https://github.com/uxjulia/CrossInk>). No source from either project is
included here — SUMI's state-machine architecture is incompatible with
CrossPoint's Activity/ActivityManager pattern, so the layout was
reimplemented from scratch inside SUMI's own `HomeState`, matching the
visual arrangement only.

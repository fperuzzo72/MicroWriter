# Third-party notices

MicroWriter X4 is not a fork of any of the projects below — no shared git
history, no upstream remote. Its source tree started as a copy of
CPR-vCodex's files (replacing an earlier SUMI-based starting point — see
git history for that first attempt and why it was dropped). This file
preserves the original copyright notices as required by the MIT license on
each source.

## CPR-vCodex — base firmware (Activity architecture, reading engine,
## rendering stack, reading statistics, dictionary, flashcards, KOReader
## sync, themes)

Source tree copied from <https://github.com/franssjz/cpr-vcodex>.

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

## MicroSlate — writer plugin/integration (planned, not yet ported)

Not in the tree yet. When the writer integration lands, `text_editor.cpp/.h`,
`ui_renderer.cpp`, `file_manager.cpp`, and `dead_keys.h` will be ported from
<https://github.com/Josh-writes/microslate-firmware> (used via
<https://github.com/fperuzzo72/microslate-firmware-US-International>, a
personal US-International-layout fork). This section will be filled in with
MicroSlate's MIT license text once that code actually lands.

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

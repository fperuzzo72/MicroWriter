"""Runtime monkey-patch for SCons's TempFileMunge on Windows.

PlatformIO's vendored SCons emits a cleanup command like:

    cmd.exe /C "del C:/Users/.../tmpXXXX.tmp"

The arg to `del` is the native tempfile path. When the project lives
under `C:/Users/...` (as this one does), CMD.EXE's `del` builtin parses
`/Users` as a switch and errors out with:

    Invalid switch - "Users".

Every compile then shows up as `Error 1` even though the compiler ran
successfully. Fix: backslash the arg to `del` so CMD.EXE doesn't see any
forward slashes. gcc's response-file parser accepts both separators, so
the middle element (the @tempfile arg to gcc) is left untouched.

We monkey-patch the TempFileMunge class at runtime (not the file on
disk) because SCons has already imported it by the time pre-scripts run.
This means the fix survives `.pio/packages` re-downloads without any
on-disk edits to the PIO packages.
"""

from __future__ import annotations

import os
import sys

Import("env")  # type: ignore[name-defined]  # injected by SCons


def _patch_tempfilemunge() -> bool:
    if sys.platform != "win32":
        return False
    try:
        import SCons.Platform as _platform  # type: ignore[import-not-found]
    except ImportError:
        return False

    tfm = getattr(_platform, "TempFileMunge", None)
    if tfm is None:
        return False
    if getattr(tfm, "_sumi_patched", False):
        return False  # already patched — e.g. second env in the same run

    orig_call = tfm.__call__

    def patched_call(self, target, source, env, for_signature):
        result = orig_call(self, target, source, env, for_signature)
        # Result is either a string (the original self.cmd in for_signature
        # mode) or a list [cmd[0], middle, native_tmp]. Only the list form
        # contains the del arg we need to fix.
        if isinstance(result, list) and len(result) >= 3:
            last = result[-1]
            if isinstance(last, str) and "/" in last:
                result = list(result)
                result[-1] = last.replace("/", "\\")
        return result

    tfm.__call__ = patched_call
    tfm._sumi_patched = True
    return True


if _patch_tempfilemunge():
    print("[patch_scons] TempFileMunge del-arg patched for Windows paths")

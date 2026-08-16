# Atomic Write Design — SDCardManager helper

**Status:** landed (Batch 1 + post-Batch-9 audit follow-up).
**Drives:** [AUDIT_PLAN.md](AUDIT_PLAN.md) Batch 1, plus the plugin-save
sweep that the original auditor didn't reach.

**Coverage as of audit pass:**
- `/.sumi/*` system files: settings.bin, library.bin, recent.bin,
  achievements.bin, reading_stats.bin, global_bookmarks.bin,
  progress.bin (per-book), bookmarks.txt (per-book), transition.bin
- `/.sumi/*` plugin files: chess_save / chess_settings / chess_stats,
  sudoku_save / sudoku_settings / sudoku_stats, flashcards_stats,
  flashcards_decks, game_2048
- `/notes/*.txt` user-editable text notes (recovery scan extended to
  cover this dir at level 0)

Files NOT yet under atomic-write (queued follow-ups): GB savestates
in gb_emulator.h (single user, can re-save), TodoList /data/todo.txt
(small file, low edit frequency), cheats.h cheat list, Maps file
imports (read-side only). The pattern is mechanical to extend.

## Why

`LibraryIndex::updateEntry`, `Achievements::save`, `Bookmarks::toggle`,
`ProgressManager::save`, plus 21 other SD writers all use the
truncate-then-overwrite pattern:

```
openFileForWrite(path);   // O_CREAT | O_TRUNC — file is now zero bytes
file.write(payload);      // power loss here = canonical file is empty
file.close();
```

Power loss between `O_TRUNC` and `close()` produces a structurally valid but
content-empty file. Loaders treat it as "default state" — the user loses their
reading progress, achievements, bookmarks, etc. **Survey found 25 such writers**
(see [AUDIT_PLAN.md](AUDIT_PLAN.md) Batch 1).

The fix is one helper applied 25 times. This doc nails its semantics so the
implementation has nothing to invent.

## API

Three public methods on `SDCardManager` plus one boot-time recovery hook.

```cpp
namespace sumi {
class SDCardManager {
 public:
  // ─── existing API unchanged ───────────────────────────────────────

  // ─── new: atomic write protocol ───────────────────────────────────

  /// Open `<path>.tmp` for writing. `path` is the canonical destination.
  /// `moduleName` is for log lines.
  ///
  /// Caller writes payload (with CRC32 trailer if file format gains
  /// one in Batch 8), then calls atomicCommit to publish, or
  /// atomicAbort to discard.
  ///
  /// @return false if .tmp couldn't be opened.
  bool atomicOpenWrite(const char* moduleName,
                       const char* path,
                       FsFile& outFile);

  /// Promote the .tmp to canonical path:
  ///   1. file.sync(); file.close();
  ///   2. if exists(path): rename(path, path + ".bak")
  ///   3. rename(path + ".tmp", path)
  ///   4. remove(path + ".bak")    // best effort
  ///
  /// On any rename failure, the previous canonical (if any) is restored.
  /// @return true on full success.
  bool atomicCommit(FsFile& file, const char* path);

  /// Discard the .tmp and leave the canonical untouched.
  ///   1. file.close();
  ///   2. remove(path + ".tmp")
  void atomicAbort(FsFile& file, const char* path);

  /// Boot-time recovery. Walks /.sumi/ (and any other registered
  /// directory) looking for orphan .tmp / .bak files. Promotes .bak
  /// when canonical is missing; otherwise cleans up.
  ///
  /// Called once from `Storage::begin()`. Cheap (one directory scan).
  void recoverAtomicWrites();
};
}
```

The interface is deliberately narrow. Callers see three method calls and never
touch the `.tmp` / `.bak` paths directly.

## The 3-rename rotation

Each commit performs renames in a strict order so that a power loss at every
intermediate point is recoverable.

```
Steady state:                         path  exists, no .tmp, no .bak
                                          │
                                          ▼
Step 0 — atomicOpenWrite creates .tmp:    path, .tmp
Step 1 — file.sync(); file.close():        path, .tmp (durable on disk)
Step 2 — rename(path, .bak):                     .tmp, .bak     ← canonical "missing" briefly
Step 3 — rename(.tmp, path):              path,         .bak
Step 4 — remove(.bak):                    path
                                          │
                                          ▼
Steady state.
```

Step 2's "canonical briefly missing" is what
[recoverAtomicWrites](#recovery-state-table) detects and repairs at next boot.

### Why three names, not two?

A two-name protocol (`tmp → canonical`) requires `rename` to overwrite, which
SdFat's POSIX layer does NOT support — it fails on rename-over. The
`LibraryIndex` audit finding (#31) is exactly this:

```cpp
SdMan.remove(INDEX_PATH);                          // ← power loss here = no library
SdMan.rename("/.sumi/library.tmp", INDEX_PATH);
```

The 3-rename rotation routes through `.bak` so we never have a window with
zero copies on disk. At every intermediate state, **at least one of {canonical,
.bak, .tmp} contains valid data** for the recovery pass to find.

## Recovery state table

`recoverAtomicWrites()` walks the storage directory once at boot. For each
canonical path it might own, it inspects three booleans:

| canonical | `.tmp` | `.bak` | Action                                       | Why                                                       |
|-----------|--------|--------|----------------------------------------------|-----------------------------------------------------------|
| ✅        | ✅      | ✅     | remove `.tmp`, remove `.bak`                 | Commit succeeded; we crashed during cleanup.              |
| ✅        | ✅      | ❌     | remove `.tmp`                                | Crash mid-write of next attempt; canonical untouched.     |
| ✅        | ❌      | ✅     | remove `.bak`                                | Commit succeeded; we crashed before bak removal.          |
| ✅        | ❌      | ❌     | no-op                                        | Steady state.                                             |
| ❌        | ✅      | ✅     | rename `.bak → canonical`, remove `.tmp`     | Crashed between `rename(canonical, .bak)` and `rename(.tmp, canonical)`. **Promote bak**. |
| ❌        | ✅      | ❌     | remove `.tmp`; canonical stays absent        | Crash before any rotation; canonical was already missing. |
| ❌        | ❌      | ✅     | rename `.bak → canonical`                    | Crashed mid-cleanup after rotation completed.             |
| ❌        | ❌      | ❌     | no-op                                        | First boot or all files cleared.                          |

All eight states are reachable. The promotion rule (`.bak → canonical` when
canonical missing) is the only data-restoring action; everything else is
bookkeeping.

## Sync ordering

In order, on `atomicCommit`:

1. `file.sync()` — flushes payload + FAT chain to SD media.
2. `file.close()` — releases the FsFile handle.
3. `SdMan.exists(path)` test, `SdMan.rename(path, path + ".bak")` if so.
4. `SdMan.rename(path + ".tmp", path)`.
5. `SdMan.remove(path + ".bak")`.

If SdFat's `vol.cacheClear()` or equivalent is available, call it after step 5
to push the directory entries to media. Otherwise rely on idle flushes — the
recovery pass tolerates a `.bak` that survived an undetected good commit.

## Failure modes

For each step, what happens if it fails:

| Step | Failure mode | Recovery |
|------|--------------|----------|
| 0 (open .tmp) | SD full / read-only / path bogus | `atomicOpenWrite` returns false; caller sees no harm. |
| 1 (sync+close) | Media error | Caller treats as failure, calls `atomicAbort` to remove the partial .tmp. |
| 2 (rename canonical → .bak) | SdFat reports failure | Don't rename .tmp. canonical stays as the previous version; .tmp orphan cleaned by recovery. Caller is told the commit failed. |
| 3 (rename .tmp → canonical) | SdFat reports failure | Restore: `rename(.bak, canonical)` to roll back. .tmp orphan cleaned by recovery. Caller is told the commit failed. |
| 4 (remove .bak) | Best-effort; ignore failure | Recovery removes the orphan on next boot. |

All caller-visible failures land before the on-disk state has been corrupted.
The worst-observable outcome is a `.bak` or `.tmp` that lingers until next
boot.

## Out-of-scope (intentional)

- **Append-mostly writers** keep `O_APPEND`. `LookupHistory::addWord` is the
  example — its safety property is "unwritten bytes are tolerable on crash"
  rather than "either old or new file." Atomic-write would impose a full
  rewrite cost the use case doesn't need.
- **CRC32 verification** lives in Batch 8 of the audit plan, not here. The
  helper is format-agnostic; it doesn't open or interpret payloads. Callers
  embed CRC trailers if they want them.
- **Cross-file transactions** (e.g. settings.bin + library.bin updated
  consistently) are not supported. Each file is independent. SUMI doesn't
  currently need the cross-file invariant; if it does later, a separate
  journal mechanism would be needed.
- **Concurrent atomic writes to the same path** are caller-prevented. If
  `atomicOpenWrite("FOO", "/x", ...)` is called twice concurrently, the
  second call sees the first's `.tmp` and fails to open. Acceptable — every
  caller is single-threaded by SUMI's task-ownership model
  ([CONCURRENCY.md](CONCURRENCY.md)).

## Test fixture

A power-cut simulator runs as part of the host-side test harness. It writes
two paths in `test/atomicwrite/` and at strategic points calls
`abort()` (in tests, not firmware) to simulate power loss. Then the test
re-mounts and calls `recoverAtomicWrites()`. Every one of the 8 states above
is exercised. Lives at `test/unit/atomicwrite/AtomicWriteRecoveryTest.cpp`
when Batch 1 lands.

## Migration / call-site changes

Each existing call site changes from:

```cpp
FsFile f;
SdMan.openFileForWrite("LIB", "/library.bin", f);
serialization::writePod(f, header);
// ... write entries ...
f.close();
```

to:

```cpp
FsFile f;
if (!SdMan.atomicOpenWrite("LIB", "/library.bin", f)) {
  Serial.printf("[LIB] atomicOpenWrite failed\n");
  return false;
}
serialization::writePod(f, header);
// ... write entries ...
if (!SdMan.atomicCommit(f, "/library.bin")) {
  Serial.printf("[LIB] atomicCommit failed\n");
  return false;
}
return true;
```

Three lines per call site. The helper handles all the rotation.

## Recovery scope

`recoverAtomicWrites()` walks `/.sumi/` and one further level of subdirs
because SUMI's per-book caches live at `/.sumi/cache/<book>/*` (per
`config.h`, `SUMI_CACHE_DIR = SUMI_DIR "/cache"`):

```
level 0: /.sumi             — recovers settings.bin, library.bin, etc.
level 1: /.sumi/<sub>       — e.g. cache, transition
level 2: /.sumi/<sub>/<sub> — per-book cache contents
                              (bookmarks.txt, progress.bin, lookups.txt)
```

The scan stops at level 2 so a future deep tree under `/.sumi/` doesn't
make boot expensive. Per-level caps are tight (8 immediate subdirs,
32 second-level subdirs, 32 orphan candidates per directory).

A few user-facing directories outside `/.sumi/` host atomic-write data
and get a narrower sweep:

- `/notes`  (depth 0) — Notes plugin user text
- `/data`   (depth 0) — TodoList plugin (`/data/todo.txt`)
- `/custom` (depth 0+1) — Lua plugin sandbox (`/custom/<plugin>_data/*`)
- `/games`  (depth 0+1) — GB emulator save state / SRAM / cheats
                          (`/games/saves/*`, `/games/cheats/*`)

Each is gated on `sd.exists()` so absent directories cost a single
stat call. Other plugin-private save paths still need to either place
state under `/.sumi/<plugin-name>/` or extend the scan above.

The scan uses no recursion past level 2, no heap allocation (stack
buffers throughout), and bounds total work at `1 + 8 + 8*32 = 265`
directory enumerations. On a typical user's SD (one cache subdir,
maybe 5 cached books) it touches ~7 directories total.

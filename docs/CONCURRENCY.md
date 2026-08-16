# SUMI Concurrency Model

**Status:** describes current behavior + invariants. Verifies and ossifies the
single-task-ownership convention that the audit revealed was undocumented.
**Drives:** [AUDIT_PLAN.md](AUDIT_PLAN.md) Batch 4.

**Batch 4 status (landed):** all the runtime guards described below are now
in place — see in-source comments at each call site. The ownership convention
itself is unchanged; the guards just catch a future contributor that quietly
breaks it.

> **The rule.** SUMI runs four FreeRTOS tasks but every shared mutable state
> is owned by **exactly one task at a time**. Hand-offs between tasks happen
> via well-known synchronization primitives (notify, semaphore, mutex). Any
> shared state that doesn't fit this model is a bug. The matrix in
> [Section B](#b--shared-state-matrix) is the registry of every shared
> resource and how ownership transfers.

---

## A — Tasks

| Name             | Stack | Priority | Owner / lifecycle                                | Loops on                                              |
| ---------------- | ----- | -------- | ------------------------------------------------ | ----------------------------------------------------- |
| `loopTask`       | 32 KB | 1        | Arduino main task (runs `setup()` then `loop()`) | input → state machine → render → idle                 |
| `eink_refresh`   | 4 KB  | 1        | Spawned by `EInkDisplay::begin()`; lives forever | `ulTaskNotifyTake` → `refreshDisplay` → RED-RAM sync  |
| `cacheTask`      | 8 KB  | 1        | `BackgroundTask` member of `ReaderState`. Spawned in `enterBackgroundCaching()`, joined in `stopBackgroundCaching()`. | parses next chapters into `pageCache_` |
| NimBLE host      | 4 KB  | 5        | Created by NimBLE-Arduino library                | network packets, GATT callbacks                       |
| NimBLE controller| (IDF) | 23       | ESP-IDF BT controller; below FreeRTOS scheduler  | radio                                                 |

ESP32-C3 is **single-core**. All FreeRTOS tasks run on the same core, scheduled
round-robin within priority. NimBLE host preempts main; main preempts the
two priority-1 background tasks cooperatively (they're equal priority, so
yields are explicit).

### Priorities matter for one thing

The eink_refresh task is priority 1 (same as main). Main's `displayBuffer()`
posts a notification then yields by waiting on `refreshDoneSemaphore_`. The
two tasks effectively serialize. The audit's #32 finding (`vTaskDelete`
mid-SPI) is benign because the destructor never fires — `EInkDisplay` is a
process-wide singleton. Batch 4 still added `waitForRefresh()` before
`vTaskDelete(refreshTaskHandle_)` in the destructor for defense-in-depth
against any future code path (host-build mocks, screen-off-on-power
routines) that does construct + destruct an `EInkDisplay`.

NimBLE host runs at priority 5 — it preempts everything else. That's why
`BleFileTransfer`'s callbacks need their own ownership story (see B below).

---

## B — Shared-state matrix

Every field or object reachable from more than one task lives here. Anything
that should be in this table but isn't is a latent bug.

### Renderer + framebuffer

| Field / object                     | Owner-task | Mutators                                       | Readers           | Sync mechanism |
|-----------------------------------|-----------|------------------------------------------------|-------------------|----------------|
| `GfxRenderer::wordWidthCache`     | exclusive | `loopTask` (UI), `cacheTask` (parse measure)   | same             | Convention enforced by Batch 4. `ReaderState` publishes `cacheTask_.getHandle()` into `GfxRenderer::s_cacheTaskHandle_` between start/stop. The mutating public methods (`drawText`, `getTextWidth`, `drawImage`, `clearScreen`, `displayBuffer`, `displayWindow`) call `warnIfNonOwner()`, which logs (rate-limited) when invoked from a task other than the registered cacheTask while the handle is set — main task forgetting to `stopBackgroundCaching()` is the bug pattern caught here. |
| `GfxRenderer::framebuffer`        | exclusive | `loopTask` (drawing), `eink_refresh` (RED-RAM sync) | both        | `EInkDisplay::waitForRefresh()` at the top of every framebuffer-mutating method (clearScreen, drawImage, setFramebuffer, drawPixel, etc.). Verified at `lib/EInkDisplay/src/EInkDisplay.cpp:586-705`. |
| `EInkDisplay::pendingJob_`        | exclusive | `loopTask` writes, `eink_refresh` reads        | same             | `xTaskNotify*` (FreeRTOS happens-before). `pendingJob_` is set BEFORE `xTaskNotifyGive`; bg reads AFTER `ulTaskNotifyTake`. |
| `EInkDisplay::_x3RedRamSynced`    | bg-only   | `eink_refresh` writes, `loopTask` reads later  | both             | Currently no fence. Single-core in-order execution makes it benign, but UB on a multi-core port. **TODO note in CONCURRENCY** but no immediate fix. |
| `EInkDisplay::fastRefreshCount_`  | exclusive | `loopTask` only                                | `loopTask` only  | None needed.   |

### PageCache

| Field / object                     | Owner-task | Mutators                                       | Readers           | Sync mechanism |
|-----------------------------------|-----------|------------------------------------------------|-------------------|----------------|
| `pageCache_` (the cache file)     | exclusive | `cacheTask` (extend), `loopTask` (loadPage)    | same             | Convention: `loopTask` calls `stopBackgroundCaching()` before `loadPage()`. Verified at `ReaderState.cpp:1083` followed by `:1150`. |
| `parser_` (EpubChapterParser)     | exclusive | `cacheTask` (constructor + parsePages)         | `cacheTask` only | Stored in `ReaderState` but only touched while bg task is alive. |
| LUT file `<book>.lut`             | exclusive | `cacheTask` extend / loadLut                   | `loopTask` loadPage | Same `stopBackgroundCaching` convention covers the close+reopen sequence. |

### BLE file transfer

| Field / object                     | Owner-task | Mutators                                       | Readers           | Sync mechanism |
|-----------------------------------|-----------|------------------------------------------------|-------------------|----------------|
| `_filename[128]`                   | shared    | NimBLE host (MetadataCallbacks::onWrite, resetTransfer) | `loopTask` UI     | `BleFileTransfer.cpp` `_state_mux` (portMUX). Batch 4 wraps the transfer-tear-down sequence (`_transferring=false` + memsets) so a UI render mid-tear-down sees either the in-progress state or the cleared state, never a half-zeroed name. SD I/O (`_file.close()` and `SdMan.remove`) happens **outside** the lock to keep the masked region short. |
| `_file` (FsFile)                   | shared    | NimBLE host (open/write/close), main (status check) | both     | Same lock. The data-write path retains a TOCTOU on `isOpen()` vs. `write()` — bounded because a closed `FsFile::write()` returns 0 cleanly, no UB. |
| `_transferring` (bool)             | shared    | NimBLE host                                    | both             | Same lock. |
| `_result` / `_receivedBytes` etc.  | shared    | NimBLE host (storeResult)                      | `loopTask` UI    | `storeResult()` populates `_result` under the same `_state_mux`. The accessor `lastResult()` still returns by `const&` for API compatibility — readers should treat it as a snapshot that becomes stale on the next storeResult. |

In practice these are screen-scoped — BLE FT is only active in `SettingsState::BleTransfer`, where no `cacheTask` runs and no reader pillar is mutating state. Audit #C3 is **CONFIRMED-BENIGN** (verified). The Batch 4 fix is defensive.

### BleBridge (FEATURE_PLUGINS)

| Field / object                     | Owner-task | Mutators                                       | Readers           | Sync mechanism |
|-----------------------------------|-----------|------------------------------------------------|-------------------|----------------|
| `MessageQueue::entries[QUEUE_DEPTH]` | shared  | NimBLE host (push), `loopTask` (drain in `process()`) | both | `portMUX_TYPE q_mux` (Batch 4). Replaced the previous `noInterrupts()/interrupts()` global mask with a per-queue spinlock; the masked region is now bounded to the queue's mutation window instead of holding off all IRQs across a 256-byte memcpy. |
| `_keepAwakeUntilMs` (volatile u32) | shared    | NimBLE host                                    | `loopTask`       | `volatile` + 32-bit atomic word load on ESP32-C3 = sufficient. |
| `_server` (NimBLEServer*)          | shared    | NimBLE host (connect/disconnect)               | `loopTask`       | Pointer assignment is atomic; deref happens under FreeRTOS scheduler. |

### SD card

| Field / object                     | Owner-task | Mutators                                       | Readers           | Sync mechanism |
|-----------------------------------|-----------|------------------------------------------------|-------------------|----------------|
| `SdMan` (SDCardManager singleton)  | shared    | `loopTask`, NimBLE host (BleFileTransfer Data callback writes incoming files to disk), `cacheTask` (LUT extend) | all | Recursive `SemaphoreHandle_t` mutex (Batch 4). `SdLockGuard` RAII at the entry of every public method — `listFiles`, `readFile*`, `writeFile`, `ensureDirectoryExists`, `open`, `mkdir`, `exists`, `remove`, `rmdir`, `rename`, `openFileFor*`, `syncAndClose`, `removeDir`, and the four atomic-write helpers (`atomicOpenWrite`/`atomicCommit`/`atomicAbort`/`recoverAtomicWrites`). Recursive because public methods call other public methods (e.g. `writeFile` → `openFileForWrite`/`syncAndClose`). The `raw()` accessor still bypasses the lock; it's marked WARNING in the header for the few callers (ported emulator code) that pass an `SdFat&` to a third-party library. |

### Settings

| Field / object                     | Owner-task | Mutators                                       | Readers           | Sync mechanism |
|-----------------------------------|-----------|------------------------------------------------|-------------------|----------------|
| `Core::settings`                   | exclusive | `loopTask` only                                | `loopTask` only  | None needed. (BLE callbacks set a `*Dirty_` flag, main applies on next tick.) |
| `bleTransferDirty_` (SettingsState)| shared    | NimBLE host sets                               | `loopTask` reads | `volatile bool` + single-byte. Should be `std::atomic<bool>` for cleanliness; in practice fine. |

---

## C — The rules

Derived from B. A PR that violates one of these is rejected.

### C1 — Renderer is single-task at every moment

Any path that calls `renderer.drawText / getTextWidth / drawImage / clearScreen
/ displayBuffer / displayWindow` from `loopTask` MUST first ensure `cacheTask_`
is not running. The canonical incantation is `stopBackgroundCaching()` (which
calls `cacheTask_.stop(timeout)` and `vTaskDelay(10)`).

The runtime guard (Batch 4 — landed) lives in `GfxRenderer::warnIfNonOwner()`,
called at the entry of each method above. It checks the static
`s_cacheTaskHandle_` field that `ReaderState` publishes between
`startBackgroundCaching()` and `stopBackgroundCaching()`. When the handle is
non-null AND the calling task isn't the registered cacheTask, a rate-limited
warning is logged. The check is permissive (logs and continues) rather than a
hard assert — pragmatic for a defensive guard.

**0.6.0-ramfix update.** The 0.5.x reader paths were silently leaning on the
"permissive guard" — `render()` would draw while the bg task was still alive
because `applyNavResult` restarted the bg task before the next frame painted.
The warnings fired (rate-limited), got ignored, and on BLE-tight heap the
parallel allocation by both tasks pushed cache-extend peak heap past the
parser's safety threshold. 0.6.0-ramfix wired it tight: `ReaderState::render()`
now opens with `stopBackgroundCaching()` and closes with
`startBackgroundCaching(core)`; `ReaderState::applyNavResult()` only
re-starts the bg task when there's no render coming (so the render path
owns the restart). Net effect: no C1 violations from the reader hot path,
and the cache-extend allocation budget is no longer halved by a concurrent
bg-task allocation.

If you find yourself wanting to call a renderer method from a callback or
event handler, you're writing a bug. Defer the work to the next `loopTask`
tick instead.

### C2 — Framebuffer mutations wait for refresh

`waitForRefresh()` is called as the first action of every method that writes
into the framebuffer. The list (verified): `clearScreen`, `drawImage`,
`setFramebuffer`, `grayscaleRevert`, `copyGrayscale*`, `cleanupGrayscaleBuffers`,
`displayWindow`, `displayBuffer`, `deepSleep`. Per-pixel `drawPixel` skips
the wait by design — it's called from inside higher-level methods that
already waited.

If you add a new method that mutates the framebuffer, add `waitForRefresh()`
as line 1.

### C3 — PageCache file is single-task

Same convention as C1. Every `pageCache_->loadPage()` from `loopTask` is
preceded by `stopBackgroundCaching()`. Conversely, `loopTask` never touches
`pageCache_->extend()` — that's `cacheTask`'s job.

### C4 — BLE callbacks copy out before publishing to UI

NimBLE host task callbacks may not directly mutate UI-visible state. They
write to internal fields (`_transferring`, `_filename`, etc.) under a
`portMUX_TYPE` (Batch 4 adds the lock). UI reads via methods that **copy
out** under the same lock. No bare references survive across the lock
release.

### C5 — Cross-task notification uses FreeRTOS primitives, not `noInterrupts`

`portDISABLE_INTERRUPTS()` (a.k.a. `noInterrupts()`) holds the entire CPU.
Use `portMUX_TYPE` for queue mutation, `xTaskNotify*` for one-shot signaling,
`SemaphoreHandle_t` for resource ownership. The BleBridge MessageQueue's
current `noInterrupts()/interrupts()` is the one offender; Batch 4 fixes it.

### C6 — Atomic 32-bit aligned word loads

ESP32-C3 single-core, in-order pipeline: aligned `uint32_t` reads/writes are
atomic. `volatile` is sufficient for one-way signaling between tasks
(Producer writes; consumer reads next iteration). Examples:
`_keepAwakeUntilMs`, `bleTransferDirty_` (currently bool — fine but
prefer `std::atomic<bool>` in new code).

For multi-byte structs or strings, no — use a lock.

### C7 — SDCardManager is mutex-protected (Batch 4 — landed)

Every public method of `SDCardManager` takes a recursive `SemaphoreHandle_t`
mutex (RAII via the file-private `SdLockGuard`) at entry. This makes the
convention "SD I/O is serialized at the bus" enforced rather than implied.
Recursive because some public methods call others (e.g. `writeFile` →
`openFileForWrite` → `syncAndClose`).

Exception: long-running file transfers (BLE FT writing a large file). Each
chunk write is a single `SDCardManager` call (currently `_file.write` against
an open `FsFile`), which doesn't re-acquire the lock — the BLE write loop
holds open the file handle across many chunks but doesn't hold the mutex
between them. Other callers can still take the lock between chunks, which is
exactly the desired behavior for the file-transfer use case.

The `raw()` accessor returns the underlying `SdFat&` and bypasses the lock;
it's documented WARNING in the header. Used only by ported third-party code
(SumiBoy emulator's SD adapter) that takes an `SdFat&` directly. New SUMI
code should use the wrapped methods instead.

---

## D — Things that are NOT shared (don't put them in B)

For the avoidance of doubt:

- All `ReaderState` members touched only outside of `cacheTask`'s lifetime
- All `Core::*` fields except those explicitly listed in B
- `Theme`, `Settings`, `Bookmarks` — main-task only; touched from BLE callbacks
  only via dirty-flag patterns (callback sets flag; main reads on next tick)
- All plugin instance state — `loopTask`-only by construction (plugins
  run synchronously in the main loop)

---

## E — Future work

- **Multi-core port** would invalidate C6's "atomic 32-bit aligned" rule
  for cross-core access. The C3 doesn't have this; if a future SUMI hardware
  revision uses C6 or S3 (dual-core), revisit B's "single-core in-order"
  notes.
- **Real BLE-bridge connection-lifecycle observer** — Batch 9's #50 fixes the
  current "BleBridge installs no callbacks because BleFileTransfer already
  did" fragility by introducing a small fan-out. When that lands, update
  this doc's BleBridge row.
- **Debug-build mutex tracking** — a `LockTracker` that warns when the
  SDCardManager mutex is held >100ms (catches BLE FT or page-cache hogging
  the bus). Nice-to-have for v0.7.

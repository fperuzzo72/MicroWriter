# SUMI Audit Plan — v0.6.0-ramfix

**Status:** Batches 1–10 landed (2026-04-27). Some sub-items deferred — see "Outstanding follow-ups" at the end.
**Source audit:** `~/Desktop/sumi audit.txt` (56 numbered findings, two rounds).
**Verification:** four parallel cluster-verifications cross-checked every claim against current code.

This is the master tracker for the audit-driven cleanup. It supersedes any
informal "what to fix next" note. Every batch references concrete file:line
locations and the verification verdict.

> Companion docs:
> - [ATOMIC_WRITE_DESIGN.md](ATOMIC_WRITE_DESIGN.md) — the helper that gates Batch 1
> - [CONCURRENCY.md](CONCURRENCY.md) — the task / shared-state matrix codifying the ownership model
> - [SETTINGS_VISITOR_SKETCH.md](SETTINGS_VISITOR_SKETCH.md) — the load/save dedup shape
> - [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md) — v2 arena (Batch 10 refresh)
> - [BOOT_FLOW.md](BOOT_FLOW.md) — boot-loop guard, UART0 discipline, dual-boot detection (Batch 10 add)
> - [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) — Batch 10 add

---

## What the audit got wrong

The auditor caveatted that they covered ~30% of the codebase. Verification
demoted **5 CRITICAL findings** that are actually fine in the current code:

| # | Auditor's claim | Reality |
| --- | --- | --- |
| 4 | PNG converter races with ParsedText DP arrays | No path can collide today (RAII bracketed). Latent only — would bite if PNG callbacks ever scratchAlloc. |
| 6 | EpubChapterParser uses renderer in bg task | FIXED — every main-task renderer call is preceded by `stopBackgroundCaching()`. |
| 7 | PageCache uses renderer in bg task | Same; FIXED by ownership pattern. |
| 13 | PageCache loadLut close+reopen race | FIXED by same ownership pattern. |
| 28 | RED RAM single-buffer race | FIXED — every framebuffer-mutating method now starts with `waitForRefresh()`. |
| 29 | wordWidthCache cross-task UB | FIXED-BY-OWNERSHIP — no mutex but the convention works. (Still want a runtime assert; that's [CONCURRENCY.md](CONCURRENCY.md) territory.) |

The auditor missed that SUMI has a tightly-controlled single-task-ownership
convention. That convention is undocumented today, which is why
[CONCURRENCY.md](CONCURRENCY.md) is high-leverage — it ossifies the
guarantees so a future contributor doesn't accidentally violate them.

Also already-fixed: **LookupHistory::addWord** got the truncate-rewrite →
`O_APPEND` fix this week (commit `3d45f69`). But **`LookupHistory::removeWord`
was missed** — Batch 1 catches it.

---

## Risk classification

### 🔴 CRITICAL — observable issues on shipping firmware (3)

| # | Title | File |
| --- | --- | --- |
| 1     | LuaPlugin shrink underflow                       | `src/plugins/LuaPlugin.cpp:33-58` |
| 31    | Atomic-write window — LibraryIndex / Achievements / Bookmarks / ProgressManager | 4 files (+ 21 more SD writers in survey) |
| 14/36 | Boot-loop guard math broken                      | `src/main.cpp:195-225` |

### 🟠 HIGH — latent / wrong under specific conditions (15)

| # | Title | File |
| --- | --- | --- |
| 2     | try/catch dead code in ReaderState (3 sites)                      | `src/states/ReaderState.cpp:336, 822, 1623` |
| 5     | BLE FT path-traversal hardening gaps                              | `src/ble/BleFileTransfer.cpp:286-317` |
| 8/9   | Settings load/save duplication + SETTINGS_COUNT drift             | `src/core/SumiSettings.cpp` |
| 15/40 | UART0 RX floating (HardwareDetect probe + isUsbConnected)         | `lib/HardwareDetect/src/HardwareDetect.cpp:163`, `src/main.cpp:309-317, 489` |
| 26    | Cache files no CRC checksum (8 files)                             | various `.bin` writers |
| 33    | v1 MemoryArena leftovers actively misleading                      | `src/states/ReaderState.cpp:328, 540, 1342-1356` + ~22 other sites |
| 34    | wordWidthCache `int16_t` truncates wide measurements              | `lib/GfxRenderer/GfxRenderer.h:91` |
| 35    | progressPercent `int8_t` wraps + collides with -1 sentinel        | `src/content/LibraryIndex.h:26-29` |
| 37    | BleBridge::extractTopic confuses keys with values                 | `src/ble/BleBridge.cpp:100-116` |
| 38    | ProgressManager has no magic / version / checksum                 | `src/content/ProgressManager.cpp:18-61` |
| 39    | UB in `(int)data[3] << 24`                                        | `src/content/ProgressManager.cpp:100, 104` |
| 41    | settings.bin rewritten 2-3× per boot transition                   | `src/core/BootMode.cpp:55, 73, 92-93, 116-118, 124-128` |

### 🟡 MEDIUM — polish / hardening (~22)

#11 removeDir heap thrash · #12 SleepScreen eager BMP parse · #16 parseJsonInt strtoul · #19 SumiBoyEmulator wrong romData · #20 missing Sleep10Min case · #21 writeFile redundant remove · #22 packed structs endian-fragile · #23 `_folder[32]` too small · #24 parseJsonInt qi/qt uint8_t cast · #25 NimBLE static callbacks · #27 fontSize migration no upper bound · #42 wordWidthCache clear-vs-LRU · #43 InputManager getHeldTime chord-release · #44 InputManager double digitalRead · #46 Bookmarks::load byte-by-byte · #47 SumiClock UTC + gmtime non-reentrant · #48 Achievements no static_assert · #49 LibraryIndex two-reads · #50 BleBridge callback dependency · #52 merge_firmware no SHA256 · #53 PluginListState path truncation · #54 config.h committed font IDs · #55 BleBridge::publishEnveloped no escaping

### 🟢 LOW / nits (~10)

#18 alignas(4)→16 · #45 FsHelpers::normalisePath leading-slash · #51 build_html.py placeholder collision · plus 4 minor findings (N1–N4) from the memory-cluster verification and 5 minor from the concurrency-cluster verification.

### Cross-cutting

- **Memory-arena dead code sweep** (M17/H33 + related; ~25 sites)
- **Atomic-write helper** ([ATOMIC_WRITE_DESIGN.md](ATOMIC_WRITE_DESIGN.md))
- **CONCURRENCY.md** — codifies the single-task-ownership convention
- **GfxRenderer ownership** — runtime assertion to enforce the convention

---

## Batched plan

Each batch is a coherent PR that ships independently. **Code work begins after
the four supporting docs are reviewed.**

### Batch 1 — User-data corruption fixes (do first)

- **[CRIT 1]** LuaPlugin allocator: branch on grow-vs-shrink; positive arithmetic each direction
- **Atomic-write helper** per [ATOMIC_WRITE_DESIGN.md](ATOMIC_WRITE_DESIGN.md). Ports to:
  - **[CRIT 31]** LibraryIndex::updateEntry
  - **[CRIT 31]** Achievements::save
  - **[CRIT 31]** Bookmarks::toggle
  - **[CRIT 31]** ProgressManager::save (paired with **[HIGH 38]** add header + **[HIGH 39]** UB shift fix)
  - **[gap]** LookupHistory::removeWord (the recent fix missed this path)
- **[CRIT 14/36]** Boot-loop guard rewrite (RTC-anchored counter, no millis math)
- **[HIGH 15/40]** UART0 RX pin: `INPUT_PULLDOWN` for both probe and main consumer

### Batch 2 — Memory-arena dead-code sweep

- **[HIGH 33 + MED 17 + N1–N4]** Single sweep PR removing ~25 dead-code sites: `release()`, `releasePrimary()`, `reclaimPrimary()`, `arenaWasInit_`, `fallbackBuffer`. Drop the public symbols. Replace misleading comments with one accurate paragraph at the top of `MemoryArena.h`.
- **[LOW 18]** `alignas(4)` → `alignas(16)` on `s_block`

### Batch 3 — Settings architecture

- **[HIGH 8/9]** Single visitor template per [SETTINGS_VISITOR_SKETCH.md](SETTINGS_VISITOR_SKETCH.md); both `save`/`saveToFile` and `load`/`loadFromFile` walk it; `static_assert` derives the count.
- **[HIGH 41]** BootMode transition state moves to dedicated tiny `/.sumi/transition.bin` (or deferred-write dirty mask), removing 2-3× full settings writes per boot.

### Batch 4 — Concurrency hardening

- Land [CONCURRENCY.md](CONCURRENCY.md) (highest-leverage non-code fix).
- **[CRIT 29 → live wire]** GfxRenderer: runtime `assert(!cacheTask_.isRunning())` at every public method.
- **[HIGH 10]** SDCardManager: `SemaphoreHandle_t` mutex at every public entry (RAII guard).
- **[CRIT 30]** BleBridge: replace `noInterrupts()` with `portMUX_TYPE` spinlock.
- **[CRIT 32]** EInkDisplay destructor: `waitForRefresh()` before `vTaskDelete`.
- **[CRIT 3]** BLE FT shared state: `portMUX` around `_file.close() + _transferring = false`.

### Batch 5 — try/catch + comment cleanup

- **[HIGH 2]** Delete three try/catch wrappers in ReaderState. Replace with `std::nothrow` + heap pre-flight at the original alloc sites; `std::set_new_handler` covers the residual surface. Update comments to document boot-loop-guard reliance.

### Batch 6 — Validation / parsing hardening

- **[HIGH 5]** BLE FT path traversal: component-split `..` check, FAT-reserved chars, trailing dots
- **[MED 23]** Bump `_folder[32]` → `_folder[64]`
- **[MED 16]** `parseJsonInt` range validation (epoch < 4e9, no negatives)
- **[MED 24]** Range-check `qi`/`qt` before `uint8_t` cast
- **[HIGH 37]** BleBridge::extractTopic — proper key/value JSON walker
- **[MED 55]** BleBridge::publishEnveloped — escape topic before snprintf

### Batch 7 — Type widening / sign-correctness

- **[HIGH 34]** wordWidthCache `int16_t` → `int32_t`
- **[HIGH 35]** progressPercent `int8_t` → `int16_t`, clamp [0, 100]; `getProgress` returns `std::optional<uint8_t>`

### Batch 8 — File-format hardening

- **[HIGH 26]** CRC32 trailer added + verified on load for: settings, library, recent, achievements, reading_stats, gbi, progress, bookmarks
- **[MED 22]** Document explicit LE-only assumption for packed structs
- **[MED 27]** Upper-bound check on fontSize migration

### Batch 9 — Misc + polish

#11 stack-allocated path buffer · #12 lazy SleepScreen validation · #19 explicit nullptr in streaming mode · #20 explicit Sleep10Min case · #21 drop redundant remove · #25 NimBLE callback comment · #42 LRU eviction · #43 chord-release docs · #44 cache power-pin read · #45 normalisePath leading-slash · #46 buffered Bookmarks::load · #47 timezone setting + gmtime_r · #48 Achievements static_assert · #49 single-pass updateEntry · #50 fan-out NimBLE callbacks · #52 SHA256 sidecar · #53 luaPaths_[96] · #54 build-time font ID generation

### Batch 10 — Documentation refresh

- Refresh `MEMORY_ARCHITECTURE.md` to match v2 verbatim
- Add `BOOT_FLOW.md` describing boot-loop guard, UART0 pin discipline
- `RELEASE_CHECKLIST.md`: regenerate font IDs, run merge_firmware, generate SHA256, real-hardware smoke test, simulate power-cut for atomic-write recovery test

---

## Ground rules during execution

1. **One batch per PR.** No mixing.
2. **Host unit tests + a real-hardware smoke test before each PR merges.** No regression in either.
3. **Atomic-write recovery test fixture** lands in Batch 1 alongside the helper. Power-cut simulation: stage a `.tmp` and `.bak`, boot, verify `recoverAtomicWrites()` makes the right call.
4. **CRC32 added in Batch 8 reads tolerantly** during the migration window: missing trailer = log warn, accept payload (so existing on-disk files don't get rejected after upgrade).
5. **No file-format magic changes** without a migration entry. The audit's lessons about silent data loss came from skipping this.

---

## What changes vs current state when each batch lands

- **After Batch 1**: data-loss surface closed. Boot-loop guard works. UART0 readings reliable.
- **After Batch 2**: `MemoryArena.h` reads honestly. Future readers don't waste time on dead conditionals.
- **After Batch 3**: settings additions go in one place. Boot transitions stop hammering the SD.
- **After Batch 4**: concurrency model is documented and enforced; `wordWidthCache` UB pathway is asserted-out.
- **After Batch 5**: comments stop lying about exception safety.
- **After Batch 6**: BLE protocol parsing is robust to malformed input.
- **After Batch 7**: typography measurement and library progress no longer have signed-overflow surprises.
- **After Batch 8**: power loss during `.bin` write is recoverable from CRC mismatch (fall back to defaults, no corruption).
- **After Batch 9**: the long tail.
- **After Batch 10**: contributor docs match implementation.

---

## Outstanding follow-ups (post Batches 1–10)

Items that didn't fit cleanly in their parent batch and were deferred.
Each is well-bounded and can be picked up independently.

**Batch 8 follow-up — CRC32 sweep: ✓ LANDED (`aee26d6`).**
The Crc32 helper is now applied to all binary persistent formats:
- `settings.bin`        v16 → v17  (Batch 8 itself)
- `progress.bin`        v1  → v2
- `library.bin`         v2  → v3
- `recent.bin`          v2  → v3
- `achievements.bin`    v1  → v2
- `reading_stats.bin`   v2  → v3
- `global_bookmarks.bin` v1  → v2

`bookmarks.txt` (per-book, line-oriented text) is intentionally
out of scope for the trailer pattern; atomic writes from Batch 1
still cover it.

**Batch 9 follow-up — deferred items:**
- **#11** `removeDir` recursion: replace `std::string filePath = path + "/" + name`
  with a stack-allocated path buffer that's pushed/popped, and
  close+reopen the dir iterator between entries.
- **#12** Lazy SleepScreen validation: collect filenames first (cheap),
  pick random, validate only the chosen one.
- **#42** Renderer `wordWidthCache` LRU eviction: replace `clear()` with
  a FIFO via `std::deque<key>` + map so the last N entries stay warm.
- ~~**#47** Timezone setting + `gmtime_r`~~ ✓ LANDED (`58fe970`) —
  `timeZoneOffsetMinutes` is now persisted in `settings.bin` and applied
  at boot via `setenv("TZ", ...)`; `SumiClock` uses `localtime_r` for
  reads. Settings UI surface deferred — adjustable today only via the
  sumi.page Bluetooth time-sync handshake.
- ~~**#50** NimBLE callback fan-out~~ ✓ LANDED (`e70a1f8`) — new
  `BleHostCallbacks` dispatcher under `src/ble/` lets multiple subsystems
  (`BleFileTransfer`, `BleBridge`, future plugins) subscribe to
  connect/disconnect/MTU events without the "later `setCallbacks()`
  caller wins" race.
- ~~**#51** `build_html.py` placeholder collision~~ ✓ LANDED — random
  per-run sentinel (secrets.token_hex(8)) replaces the deterministic
  `__PRESERVE_BLOCK_N__` token. The other two limitations (script-tag
  string-literal regex termination + `\s+` collapse inside attribute
  values) are documented in the script header — for SUMI's single
  curated index.html these are theoretical; future-proof rewrite is
  out of scope for the audit.
- ~~**#52** `merge_firmware.py` SHA256 sidecar generation.~~ ✓ LANDED
  — the post-build script now emits a `.sha256` next to every merged
  firmware artefact. RELEASE_CHECKLIST step 7 is now a verify, not a
  manual generate.
- **#54** Move `config.h` font IDs to a build-time generated header so
  binaries and IDs can't drift. **Resolved as a drift detector**
  (`scripts/check_font_ids.py`, run as a pre-build hook) rather than a
  forced regeneration: full auto-regeneration would invalidate every
  existing user's EPUB cache (cache filename embeds the font ID), so
  the script logs a `[font-ids] WARN: drift detected` message on every
  build and a maintainer can opt-in to regenerate via
  `python scripts/check_font_ids.py --regenerate`. The warning serves
  the audit's intent — drift is now visible — without forcing the
  migration cost.

---

## New findings during 0.6.0-ramfix stability work

Real-hardware reproductions during the BLE-page-turner stability pass
surfaced three issues that the original audit didn't cover. Each has a
ticket-style description and a "what's known" entry; pick-up is
independent.

### F1 — BLE init hangs `loopTask` past the task-watchdog timeout

**Symptom:** entering Settings → Bluetooth and starting a pairing/connect
session caused `loopTask` to stop yielding for ~60 seconds; the ESP-IDF
task watchdog fired and aborted. Device rebooted, lost the in-progress
reader session, BLE disconnected with no chance to reconnect cleanly.

**Reproduction:** observable with the v0.6.0-ramfix `[HP]` reader
instrumentation. Trigger by entering Reader, then opening Settings,
then choosing the Bluetooth action. Watchdog log: `task_wdt: Task
watchdog got triggered. - loopTask (CPU 0)` followed by `abort()`.

**Code site:** likely inside `ble::init()` (`src/ble/BleHid.cpp:599`)
which runs synchronously on `loopTask`. NimBLE init reportedly includes
controller startup that can stall multi-second.

**Note:** `platformio.ini:24-25` claims "WDT disabled at runtime via
`esp_task_wdt_deinit()` in `earlyInit()`" — but the watchdog clearly
still fires. Either the deinit isn't catching all subscribers or
NimBLE re-arms it. Fix candidates: (a) genuinely disable WDT (audit
why deinit isn't sticking), (b) feed the WDT inside the BLE init path,
or (c) move BLE init to a separate task with its own (looser) watchdog.

### F2 — Long-press Center unreliable on rocker hardware

**Symptom:** the in-reader settings overlay was previously opened via
long-press Center (≥700 ms hold). On real-hardware reproduction, the
Back/Select toggle rocker can't sustain a clean ≥700 ms `isDown` state
— small ADC drift drops the press below threshold mid-hold, resetting
the `pressStartMs_[idx]` timer in `Input.cpp:106-114`, and the eventual
release fires as a short-press (which opens the TOC).

**Workaround landed in 0.6.0-ramfix:** short-press Power in the reader
now opens the in-reader settings overlay when `shortPwrBtn ==
PowerIgnore` (the default). Long-press Center is left in place for any
hardware that can hit the threshold cleanly, but is no longer the
documented binding.

**Real fix candidates (deferred):** (a) detect long-press across short
release-press chains within a tolerance window; (b) lower the
`LONG_PRESS_MS` threshold from 700 to 400 and accept that some users
may trigger settings accidentally while paging; (c) accept the
hardware constraint and remove the long-press path entirely.

### F3 — Cold-extend caps with BLE + heavy chapters

**Symptom:** with BLE active (NimBLE holding ~48 KB) on chapter 6+ of
heavy EPUBs, the parser's safety threshold (`< 4096 B free heap in
startElement`) trips during cold cache extension, leaving the chapter
permanently partial at ~15-30 pages.

**Partial fixes landed in 0.6.0-ramfix:** font release during extend
(+8-15 KB heap), renderer concurrency tightened (peak heap halved),
session-complete on extend failure (advance to next spine on doomed
re-extend). Net result: chapters that previously capped at 15 pages
now reach 20-30 reliably, and "stuck on page N" is no longer a UX wall.

**Real fix:** the on-device indexer (also 0.6.0-ramfix) pre-builds
every spine's cache before reading, structurally eliminating cold
extend at read time. Users with heavy books are pointed at the Index
action from the file browser. F3 is "solved by routing around"; the
underlying heap-pressure-vs-parser-budget question remains open if a
similar pattern shows up elsewhere.

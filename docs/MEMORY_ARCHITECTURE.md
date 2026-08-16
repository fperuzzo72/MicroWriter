# SUMI Memory Architecture

**Status:** describes v2 (single-block, .bss-resident, never-released).
v1 (three independent heap-allocated blocks with a release() escape hatch) is
gone — see git blame for the migration. The names of the sub-regions
(`primaryBuffer`, `scratchBuffer`, `ditherRegion`, `imageRowRegion`,
`taskStackRegion`) carried over unchanged so consumer code didn't need to move.

---

## Memory Budget

| Component | Size | Notes |
|-----------|------|-------|
| ESP32-C3 SRAM | ~400 KB | Total available |
| Static BSS (incl. arena) | ~190 KB | Globals, FreeRTOS, the 76 KB arena below |
| Display framebuffer | 48 KB | 800 × 480 ÷ 8 (also in BSS) |
| Memory Arena | 76 KB | Single .bss block, alignas(16) |
| NimBLE (when active) | ~48 KB | BLE HID page-turner, file transfer |
| **Available heap (post-init)** | **~100–120 KB** | Fonts, page cache, EPUB parser, plugins |

The arena, framebuffer, and FreeRTOS static state all live in `.bss`, which
ESP-IDF zeros at boot before `app_main()`. Nothing arena-shaped is ever
malloc'd from the heap.

---

## Memory Arena (v2)

The arena is a **single 76 KB block** placed by the linker into `.bss` at
program startup. It is never released, never reallocated, and never moves.
All sub-regions are computed as offsets into the same block.

### Buffer Layout

```
┌──────────────────────────────────────────────────────────────┐
│ s_block (76 KB, alignas(16), .bss-resident, single block)     │
├──────────────────────────────────────────────────────────────┤
│ PRIMARY (32 KB) @ offset 0                                    │
│   primaryBuffer │ Full 32 KB, base pointer                    │
│   zipBuffer     │ Alias for primaryBuffer                     │
│                 │   (32 KB used as TINFL LZ77 dictionary)     │
├──────────────────────────────────────────────────────────────┤
│ WORK (20 KB) @ offset 32 KB                                   │
│   scratchBuffer  │  8 KB │ Bump allocator (DP arrays, temp)   │
│   ditherRegion   │  8 KB │ Ditherer error rows (3× ~1.6 KB)   │
│   imageRowRegion │  4 KB │ Bitmap row I/O buffers              │
├──────────────────────────────────────────────────────────────┤
│ TASK STACK (24 KB) @ offset 52 KB                             │
│   taskStackRegion │ 24 KB │ PageCache cacheTask stack         │
│                   │       │ Used by xTaskCreateStatic         │
└──────────────────────────────────────────────────────────────┘
                           76 KB total
```

`alignas(16)` matches the RISC-V calling convention requirement at function
call boundaries — the task stack at offset 52 KB inherits 16-byte alignment
from `s_block` so FreeRTOS's `pxTopOfStack` adjustment doesn't quietly nibble
12 bytes off the stack each task spawn. Pre-Batch 2 the alignment was 4 and
the budget worked in practice; the bump to 16 makes the invariant explicit.

### Why one block instead of three?

The v1 design used three independent `heap_caps_malloc` allocations so each
piece could fit in fragmented heap alongside NimBLE. v2 sidesteps the
fragmentation problem entirely by keeping the arena in `.bss`: it never
contends with the heap, never has to fit between holes, and is always at the
same address. Boot is simpler (no allocation step that can fail), runtime is
simpler (no `release()` hatch with state to track), and the heap that's left
after boot is whatever the linker decided wasn't `.bss` — predictable.

The cost is paying 76 KB up front whether or not the device is reading a
book. The win is a 100% reliable arena available from the first instruction
in `setup()` onwards.

### Bump Allocator

The 8 KB scratch region doubles as a bump allocator for short-lived
allocations via `MemoryArena::scratchAlloc(size)`. Text layout uses this for
DP line-breaking arrays and hyphenation vectors, avoiding any
fragmentation-prone std::vector heap allocations on the hot path.

The `ArenaScratch` RAII guard captures the offset on construction and resets
it on destruction, so nested usage works:

```cpp
{
  sumi::ArenaScratch outer;
  int* a = static_cast<int*>(MemoryArena::scratchAlloc(n * sizeof(int)));
  {
    sumi::ArenaScratch inner;
    size_t* b = static_cast<size_t*>(MemoryArena::scratchAlloc(m * sizeof(size_t)));
    // ... use a + b ...
  }  // b's offset reverts here, a is still valid
}    // a's offset reverts here, scratch is empty again
```

A failed `scratchAlloc` (size > remaining) returns `nullptr`. Callers
fall back to heap `std::vector` if they need the allocation regardless.

### Source Files

- `lib/MemoryArena/MemoryArena.h` — sub-region pointers, `scratchAlloc`, `ArenaScratch`
- `lib/MemoryArena/MemoryArena.cpp` — single-block layout, init, scratch impl

### Usage Pattern

```cpp
// Always-initialised post-boot. The init() call in main is idempotent;
// every check is purely for the host-build and test mocks.
if (!sumi::MemoryArena::isInitialized()) {
  return false;  // host build only
}

// Pre-positioned sub-regions. No malloc needed.
uint8_t* zipDict = sumi::MemoryArena::zipBuffer;        // 32 KB LZ77 dictionary
uint8_t* imgRow  = sumi::MemoryArena::imageRowRegion;    // 4 KB
uint8_t* dither  = sumi::MemoryArena::ditherRegion;      // 8 KB

// Bump allocator for temporary arrays (e.g., text layout DP).
{
  sumi::ArenaScratch guard;
  int* dp     = static_cast<int*>(MemoryArena::scratchAlloc(n * sizeof(int)));
  size_t* ans = static_cast<size_t*>(MemoryArena::scratchAlloc(n * sizeof(size_t)));
  // ... use dp, ans ...
}  // watermark auto-restored here

// Static task allocation — preferred over xTaskCreate's heap stack.
task.startStatic("Name",
                 sumi::MemoryArena::taskStackRegion,
                 sumi::MemoryArena::TASK_STACK_SIZE,
                 std::move(taskBody),
                 0);
```

There is no `MemoryArena::release()`. v1 had one for releasing the arena
during BLE pairing (when NimBLE wanted ~48 KB contiguous); v2 keeps the
arena permanently and the heap-after-init has enough room for NimBLE
plus parser working memory in the same budget.

---

## BLE Coexistence

NimBLE is initialized lazily — only when the file transfer screen, plugin
bridge, or HID is actually being used. When NimBLE is up, it holds ~48 KB
of heap. With the arena in `.bss` instead of heap, the post-init heap
already accounts for that allocation: the linker's heap-start moves up by
the 76 KB arena in `.bss`, leaving the rest for NimBLE + parser + UI.

```
Boot:
  .bss zeroed (includes arena)    ← Free
  app_main() starts                ← Heap available: ~120 KB
  setup()                          ← MemoryArena::init() is a no-op

Enter BLE feature:
  ble::init()                      ← NimBLE takes ~48 KB from heap
  Heap remaining: ~72 KB
  EPUB parser + cache + UI        ← all heap allocations from here

Exit BLE feature:
  ble::deinit()                    ← NimBLE releases ~48 KB
  Heap remaining: ~120 KB

Reader open (BLE held):
  ContentHandle::open()            ← parser allocations from heap
  zipBuffer (.bss arena)            ← TINFL dict, no heap pressure
```

The v1 doc described a `fallbackBuffer` aliasing the framebuffer as a ZIP
dictionary when the heap-allocated `primaryBuffer` had been released for
BLE. v2 doesn't need that hatch: the `.bss` `primaryBuffer` is always
present.

---

## Reader Hot-Path Heap Tactics (0.6.0-ramfix)

The 0.5.x reader path treated heap as a flat pool — `ContentHandle`,
`ReaderState`, `cacheTask`, and the EPUB parser all allocated from the
same heap concurrently. With NimBLE holding ~48 KB the remaining ~20 KB
was too tight for some chapter parses, especially when a `.bin` CJK font
was loaded (another 8-15 KB resident glyph cache). 0.6.0-ramfix layered
three tactics on top of the arena/heap model:

**Concurrency-serialised renderer.** See [CONCURRENCY.md C1](CONCURRENCY.md).
`ReaderState::render()` now calls `stopBackgroundCaching()` before the
first renderer touch and `startBackgroundCaching(core)` after the last.
The bg task and the main render are no longer competing allocators
during the same page paint. Effect: cache-extend peak heap roughly
halved compared to 0.5.1 (was ~17 KB for parser+render co-allocation;
now ~10 KB for parser alone).

**Font release during cold extend.** `ReaderState::createOrExtendCache`
unloads the external `.bin` font's glyph cache (via
`FONT_MANAGER.unloadExternalFont()`) for the duration of the parse and
reloads it after. The parser doesn't render glyphs — it only measures
widths via `getTextWidth`, which falls back to the built-in font's
missing-glyph metrics for CJK during the parse window. Recovers 8-15 KB
contiguous heap exactly where the parser needs it most. The reload
happens before the next render, so user-visible CJK rendering is
unaffected.

**Session-complete on extend failure.** `PageCache::extend` marks
`isPartial_ = false` in memory only (disk header unchanged) when a
cold-extend produces zero new pages. Navigation then advances to the
next spine on a forward press instead of looping on the same doomed
re-extend. Next session reload reads the disk header (still partial)
and gets a fresh chance to extend under different heap conditions.
Memory cost: zero. UX cost: zero. Recovers the user from the "stuck
on page N" failure mode without permanently truncating any chapter.

These three together are what made BLE-active reading viable across
heavy books in 0.6.0-ramfix. The font-release alone gains ~30% more
cached pages per cold extend; the concurrency fix gains another ~30%;
the session-complete fallback makes "we ran out of heap on this
particular chapter" a navigation event instead of a wedge.

---

## On-Device Indexer (0.6.0-ramfix)

`FileListState`'s Index action pre-builds every spine's page cache
before the user opens the book to read. Heap strategy during indexing:

- **External font released** for the entire indexing run (saves 8-15 KB).
- **BLE left untouched.** Calling `ble::init()` after a `ble::deinit()`
  has historically caused multi-second `loopTask` stalls past the
  task-watchdog timeout, so deinit is avoided. Net result: parser
  operates on ~20 KB free + the freed font. Sufficient for typical
  chapters; tight chapters may produce partial spine caches that
  rebuild lazily at read time.
- **`cacheTask` is never started during indexing.** All work runs on
  `loopTask` synchronously, one spine per update tick. The render runs
  *before* each spine's work so "Chapter N of M" is on screen during
  the (1-5 s) parse, not after.
- **Caches persisted to SD** as they're built, so cancellation or
  reboot mid-run loses only the in-flight spine. Already-indexed
  spines stay valid.

The on-device path mirrors `sumi.page/process`'s output format — same
on-disk cache structure, same per-section file names, same config-key
in the cache header. A book indexed on-device and a book processed on
the site are byte-equivalent at the cache level, so the reader's load
path is identical for both.

---

## Buffer Consumers

### JPEGDEC (JpegToBmpConverter)

| Buffer | Use |
|--------|-----|
| `ditherRegion` | Ditherer error rows |
| heap `grayBuffer` | Full image grayscale buffer (heap-allocated, width×height) |

### PngToBmpConverter

| Buffer | Use |
|--------|-----|
| `scratchBuffer` | Source/output row buffers + scaling accumulators |
| `ditherRegion` | Ditherer error rows |

### BitmapHelpers (bmpTo1BitBmpScaled)

| Buffer | Use |
|--------|-----|
| `primaryBuffer` | Source rows + output row buffer |
| `ditherRegion` | Ditherer error rows |

### Text layout (ParsedText)

| Buffer | Use |
|--------|-----|
| Arena bump allocator | DP cost array + answer array for line breaking |

The minimum-raggedness DP allocates two arrays proportional to the number
of words. They come from the bump allocator via `ArenaScratch`. Heap
fallback (`std::vector`) is used when the arena is unavailable (host
tests).

### HomeState (Thumbnails)

| Buffer | Use |
|--------|-----|
| `scratchBuffer` | Thumbnail extraction and decompression |

### ZipFile (EPUB decompression)

| Buffer | Use |
|--------|-----|
| `zipBuffer` | TINFL dictionary (32 KB, miniz-required size) |

`zipBuffer` is the same memory as `primaryBuffer`. ZIP decompression and
image decoding are time-shared (ZIP finishes before images start), so the
alias is safe.

### BackgroundTask (PageCache cacheTask)

| Buffer | Use |
|--------|-----|
| `taskStackRegion` | FreeRTOS stack for `xTaskCreateStatic` |

---

## Flash Thumbnail Cache

Thumbnails persist across reboots in LittleFS (`/thumbs/` directory).

| Field | Value |
|-------|-------|
| Path | `/thumbs/<hash>.thb` |
| Hash | 32-bit MurmurHash2 of book path (matches LibraryIndex hash) |
| Size | 2,700 bytes (120 × 180 ÷ 8) |
| Format | Raw 1-bit bitmap, MSB first |

Source files: `src/content/ThumbnailCache.{h,cpp}`.

---

## Debugging

```cpp
sumi::MemoryArena::printStatus();
```

Output (post-Batch-2):

```
[MEM] === Arena Status (single-block, .bss-resident) ===
[MEM] block=0x3fc80000 size=76 KB (32 primary + 20 work + 24 task stack)
[MEM] PRIMARY: 0x3fc80000
[MEM] WORK:    scratch=0x3fc88000 dither=0x3fc8a000 imgrow=0x3fc8c000
[MEM] STACK:   0x3fc8d000
[MEM] Scratch: 0/8192 bytes used
[MEM] Heap free: 106384, largest: 88052
```

The block address stays the same across boots (linker-placed). If you
ever see arena pointers move, something is wrong.

---

## Design Rationale

### Why pre-allocate?

ESP32 malloc/free cycles fragment the heap quickly. After enough image
operations, the heap is swiss cheese — plenty of total free bytes but no
single block large enough for the next allocation. Pre-allocating fixed
buffers eliminates this failure mode for everything that goes through the
arena.

### Why .bss instead of heap?

v1 used heap allocations because the original design wanted to release the
arena for BLE. v2 keeps the arena permanently — the BLE coexistence numbers
work without the release hatch — so heap-allocating the arena buys nothing
and costs the runtime an allocation step that can fail. `.bss` is allocated
by the linker, zeroed by IDF before `app_main`, and always present.

### Why 32 + 20 + 24 KB?

- **32 KB `primaryBuffer`** (a.k.a. `zipBuffer`): TINFL_LZ_DICT_SIZE for
  ZIP decompression (miniz-required). Also reused as scratch space for
  BMP scaling row buffers during thumbnailing (time-shared).
- **8 KB `scratchBuffer`**: thumbnails, Group5 compression, the bump
  allocator for DP arrays (supports up to 1024 words per paragraph).
- **8 KB `ditherRegion`**: ordered dithering error rows (3 rows × ~1.6 KB
  each for 800 px width).
- **4 KB `imageRowRegion`**: BMP row I/O buffer (max 800 × 4 B).
- **24 KB `taskStackRegion`**: PageCache cacheTask stack. Stack
  high-water-mark instrumentation is logged on cacheTask exit.

Total: 76 KB. With NimBLE active (~48 KB) and the framebuffer in BSS
(48 KB), the post-boot heap is ~120 KB which has comfortably handled
the EPUB parser, page cache, and UI under load.

### Why not use PSRAM?

Both Xteink X4 and X3 ship with an ESP32-C3 without PSRAM. All memory is
internal SRAM. Future C3-or-S3 hardware revisions with PSRAM would add a
new buffer tier; the arena interface wouldn't change.

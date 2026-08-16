# SUMI Boot Flow

Covers the path from reset vector to first input event, with emphasis on
the parts that aren't obvious from reading `setup()`: the boot-loop
guard, UART0 pin discipline (X3 vs X4), and the dual-boot
emulator-detection branch.

---

## Phases

```
ESP-IDF reset vector
  ↓
.bss zeroed (includes 76 KB MemoryArena, framebuffer, FreeRTOS state)
  ↓
app_main() — runs as a FreeRTOS task at priority 1
  ↓
setup() — Arduino's wrapper, runs once
  ↓
loop() — runs forever, the main task body
```

`.bss` zeroing happens before any C++ static constructor — ESP-IDF's
linker script puts `.bss` clear in the early startup. By the time any
SUMI code runs, `MemoryArena::s_block` is 76 KB of zeros and
`MemoryArena::init()` is just a metadata-population step (no actual
zeroing needed).

---

## setup() outline

The order matters. Each step assumes the previous ones completed.

```
1. Serial.begin (UART0 pins set per X3/X4 detection — see "UART0
   discipline" below)
2. SD card init (SDCardManager::begin) — populates the recursive bus
   mutex on first call
3. SDCardManager::recoverAtomicWrites() — scans /.sumi at 2 levels for
   .tmp/.bak orphans left behind by a crash mid-rotation, applies the
   8-state recovery rules from docs/ATOMIC_WRITE_DESIGN.md
4. Settings::loadFromFile — reads /.sumi/settings.bin (with v17 CRC32
   verification on Batch-8+ writes)
5. LittleFS mount (for thumbnail cache + reader_guard)
6. MemoryArena::init() — idempotent, marks the .bss block "ready"
7. EInkDisplay::begin() — spawns the eink_refresh FreeRTOS task
8. Plugin registration — built-in plugins + Lua plugin discovery
9. Boot-mode detection (peekEmulatorTransition) — see "Dual-boot
   detection" below
10. State machine entry — either Home or directly into the emulator if
    pendingTransition is EMULATOR
```

---

## Boot-loop guard

### What it protects against

A book that the device can't render (parser OOM on a huge EPUB, font
asset that fails to load, page cache that triggers a heap exhaustion)
can land in the reader on every boot if `lastBookPath` is set in
settings.bin. Without a guard the device would crash on render, reboot,
crash again, and brick.

### Mechanism

- `core.settings.readerLoadAttempts` (uint8_t in settings.bin) is
  incremented at every reader entry attempt and persisted **before**
  the render call.
- The reader is allowed to clear the counter only after **proof of
  liveness**: `navigateNext` / `navigatePrev` (the user pressed a
  button after the page rendered).
- After 3 attempts without a clean exit, the reader auto-resume is
  skipped and the device boots to Home with a "this book can't open
  on this device" surface.

### Why this matters more after Batch 5

Batch 5 deleted the dead try/catch wrappers in `ReaderState`. Pre-Batch
5 the catch handlers logged but didn't actually catch anything (the
build is `-fno-exceptions`). Post-Batch 5 the same crash still aborts
the chip, but the comments now correctly point at the boot-loop guard
as the recovery mechanism — not at imaginary exception handling.

### Known limitation

The boot-loop guard increments **per-attempt**, not per-failed-attempt.
A user who manually exits the reader before navigating still leaves
the counter at 1 (or whatever attempt number that exit happened on).
The next boot will increment to 2. Three "press Home immediately"
exits in a row look identical to three crashes from the guard's
perspective. In practice this hasn't been a problem — users that
actively close the book don't re-enter it from the auto-resume path
anyway.

---

## UART0 pin discipline

### Why it matters

The ESP32-C3 doesn't have a PSRAM, but it does have shared GPIO uses
that vary between the two SUMI hardware revisions:

| Pin | X4 (current) | X3 (legacy) |
|-----|-------------|-------------|
| GPIO 20 | UART0 RXD (`isUsbConnected` reads this) | I2C SDA |
| GPIO 21 | UART0 TXD (Serial output) | I2C SCL |
| GPIO 0  | Battery ADC | I2C SDA / GPIO 0 (boot strap) |

The X3 has a touch panel + accelerometer on I2C; the X4 dropped both.
Firmware has to detect which board it's on at boot and configure pins
accordingly.

### The probe sequence

`HardwareDetect::runX3ProbePass()`:

1. `Wire.begin(20, 21)` — try I2C on the X3 pin layout
2. Probe known X3 device addresses (touch controller, QMI8658 IMU)
3. If any respond → X3 detected
4. `Wire.end()` — release the pins
5. Set GPIO 20 and GPIO 0 to `INPUT_PULLDOWN` — so when the X4 driver
   takes them over for UART0 RXD and battery ADC, the pins read clean
   LOWs when nothing's driving them

Pre-Batch-1 step 5 used bare `INPUT` (no pull). The pins floated, USB
detection misfired ("am I plugged in?" returned random results from
boot to boot), Serial.begin sometimes never opened a console, and the
battery ADC sampled trace leakage. Symptom in the field: "where are my
boot logs?" — fixed by the `INPUT_PULLDOWN` change.

### Implication for `isUsbConnected`

`isUsbConnected()` reads GPIO 20 (UART0_RXD). On X4 with USB connected,
the host's CDC pulls RXD HIGH. On X4 with no USB, the pin is held LOW
by the pulldown set during X3-probe cleanup. Reading the pin gives a
clean USB-or-not signal that the auto-sleep logic uses to decide the
power-button semantics (long-hold-to-shutdown vs. short-press-to-sleep).

---

## Dual-boot detection (SumiBoy emulator)

The Game Boy emulator (`SumiBoy`) needs the arena layout but doesn't
need most of SUMI's UI. Booting into the emulator from a UI button
press would be straightforward, but the emulator wants its own bring-up
to skip several SUMI features (reader, plugin scan, BLE auto-resume).
The dual-boot path provides that.

### The transition.bin file

- Path: `/.sumi/transition.bin`
- Format: 4-byte magic 'TRSN' + version 1 + reserved + pendingTransition
  enum + transitionReturnTo enum + bookPath[200] = 208 bytes
- Atomic-written via the Batch 1 protocol

### Flow

```
User selects Tetris.gb in the SumiBoy ROM picker:
  saveTransition(BootMode::EMULATOR, "/games/Tetris.gb", ReturnTo::HOME)
  ESP.restart()
  ↓
Reset vector, .bss zero, app_main, setup()
  ↓
peekEmulatorTransition(bookPath) reads transition.bin
  Returns true → bootMode = EMULATOR
  ↓
setup() short-circuits: no reader auto-resume, no plugin scan
  ↓
SumiBoyEmulator::init() takes over the screen
  ↓
On user-exit:
  saveTransition(BootMode::UI)  ← clear emulator + restore UI on next boot
  ESP.restart()
```

Pre-Batch-3b this used a `pendingTransition` field in `settings.bin`,
which meant every emulator launch rewrote 35 settings fields atomically.
The dedicated transition.bin keeps the rewrite scoped to 208 bytes.

The dual-boot uses two parallel signals so the device can recover even
when one of them is wiped:
  * **RTC retention RAM** (`rtcEmuMagic` + `rtcEmuRomPath`) — survives
    a soft reset cleanly; cleared on full power cycle.
  * **`/.sumi/transition.bin`** on SD — survives power cycles, written
    via the atomic-write protocol.

Either alone is enough; both together cover the failure modes for soft
reset vs cold boot.

---

## Crash-safe state files

Every `.bin` file in `/.sumi/` is written via the atomic-write protocol
(3-rename rotation, 8-state recovery on next boot).

| File | Format | Purpose |
|------|--------|---------|
| `settings.bin` | 'PPXS' magic + v17 + 35 fields + CRC32 | User settings |
| `transition.bin` | 'TRSN' magic + v1 + 208 B | Dual-boot signal |
| `progress.bin` (per-book) | 'PROG' magic + v1 | Reading position |
| `library.bin` | v1 + N×Entry | Library index |
| `recent.bin` | v1 + N×Entry | Recent-books carousel |
| `bookmarks.bin` (per-book) | text, one int per line | Bookmark pages |
| `achievements.bin` | uint64_t bitmask | Unlocked achievements |
| `reading_stats.bin` | various counters | Per-session stats |
| `gbi.bin` | global bookmark index | Cross-book bookmark store |

Recovery: `SDCardManager::recoverAtomicWrites()` runs once per boot and
walks `/.sumi/` two levels deep (root + per-book cache subdirs),
`/notes` (depth 0), `/data` (depth 0), `/custom` (depth 1, Lua plugin
saves), and `/games` (depth 1, SumiBoy save states + cheats). It
applies the recovery rules from docs/ATOMIC_WRITE_DESIGN.md to any
`.tmp` or `.bak` orphan it finds. The audit's #38/#41 risk windows are
closed by this step. 0.6.0-ramfix's Batch-1-and-follow-ups extended
the atomic-write protocol to every plugin save (Chess, Sudoku, Notes,
TodoList, Flashcards, ThemeManager) plus Lua's `sumi.writeFile()` and
the GB savestate + cheats files, so the recovery scan's coverage and
the write side now match end-to-end.

CRC32 trailers (Batch 8 + follow-up) land on **all 8** persistent
`.bin` formats: `settings.bin`, `recent.bin`, `library.bin`,
`reading_stats.bin`, `achievements.bin`, `global_bookmarks.bin`,
`overrides.bin`, `reader_guard.bin`. The helper lives in
`lib/Serialization/Crc32.h`. Corrupt-file load rejects the file
silently and rebuilds from defaults on next write — no crash on a
brown-out-corrupted save.

Page caches (`/.sumi/cache/epub_<hash>/sections/<N>.bin`) are NOT
atomic-written: a corrupt one just re-parses the chapter at next read
(see Reader's "Config mismatch, invalidating cache" branch). The
on-device indexer (0.6.0-ramfix) eagerly builds every spine's cache
file via the same path so post-index opens hit the cache from byte
zero.

---

## Cross-references

- [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md) — arena, framebuffer, heap budget
- [CONCURRENCY.md](CONCURRENCY.md) — task graph, ownership rules
- [ATOMIC_WRITE_DESIGN.md](ATOMIC_WRITE_DESIGN.md) — 3-rename rotation, recovery
- [AUDIT_PLAN.md](AUDIT_PLAN.md) — the master cleanup plan that drives Batches 1-10

#pragma once

#include <cstddef>
#include <cstdint>

namespace sumi {

/**
 * Pre-allocated memory arena — 76 KB total, .bss-resident.
 *
 * Design: a single 76 KB `static uint8_t` array in .bss, zeroed by the
 * IDF startup code before main(). Sub-regions live at offsets within
 * the block. The block is never freed — there is nothing to free; .bss
 * is part of the firmware image's address space. After init() runs once
 * at boot, every pointer below is permanently valid for the firmware's
 * lifetime.
 *
 *   Discipline borrowed from the X4 emulator's `emuBlock` pattern:
 *   reserve the biggest thing you need at link time and never give
 *   it back. Everything else (BLE, plugins, emulators) works from the
 *   ~120 KB of heap that remains after boot.
 *
 * LAYOUT (offsets into the single block, total 76 KB):
 *   [0..32KB)       Primary — ZIP LZ77 decompression dictionary
 *   [32..40KB)      Scratch — 8 KB bump allocator
 *   [40..48KB)      Dither  — 8 KB Atkinson dither error rows
 *   [48..52KB)      Image row scratch (4 KB)
 *   [52..76KB)      PageCache background task stack (24 KB, xTaskCreateStatic)
 *
 * Bump allocator: the 8 KB scratch region via scratchAlloc() /
 * ArenaScratch RAII. Text-layout DP arrays and PNG-decode row buffers
 * use it (both with heap fallback).
 *
 * v1 → v2 migration history (kept here so future readers don't reinvent
 * the wheel): v1 allocated three separate heap blocks and exposed
 * release()/releasePrimary()/reclaimPrimary() so callers (BLE, Lua,
 * Game Boy emulator) could free the arena transiently. The release +
 * reclaim ping-pong became the dominant fragmentation source — NimBLE
 * scattered ~15 small allocs through the hole and reclaim() couldn't
 * find 32 KB contiguous. v2 moved everything to a single .bss block,
 * and the old release/reclaim API was removed entirely once all call
 * sites were swept (see Batch 2 of docs/AUDIT_PLAN.md). v1's
 * `fallbackBuffer` (a framebuffer alias used as a ZIP fallback when the
 * arena was unavailable) is similarly gone — the arena is always
 * available now, so there is nothing to fall back from.
 */
class MemoryArena {
 public:
  // === PRIMARY BUFFER (32KB) ===
  static constexpr size_t PRIMARY_BUFFER_SIZE = 32 * 1024;

  // === WORK BUFFER (20KB) ===
  // Reduced from 26KB — the old layout had 6KB of unmapped spare that was allocated
  // but never used. On a 380KB device, 6KB back to the heap matters.
  static constexpr size_t WORK_BUFFER_SIZE = 20 * 1024;

  // Work buffer regions (must fit in WORK_BUFFER_SIZE = 20KB)
  static constexpr size_t SCRATCH_BUFFER_SIZE = 8 * 1024;     // 8KB - text layout DP arrays (1024 words)
  static constexpr size_t DITHER_REGION_SIZE = 8 * 1024;      // 8KB - ditherer error rows (3x ~1.6KB)
  static constexpr size_t IMAGE_ROW_REGION_SIZE = 4 * 1024;   // 4KB - GfxRenderer bitmap row buffers
  // Total: 8 + 8 + 4 = 20KB (exact fit, no waste)

  // === TASK STACK (24KB) - separate allocation ===
  static constexpr size_t TASK_STACK_SIZE = 24 * 1024;        // 24KB - PageCache background task stack

  // Legacy size constants (for Epub.cpp zipBuffer reference)
  static constexpr size_t ZIP_BUFFER_SIZE = 32 * 1024;

  // Buffer pointers (wired by init(); the backing memory lives in .bss
  // for the firmware lifetime).
  static uint8_t* primaryBuffer;   // 32KB - ZIP LZ77 dictionary
  static uint8_t* zipBuffer;       // Alias for primaryBuffer (first 32KB used by LZ77)
  static uint8_t* scratchBuffer;   // 8KB - bump allocator region
  static uint8_t* ditherRegion;    // 8KB - ditherer error rows
  static uint8_t* imageRowRegion;  // 4KB - bitmap row buffers
  static uint8_t* taskStackRegion; // 24KB - background task stack (for xTaskCreateStatic)

  // Wire pointers to the .bss-resident block. Cheap, idempotent, never
  // fails — kept as a function (rather than a constructor) so boot
  // order stays explicit in main().
  static bool init();

  // Check if arena is currently allocated. Always true post-init().
  static bool isInitialized() { return initialized_; }

  // --- Bump allocator for temporary scratch allocations ---
  static void* scratchAlloc(size_t size);
  static void scratchReset();
  static size_t scratchRemaining();

  static void printStatus();

  static constexpr size_t totalSize() {
    return PRIMARY_BUFFER_SIZE + WORK_BUFFER_SIZE + TASK_STACK_SIZE;
  }

 private:
  friend class ArenaScratch;
  static bool initialized_;
  static uint8_t* primaryBase_;    // 32KB allocation base
  static uint8_t* workBase_;       // 26KB allocation base
  static uint8_t* taskStackBase_;  // 24KB allocation base
  static size_t scratchOffset_;    // Bump allocator watermark within scratch region
  static void scratchSetOffset(size_t offset) { scratchOffset_ = offset; }
  MemoryArena() = delete;
};

/**
 * RAII guard that resets the arena bump allocator on destruction.
 * Use this around text layout operations to ensure scratch memory is reclaimed.
 */
class ArenaScratch {
  size_t savedOffset_;

 public:
  ArenaScratch();
  ~ArenaScratch();

  ArenaScratch(const ArenaScratch&) = delete;
  ArenaScratch& operator=(const ArenaScratch&) = delete;

  template <typename T>
  T* alloc(size_t count) {
    void* p = MemoryArena::scratchAlloc(count * sizeof(T));
    return static_cast<T*>(p);
  }

  bool isValid() const;
};

}  // namespace sumi

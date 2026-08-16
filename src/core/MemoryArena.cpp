#include "MemoryArena.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace sumi {

// ──────────────────────────────────────────────────────────────────────
// MemoryArena — single-block, never-released, linker-placed.
//
// The header (MemoryArena.h) carries the design rationale and the v1→v2
// migration story. Implementation notes only here.
// ──────────────────────────────────────────────────────────────────────

// Single-block layout. Sizes must sum to BLOCK_SIZE exactly.
static constexpr size_t BLOCK_SIZE =
    MemoryArena::PRIMARY_BUFFER_SIZE +   // 32 KB (ZIP LZ77 dict)
    MemoryArena::WORK_BUFFER_SIZE +      // 20 KB (scratch + dither + img rows)
    MemoryArena::TASK_STACK_SIZE;        // 24 KB (PageCache task stack)

// Linker-placed static block. Lives in .bss, which IDF zeros at boot
// before main() runs. 16-byte alignment matches the RISC-V calling
// convention requirement at function call boundaries — important
// because the task-stack sub-region at offset (32+20)*1024 = 53248
// is itself 16-byte aligned only if `s_block` is. FreeRTOS's
// xTaskCreateStatic rounds the stack pointer down to the required
// alignment, so the previous `alignas(4)` happened to work in practice
// (lost at most 12 bytes), but `alignas(16)` makes the invariant
// explicit and prevents future quiet stack-budget shrinkage.
alignas(16) static uint8_t s_block[BLOCK_SIZE];

uint8_t* MemoryArena::primaryBase_ = nullptr;
uint8_t* MemoryArena::workBase_ = nullptr;
uint8_t* MemoryArena::taskStackBase_ = nullptr;
uint8_t* MemoryArena::primaryBuffer = nullptr;
uint8_t* MemoryArena::zipBuffer = nullptr;
uint8_t* MemoryArena::scratchBuffer = nullptr;
uint8_t* MemoryArena::ditherRegion = nullptr;
uint8_t* MemoryArena::imageRowRegion = nullptr;
uint8_t* MemoryArena::taskStackRegion = nullptr;
bool MemoryArena::initialized_ = false;
size_t MemoryArena::scratchOffset_ = 0;

bool MemoryArena::init() {
  if (initialized_) {
    // Idempotent — v1 callers sprinkled init() everywhere and we still
    // compile them. Every call after the first is a no-op.
    return true;
  }

  Serial.printf("[%lu] [MEM] Wiring static arena (%zu KB, linker-placed)\n",
                millis(), BLOCK_SIZE / 1024);

  // Lay sub-regions out by offset into the linker-placed block. .bss is
  // already zeroed so no memset is needed — but we reset the scratch
  // watermark because init() can be called post-boot from paths that
  // haven't touched the arena since the last parse.
  size_t offset = 0;

  primaryBase_ = s_block + offset;
  primaryBuffer = primaryBase_;
  zipBuffer = primaryBuffer;
  offset += PRIMARY_BUFFER_SIZE;

  workBase_ = s_block + offset;
  scratchBuffer = workBase_;
  ditherRegion = workBase_ + SCRATCH_BUFFER_SIZE;
  imageRowRegion = workBase_ + SCRATCH_BUFFER_SIZE + DITHER_REGION_SIZE;
  offset += WORK_BUFFER_SIZE;

  taskStackBase_ = s_block + offset;
  taskStackRegion = taskStackBase_;
  offset += TASK_STACK_SIZE;

  scratchOffset_ = 0;
  initialized_ = true;

  Serial.printf("[%lu] [MEM] Arena @%p: primary=%p work=%p stack=%p\n",
                millis(), s_block, primaryBuffer, scratchBuffer, taskStackRegion);
  Serial.printf("[%lu] [MEM] Heap state: free=%lu, largest=%lu\n",
                millis(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return true;
}

void* MemoryArena::scratchAlloc(size_t size) {
  if (!initialized_ || !scratchBuffer || size == 0) {
    return nullptr;
  }

  // Align to 4-byte boundary (required for int/size_t arrays on ESP32).
  const size_t alignedOffset = (scratchOffset_ + 3) & ~static_cast<size_t>(3);

  if (alignedOffset + size > SCRATCH_BUFFER_SIZE) {
    return nullptr;
  }

  void* ptr = scratchBuffer + alignedOffset;
  scratchOffset_ = alignedOffset + size;
  return ptr;
}

void MemoryArena::scratchReset() {
  scratchOffset_ = 0;
}

size_t MemoryArena::scratchRemaining() {
  if (!initialized_ || !scratchBuffer) return 0;
  const size_t alignedOffset = (scratchOffset_ + 3) & ~static_cast<size_t>(3);
  return (alignedOffset < SCRATCH_BUFFER_SIZE) ? (SCRATCH_BUFFER_SIZE - alignedOffset) : 0;
}

void MemoryArena::printStatus() {
  if (initialized_) {
    Serial.println("[MEM] === Arena Status (single-block, .bss-resident) ===");
    Serial.printf("[MEM] block=%p size=%zu KB (%zu primary + %zu work + %zu task stack)\n",
                  s_block, BLOCK_SIZE / 1024, PRIMARY_BUFFER_SIZE / 1024,
                  WORK_BUFFER_SIZE / 1024, TASK_STACK_SIZE / 1024);
    Serial.printf("[MEM] PRIMARY: %p\n", primaryBuffer);
    Serial.printf("[MEM] WORK:    scratch=%p dither=%p imgrow=%p\n",
                  scratchBuffer, ditherRegion, imageRowRegion);
    Serial.printf("[MEM] STACK:   %p\n", taskStackRegion);
    Serial.printf("[MEM] Scratch: %zu/%zu bytes used\n", scratchOffset_, SCRATCH_BUFFER_SIZE);
  } else {
    Serial.println("[MEM] === Arena Status (uninitialized) ===");
  }
  Serial.printf("[MEM] Heap free: %lu, largest: %lu\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

// --- ArenaScratch RAII guard — unchanged ---

ArenaScratch::ArenaScratch() : savedOffset_(MemoryArena::scratchOffset_) {}

ArenaScratch::~ArenaScratch() {
  MemoryArena::scratchSetOffset(savedOffset_);
}

bool ArenaScratch::isValid() const {
  return MemoryArena::isInitialized() && MemoryArena::scratchBuffer != nullptr;
}

}  // namespace sumi

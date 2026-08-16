#pragma once

// ──────────────────────────────────────────────────────────────────────
// CRC-32 / IEEE 802.3 (reflected, polynomial 0xEDB88320). Single-byte
// loop, no precomputed table. ~10 us/KB on ESP32-C3 — fine for the .bin
// files we use it on (largest is bookmarks.bin at ~2 KB).
//
// Table-free is a deliberate choice: the 1 KB lookup table would land in
// .rodata and cost flash on a 380 KB-DRAM / 16 MB-flash device that
// already runs at 84% flash. The byte-by-byte loop runs in microseconds
// for the file sizes we care about.
//
// Used by Batch 8 to add integrity trailers to the eight persistent
// .bin formats. See docs/AUDIT_PLAN.md for the migration policy:
//   write: always emit CRC32 trailer.
//   read:  if the trailer's missing or mismatches, log warn + fall
//          back to defaults / accept payload tolerantly. Existing
//          on-disk files don't get rejected post-upgrade.
// ──────────────────────────────────────────────────────────────────────

#include <stddef.h>
#include <stdint.h>

namespace sumi {

class Crc32 {
 public:
  Crc32() : crc_(0xFFFFFFFFu) {}

  // Accumulate `len` bytes from `data` into the running CRC.
  void update(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t c = crc_;
    for (size_t i = 0; i < len; ++i) {
      c ^= p[i];
      for (int j = 0; j < 8; ++j) {
        const uint32_t mask = -static_cast<int32_t>(c & 1u);
        c = (c >> 1) ^ (0xEDB88320u & mask);
      }
    }
    crc_ = c;
  }

  // Final CRC value to write to the file or compare against the trailer.
  // Idempotent: calling finalize() twice returns the same value.
  uint32_t finalize() const { return crc_ ^ 0xFFFFFFFFu; }

  // Reset to initial state — use when reusing a Crc32 instance for a
  // second pass over a different payload.
  void reset() { crc_ = 0xFFFFFFFFu; }

 private:
  uint32_t crc_;
};

// One-shot helper for callers that have the whole payload in memory.
inline uint32_t crc32(const void* data, size_t len) {
  Crc32 c;
  c.update(data, len);
  return c.finalize();
}

}  // namespace sumi

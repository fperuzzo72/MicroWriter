#pragma once

#include <cstdint>

namespace sumi {

struct Core;

// LibraryIndex - Lightweight per-book progress index stored in /.sumi/library.bin
// Used by the file browser to show progress bars and content type icons.
// Updated by the reader whenever progress is saved.
//
// File format (v2):
//   Header: version(1) + count(2) = 3 bytes
//   Entries: [pathHash(4) + currentPage(2) + totalPages(2) + contentHint(1)] * N = 9N bytes
//
// 100 books = 903 bytes. One read on browser entry, one write per progress save.
//
// Endianness: every multi-byte field in the on-disk format is written and
// read in the host's NATIVE byte order via writePod / readPod (raw memcpy).
// SUMI is shipped exclusively on ESP32-C3 (RISC-V, little-endian). If the
// firmware is ever ported to a big-endian target the on-disk format would
// silently become incompatible — the library.bin files written by C3
// devices would read with bytes swapped on the BE host. Audit #22 flags
// this as a documentation requirement: any port to BE must add explicit
// htole32/le32toh wrappers around the multi-byte fields below before
// shipping. (Same notice applies to RecentBooks::Entry, ProgressManager's
// v2 format, ContentMetadata's serialized form, and BootMode's
// transition.bin.)
class LibraryIndex {
 public:
  struct __attribute__((packed)) Entry {
    uint32_t pathHash;     // std::hash<string>(filepath)
    uint16_t currentPage;  // absolute page (flat)
    uint16_t totalPages;   // total pages in book
    uint8_t contentHint;   // ContentHint enum value from dc:subject

    // Returns the reading progress in percent, clamped to [0, 100].
    // Pre-Batch-7 returned int8_t and never clamped — a stale entry where
    // currentPage somehow exceeded totalPages (cache rebuild between
    // versions, manual SD edit, decoder bug producing a wider book.bin)
    // produced values like 200 → static_cast<int8_t> = -56, which the
    // ">=0 means valid" sentinel test silently treated as "unknown".
    // Wider type + explicit clamp surfaces the corruption (no more
    // negative phantoms) and produces a sane display percent regardless.
    // Audit #35.
    int16_t progressPercent() const {
      if (totalPages == 0) return 0;
      const uint32_t pct =
          static_cast<uint32_t>(currentPage) * 100u / totalPages;
      if (pct > 100u) return 100;
      return static_cast<int16_t>(pct);
    }
  };

  // Update or create an entry for the given book path.
  // Called from ReaderState when progress is saved.
  static bool updateEntry(Core& core, const char* bookPath, uint16_t currentPage, uint16_t totalPages,
                          uint8_t contentHint = 0);

  // Look up progress for a book by its full path.
  // Returns the reading progress in percent: -1 if the book isn't in
  // the index, 0..100 otherwise. (Audit #35 originally specified
  // std::optional<uint8_t> for clearer "missing" semantics, but the
  // toolchain's effective C++ standard varies by include path on this
  // tree — `<optional>` doesn't resolve from main.cpp's compile under
  // certain library combinations. Sticking with the int16_t/-1
  // convention keeps headers buildable, and the underlying bug — a
  // negative phantom from int8_t arithmetic wrap — is fixed by the
  // wider type + explicit clamp inside Entry::progressPercent().)
  static int16_t getProgress(Core& core, const char* bookPath);

  // Batch load: read all entries into a caller-provided array.
  // Returns number of entries read (up to maxEntries).
  static int loadAll(Core& core, Entry* entries, int maxEntries);

  // Find a single entry by hash (low memory usage)
  static bool findByHash(Core& core, uint32_t hash, Entry& entry);

  // Compute hash for a filepath (same hash used everywhere)
  static uint32_t hashPath(const char* path);

 private:
  // v2: added contentHint byte. v3: appended 4-byte CRC32 trailer over
  // all preceding bytes (audit #26 follow-up). v3 reads still tolerate
  // v2 files (no trailer) — they get migrated on next save.
  static constexpr uint8_t VERSION = 3;
  static constexpr uint8_t VERSION_WITH_CRC = 3;
  static constexpr int MAX_ENTRIES = 200;
  static constexpr const char* INDEX_PATH = "/.sumi/library.bin";
};

}  // namespace sumi

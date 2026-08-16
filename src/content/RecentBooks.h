#pragma once

#include <cstdint>
#include <cstring>

namespace sumi {

struct Core;

// RecentBooks - Tracks recently opened books in order
// Stored in /.sumi/recent.bin
// Used by HomeState to show library carousel
//
// Endianness: every multi-byte field in the Entry struct below is
// written and read via raw memcpy (writePod / readPod), so the on-disk
// representation is the host's NATIVE byte order. SUMI ships exclusively
// on ESP32-C3 (RISC-V, little-endian); any port to a big-endian target
// must wrap multi-byte fields in explicit htole32 / le32toh before
// shipping or recent.bin written on C3 will read with byte-swapped
// numerics on the BE host. Audit #22.
class RecentBooks {
 public:
  static constexpr int MAX_RECENT = 10;
  static constexpr int PATH_LEN = 128;
  static constexpr int TITLE_LEN = 64;
  static constexpr int AUTHOR_LEN = 48;
  static constexpr int THUMB_LEN = 80;

  struct __attribute__((packed)) Entry {
    char path[PATH_LEN];
    char title[TITLE_LEN];
    char author[AUTHOR_LEN];
    uint32_t lastAccess;  // Reserved (no RTC; ordering is by file position)
    uint16_t progress;    // 0-100 percent
    char thumbPath[THUMB_LEN];  // Persisted thumbnail BMP path (empty = none)

    bool isEmpty() const { return path[0] == '\0'; }
    bool hasThumb() const { return thumbPath[0] != '\0'; }
  };

  // Record that a book was opened (moves to front if already in list)
  static void recordOpen(Core& core, const char* path, const char* title,
                         const char* author, uint16_t progress = 0,
                         const char* thumbPath = nullptr);

  // Update progress for a book (doesn't change order)
  static void updateProgress(Core& core, const char* path, uint16_t progress);

  // Update thumbnail path for a book (persists across sessions)
  static void updateThumbPath(Core& core, const char* path, const char* thumbPath);

  // Load all recent books (returns count, fills entries array)
  // Entries are in most-recent-first order
  static int loadAll(Core& core, Entry* entries, int maxEntries);

  // Get the most recent book (returns false if no recent books)
  static bool getMostRecent(Core& core, Entry& entry);

  // Clear all recent books
  static void clear(Core& core);

 private:
  // v2: added thumbPath field. v3: appended 4-byte CRC32 trailer over
  // version+count+entries (audit #26 follow-up). v3 reads still tolerate
  // v2 files (no trailer) — they get migrated on next save.
  static constexpr uint8_t VERSION = 3;
  static constexpr uint8_t VERSION_WITH_CRC = 3;
  static constexpr const char* INDEX_PATH = "/.sumi/recent.bin";
};

}  // namespace sumi

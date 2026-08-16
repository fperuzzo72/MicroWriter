#include "GlobalBookmarkIndex.h"

#include <Crc32.h>
#include <SDCardManager.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>

namespace sumi {

// Binary format:
// [4] magic = "GBMK"
// [1] version = 1 (no CRC) or 2 (CRC32 trailer over preceding bytes)
// [2] count (uint16_t, capped at MAX_ENTRIES)
// [N * sizeof(Entry)] entries
// v2 only:
// [4] CRC32 trailer over the 7-byte header + N entries
//
// v2 reads still tolerate v1 files (no trailer); migration on next save.
// Audit #26 follow-up.

static constexpr uint8_t MAGIC[4] = {'G', 'B', 'M', 'K'};
static constexpr uint8_t VERSION = 2;
static constexpr uint8_t VERSION_WITH_CRC = 2;
static constexpr size_t HEADER_SIZE = 4 + 1 + 2;  // magic + version + count

uint32_t GlobalBookmarkIndex::hashPath(const char* path) {
  // FNV-1a 32-bit
  uint32_t hash = 2166136261u;
  if (path) {
    for (const char* p = path; *p; ++p) {
      hash ^= static_cast<uint8_t>(*p);
      hash *= 16777619u;
    }
  }
  return hash;
}

std::vector<GlobalBookmarkIndex::Entry> GlobalBookmarkIndex::loadAll() {
  std::vector<Entry> entries;

  FsFile f;
  if (!SdMan.openFileForRead("GBI", INDEX_PATH, f)) {
    return entries;
  }

  Crc32 crc;

  // Read and validate header
  uint8_t header[HEADER_SIZE];
  const int n = f.read(header, HEADER_SIZE);
  if (n != static_cast<int>(HEADER_SIZE)) {
    f.close();
    return entries;
  }

  // Accept v1 (pre-trailer) and v2 (CRC trailer). v2 reads still
  // tolerate v1 — they get migrated on next write.
  const uint8_t version = header[4];
  if (memcmp(header, MAGIC, 4) != 0 || version < 1 || version > VERSION) {
    f.close();
    return entries;
  }
  crc.update(header, HEADER_SIZE);

  const uint16_t requested = static_cast<uint16_t>(header[5]) |
                             (static_cast<uint16_t>(header[6]) << 8);
  uint16_t count = requested;
  // Bound-check count against MAX_ENTRIES to prevent heap exhaustion
  if (count > MAX_ENTRIES) count = MAX_ENTRIES;

  entries.reserve(count);
  for (uint16_t i = 0; i < count; i++) {
    Entry entry;
    const int bytesRead = f.read(reinterpret_cast<uint8_t*>(&entry), sizeof(Entry));
    if (bytesRead != static_cast<int>(sizeof(Entry))) break;
    crc.update(&entry, sizeof(Entry));
    // Ensure null termination of string fields
    entry.bookTitle[sizeof(entry.bookTitle) - 1] = '\0';
    entry.snippet[sizeof(entry.snippet) - 1] = '\0';
    entries.push_back(entry);
  }

  // CRC32 trailer (v2+). Tolerant per audit policy. If `requested` was
  // capped (e.g. malicious header) the on-disk CRC was over the full
  // claimed entries, which we didn't read — skip the check.
  if (version >= VERSION_WITH_CRC && requested == count) {
    uint32_t fileCrc = 0;
    if (f.read(reinterpret_cast<uint8_t*>(&fileCrc), 4) == 4) {
      const uint32_t computed = crc.finalize();
      if (fileCrc != computed) {
        Serial.printf("[GBI] WARN: global_bookmarks.bin CRC32 mismatch "
                      "(file=0x%08X computed=0x%08X); accepting payload\n",
                      static_cast<unsigned>(fileCrc),
                      static_cast<unsigned>(computed));
      }
    } else {
      Serial.printf("[GBI] WARN: global_bookmarks.bin v%u missing CRC32 trailer\n",
                    static_cast<unsigned>(version));
    }
  }

  f.close();
  return entries;
}

static bool writeEntries(const std::vector<GlobalBookmarkIndex::Entry>& entries) {
  SdMan.ensureDirectoryExists("/.sumi");

  // Atomic — pre-audit this used regular openFileForWrite plus
  // a CRC32 trailer (the trailer caught bit-flips but not partial
  // writes). Power loss between O_TRUNC and the first byte left
  // global_bookmarks.bin empty and the user's cross-book bookmark list
  // was wiped. atomicOpenWrite + atomicCommit closes the partial-write
  // window the same way settings.bin and the other /.sumi/* files do.
  FsFile f;
  if (!SdMan.atomicOpenWrite("GBI", GlobalBookmarkIndex::INDEX_PATH, f)) {
    Serial.println("[GBI] atomicOpenWrite failed");
    return false;
  }

  Crc32 crc;

  // Write header
  f.write(MAGIC, 4);
  crc.update(MAGIC, 4);
  f.write(VERSION);
  crc.update(&VERSION, 1);
  uint16_t count = static_cast<uint16_t>(
      std::min(static_cast<int>(entries.size()), GlobalBookmarkIndex::MAX_ENTRIES));
  f.write(reinterpret_cast<const uint8_t*>(&count), 2);
  crc.update(&count, 2);

  // Write entries
  for (uint16_t i = 0; i < count; i++) {
    f.write(reinterpret_cast<const uint8_t*>(&entries[i]),
            sizeof(GlobalBookmarkIndex::Entry));
    crc.update(&entries[i], sizeof(GlobalBookmarkIndex::Entry));
  }

  // CRC32 trailer (v2+). Audit #26 follow-up.
  const uint32_t trailer = crc.finalize();
  f.write(reinterpret_cast<const uint8_t*>(&trailer), 4);

  if (!SdMan.atomicCommit(f, GlobalBookmarkIndex::INDEX_PATH)) {
    SdMan.atomicAbort(f, GlobalBookmarkIndex::INDEX_PATH);
    Serial.println("[GBI] atomicCommit failed");
    return false;
  }
  return true;
}

void GlobalBookmarkIndex::addBookmark(const char* bookPath, const char* bookTitle,
                                       uint32_t page, const char* snippet) {
  auto entries = loadAll();
  uint32_t hash = hashPath(bookPath);

  // Check for duplicate (same book + page)
  for (const auto& e : entries) {
    if (e.bookPathHash == hash && e.page == page) {
      return;  // Already exists
    }
  }

  // Enforce cap: if at max, drop the oldest entry (first in list)
  if (static_cast<int>(entries.size()) >= MAX_ENTRIES) {
    entries.erase(entries.begin());
  }

  Entry entry;
  memset(&entry, 0, sizeof(entry));
  entry.bookPathHash = hash;
  entry.page = page;
  utf8SafeCopy(entry.bookTitle, bookTitle ? bookTitle : "", sizeof(entry.bookTitle));
  utf8SafeCopy(entry.snippet, snippet ? snippet : "", sizeof(entry.snippet));

  entries.push_back(entry);
  writeEntries(entries);
}

void GlobalBookmarkIndex::removeBookmark(const char* bookPath, uint32_t page) {
  auto entries = loadAll();
  uint32_t hash = hashPath(bookPath);

  auto it = std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) {
    return e.bookPathHash == hash && e.page == page;
  });

  if (it != entries.end()) {
    entries.erase(it, entries.end());
    writeEntries(entries);
  }
}

int GlobalBookmarkIndex::count() {
  FsFile f;
  if (!SdMan.openFileForRead("GBI", INDEX_PATH, f)) {
    return 0;
  }

  uint8_t header[HEADER_SIZE];
  const int n = f.read(header, HEADER_SIZE);
  f.close();

  if (n != static_cast<int>(HEADER_SIZE)) return 0;
  // Accept v1 (no trailer) and v2 (CRC trailer) for the cheap count
  // path — we don't need to verify the CRC just to read a count.
  if (memcmp(header, MAGIC, 4) != 0 || header[4] < 1 || header[4] > VERSION) {
    return 0;
  }

  uint16_t count = static_cast<uint16_t>(header[5]) |
                   (static_cast<uint16_t>(header[6]) << 8);

  if (count > MAX_ENTRIES) count = MAX_ENTRIES;
  return count;
}

}  // namespace sumi

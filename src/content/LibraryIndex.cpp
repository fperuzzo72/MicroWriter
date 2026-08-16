#include "LibraryIndex.h"

#include <Arduino.h>
#include <Crc32.h>

#include <vector>

#include "../core/Core.h"

namespace sumi {

// GCC 8.4 requires out-of-class definition for ODR-used static constexpr
constexpr uint8_t LibraryIndex::VERSION;

uint32_t LibraryIndex::hashPath(const char* path) {
  // MurmurHash2 — matches GCC 8.4 std::hash<std::string> on 32-bit targets.
  // Must stay in sync with Epub.h cachePath hash so thumbnail lookups work.
  // Avoids std::string heap allocation which can abort() on ESP32.
  const auto* data = reinterpret_cast<const uint8_t*>(path);
  size_t len = strlen(path);
  constexpr uint32_t seed = 0xc70f6907u;
  constexpr uint32_t m = 0x5bd1e995u;
  uint32_t hash = seed ^ static_cast<uint32_t>(len);

  while (len >= 4) {
    uint32_t k;
    memcpy(&k, data, 4);
    k *= m;
    k ^= k >> 24;
    k *= m;
    hash *= m;
    hash ^= k;
    data += 4;
    len -= 4;
  }

  switch (len) {
    case 3: hash ^= static_cast<uint32_t>(data[2]) << 16; [[fallthrough]];
    case 2: hash ^= static_cast<uint32_t>(data[1]) << 8;  [[fallthrough]];
    case 1: hash ^= static_cast<uint32_t>(data[0]);
            hash *= m;
  }

  hash ^= hash >> 13;
  hash *= m;
  hash ^= hash >> 15;
  return hash;
}

bool LibraryIndex::updateEntry(Core& core, const char* bookPath, uint16_t currentPage, uint16_t totalPages,
                               uint8_t contentHint) {
  if (!bookPath || bookPath[0] == '\0') return false;

  const uint32_t hash = hashPath(bookPath);

  // Single-pass: read every existing entry into a small in-memory vector
  // (200 × 9 bytes = 1.8 KB max), patch in place, then write once.
  // Pre-Batch-9 this opened INDEX_PATH twice — once to find the matching
  // hash, once to copy entries into the new file — and the second open
  // could see a different state if anything else mutated SD between
  // them. The atomic-write protocol still wraps the write side; the
  // change here is purely "halve the SD opens". Audit #49.
  std::vector<Entry> entries;
  entries.reserve(MAX_ENTRIES);

  FsFile readFile;
  if (core.storage.openRead(INDEX_PATH, readFile).ok()) {
    uint8_t version = 0;
    Crc32 crc;
    // Accept v2 (pre-Batch-8 layout, no trailer) and v3 (adds CRC32
    // trailer). v3 reads still tolerate v2 files — they get migrated
    // on next save.
    if (readFile.read(&version, 1) == 1 && version >= 2 && version <= VERSION) {
      crc.update(&version, 1);
      uint16_t count = 0;
      if (readFile.read(reinterpret_cast<uint8_t*>(&count), 2) == 2) {
        crc.update(&count, 2);
        if (count > MAX_ENTRIES) {
          Serial.printf("[%lu] [LIB] count %u exceeds MAX_ENTRIES %d, capping\n",
                        millis(), count, MAX_ENTRIES);
          count = MAX_ENTRIES;
        }
        Entry tempEntry;
        for (int i = 0; i < count; i++) {
          if (readFile.read(reinterpret_cast<uint8_t*>(&tempEntry), sizeof(Entry)) ==
              sizeof(Entry)) {
            crc.update(&tempEntry, sizeof(Entry));
            entries.push_back(tempEntry);
          } else {
            break;  // Truncated — keep what we have.
          }
        }
        // Verify CRC32 trailer for v3+. Tolerant per audit policy: a
        // mismatch logs and we keep the data we already loaded.
        if (version >= VERSION_WITH_CRC) {
          uint32_t fileCrc = 0;
          if (readFile.read(reinterpret_cast<uint8_t*>(&fileCrc), 4) == 4) {
            const uint32_t computed = crc.finalize();
            if (fileCrc != computed) {
              Serial.printf("[%lu] [LIB] WARN: library.bin CRC32 mismatch "
                            "(file=0x%08X computed=0x%08X); accepting payload\n",
                            millis(),
                            static_cast<unsigned>(fileCrc),
                            static_cast<unsigned>(computed));
            }
          } else {
            Serial.printf("[%lu] [LIB] WARN: library.bin v%u missing CRC32 trailer\n",
                          millis(), static_cast<unsigned>(version));
          }
        }
      }
    } else if (version != 0) {
      Serial.printf("[%lu] [LIB] Unsupported version %u, dropping\n",
                    millis(), static_cast<unsigned>(version));
    }
    readFile.close();
  }

  // Construct the new/updated entry. If contentHint==0 (caller didn't
  // supply one) we preserve the prior entry's hint when updating.
  Entry newEntry;
  newEntry.pathHash = hash;
  newEntry.currentPage = currentPage;
  newEntry.totalPages = totalPages;
  newEntry.contentHint = contentHint;

  bool updated = false;
  for (auto& e : entries) {
    if (e.pathHash == hash) {
      if (contentHint == 0) newEntry.contentHint = e.contentHint;
      e = newEntry;
      updated = true;
      break;
    }
  }
  if (!updated) {
    if (entries.size() >= static_cast<size_t>(MAX_ENTRIES)) {
      // Drop oldest to make room — matches pre-Batch-9 behaviour.
      entries.erase(entries.begin());
    }
    entries.push_back(newEntry);
  }

  // Atomic write: helper opens <INDEX_PATH>.tmp; on commit, performs the
  // canonical→.bak, .tmp→canonical, drop-.bak rotation documented in
  // docs/ATOMIC_WRITE_DESIGN.md. Power loss at any intermediate point
  // is recovered by recoverAtomicWrites() at next boot — the previous
  // unsafe remove+rename pair could leave the library file missing.
  FsFile writeFile;
  if (!core.storage.atomicOpenWrite(INDEX_PATH, writeFile).ok()) {
    Serial.println("[LIBIDX] atomicOpenWrite failed");
    return false;
  }

  Crc32 wcrc;
  writeFile.write(&VERSION, 1);
  wcrc.update(&VERSION, 1);
  uint16_t cnt = static_cast<uint16_t>(entries.size());
  writeFile.write(reinterpret_cast<const uint8_t*>(&cnt), 2);
  wcrc.update(&cnt, 2);
  for (const auto& e : entries) {
    writeFile.write(reinterpret_cast<const uint8_t*>(&e), sizeof(Entry));
    wcrc.update(&e, sizeof(Entry));
  }
  // CRC32 trailer (v3+). Audit #26 follow-up.
  const uint32_t trailer = wcrc.finalize();
  writeFile.write(reinterpret_cast<const uint8_t*>(&trailer), 4);

  if (!core.storage.atomicCommit(writeFile, INDEX_PATH).ok()) {
    Serial.println("[LIBIDX] atomicCommit failed");
    core.storage.atomicAbort(writeFile, INDEX_PATH);
    return false;
  }

  Serial.printf("[LIBIDX] Updated: hash=%u page=%u/%u (%zu entries)\n",
                hash, currentPage, totalPages, entries.size());
  return true;
}

int16_t LibraryIndex::getProgress(Core& core, const char* bookPath) {
  if (!bookPath || bookPath[0] == '\0') return -1;

  const uint32_t hash = hashPath(bookPath);

  FsFile file;
  auto result = core.storage.openRead(INDEX_PATH, file);
  if (!result.ok()) return -1;

  uint8_t version;
  // Accept v2 (no CRC) and v3+ (CRC trailer present but unused on these
  // read-only paths — they don't track CRC, just consume entries).
  if (file.read(&version, 1) != 1 || version < 2 || version > VERSION) {
    file.close();
    return -1;
  }

  uint16_t count;
  if (file.read(reinterpret_cast<uint8_t*>(&count), 2) != 2) {
    file.close();
    return -1;
  }
  if (count > MAX_ENTRIES) count = MAX_ENTRIES;

  Entry entry;
  for (uint16_t i = 0; i < count; i++) {
    if (file.read(reinterpret_cast<uint8_t*>(&entry), sizeof(Entry)) != sizeof(Entry)) break;
    if (entry.pathHash == hash) {
      file.close();
      // progressPercent() clamps to [0, 100], so int16_t fits.
      return entry.progressPercent();
    }
  }

  file.close();
  return -1;
}

int LibraryIndex::loadAll(Core& core, Entry* entries, int maxEntries) {
  FsFile file;
  auto result = core.storage.openRead(INDEX_PATH, file);
  if (!result.ok()) return 0;

  uint8_t version;
  // Accept v2 (no CRC) and v3+ (CRC trailer present but unused on these
  // read-only paths — they don't track CRC, just consume entries).
  if (file.read(&version, 1) != 1 || version < 2 || version > VERSION) {
    file.close();
    return 0;
  }

  uint16_t count;
  if (file.read(reinterpret_cast<uint8_t*>(&count), 2) != 2) {
    file.close();
    return 0;
  }
  if (count > MAX_ENTRIES) count = MAX_ENTRIES;

  int toRead = (count < maxEntries) ? count : maxEntries;
  int actual = 0;
  for (int i = 0; i < toRead; i++) {
    if (file.read(reinterpret_cast<uint8_t*>(&entries[i]), sizeof(Entry)) == sizeof(Entry)) {
      actual++;
    } else {
      break;
    }
  }

  file.close();
  return actual;
}

bool LibraryIndex::findByHash(Core& core, uint32_t hash, Entry& entry) {
  FsFile file;
  auto result = core.storage.openRead(INDEX_PATH, file);
  if (!result.ok()) return false;

  uint8_t version;
  // Accept v2 (no CRC) and v3+ (CRC trailer present but unused on these
  // read-only paths — they don't track CRC, just consume entries).
  if (file.read(&version, 1) != 1 || version < 2 || version > VERSION) {
    file.close();
    return false;
  }

  uint16_t count;
  if (file.read(reinterpret_cast<uint8_t*>(&count), 2) != 2) {
    file.close();
    return false;
  }
  if (count > MAX_ENTRIES) count = MAX_ENTRIES;

  Entry temp;
  for (uint16_t i = 0; i < count; i++) {
    if (file.read(reinterpret_cast<uint8_t*>(&temp), sizeof(Entry)) != sizeof(Entry)) break;
    if (temp.pathHash == hash) {
      entry = temp;
      file.close();
      return true;
    }
  }

  file.close();
  return false;
}

}  // namespace sumi

#include "RecentBooks.h"

#include <Arduino.h>
#include <Crc32.h>
#include <SDCardManager.h>
#include <Utf8.h>

#include "../core/Core.h"

namespace sumi {

namespace {
// Shared writer used by recordOpen / updateProgress / updateThumbPath:
// version byte + count byte + (count × Entry) + 4-byte CRC32 trailer
// over the preceding bytes. Audit #26 follow-up.
void writeRecentFile(FsFile& file, const RecentBooks::Entry* entries,
                     uint8_t count) {
  using Entry = RecentBooks::Entry;
  Crc32 c;
  const uint8_t version = 3;  // RecentBooks::VERSION
  file.write(version);
  c.update(&version, 1);
  file.write(count);
  c.update(&count, 1);
  if (count > 0) {
    file.write(reinterpret_cast<const uint8_t*>(entries),
               sizeof(Entry) * count);
    c.update(entries, sizeof(Entry) * count);
  }
  const uint32_t trailer = c.finalize();
  file.write(reinterpret_cast<const uint8_t*>(&trailer), 4);
}
}  // namespace

void RecentBooks::recordOpen(Core& core, const char* path, const char* title,
                             const char* author, uint16_t progress,
                             const char* thumbPath) {
  constexpr int MAX_TRACK = MAX_RECENT;
  Entry entries[MAX_TRACK];
  int count = loadAll(core, entries, MAX_TRACK);

  // Check if already in list
  int existingIdx = -1;
  for (int i = 0; i < count; i++) {
    if (strcmp(entries[i].path, path) == 0) {
      existingIdx = i;
      break;
    }
  }

  // Create new entry. utf8SafeCopy keeps title/author from being sliced
  // mid-codepoint — a long CJK book title truncated with strncpy would
  // end in a broken 3-byte sequence and render as '?' in the Recent list.
  Entry newEntry = {};
  utf8SafeCopy(newEntry.path, path, PATH_LEN);
  utf8SafeCopy(newEntry.title, title, TITLE_LEN);
  utf8SafeCopy(newEntry.author, author, AUTHOR_LEN);
  newEntry.lastAccess = 0;  // No RTC; ordering maintained by file position (newest first)
  newEntry.progress = progress;
  // Persist thumbnail: use new path if provided, else preserve existing
  if (thumbPath && thumbPath[0] != '\0') {
    utf8SafeCopy(newEntry.thumbPath, thumbPath, THUMB_LEN);
  } else if (existingIdx >= 0 && entries[existingIdx].hasThumb()) {
    utf8SafeCopy(newEntry.thumbPath, entries[existingIdx].thumbPath, THUMB_LEN);
  } else {
    newEntry.thumbPath[0] = '\0';
  }
  
  // Build the new entries array in memory: new entry first, then
  // existing entries (skipping the duplicate if present). One write
  // pass via writeRecentFile() emits the v3 format with CRC32 trailer.
  Entry merged[MAX_TRACK];
  merged[0] = newEntry;
  int written = 1;
  for (int i = 0; i < count && written < MAX_TRACK; i++) {
    if (i != existingIdx) {
      merged[written++] = entries[i];
    }
  }

  // Atomic write — pre-audit this used regular openFileForWrite
  // (truncate + write) plus a CRC trailer. The CRC catches bit-flips but
  // not partial writes; a power loss between O_TRUNC and the first byte
  // landed left the canonical recent.bin empty, the next boot read 0
  // entries, and the home carousel was empty until the user re-opened a
  // book. atomicOpenWrite + atomicCommit closes both windows uniformly
  // with the rest of /.sumi/*.
  FsFile file;
  if (!SdMan.atomicOpenWrite("RECENT", INDEX_PATH, file)) {
    Serial.println("[RECENT] atomicOpenWrite failed");
    return;
  }
  writeRecentFile(file, merged, static_cast<uint8_t>(written));
  if (!SdMan.atomicCommit(file, INDEX_PATH)) {
    SdMan.atomicAbort(file, INDEX_PATH);
    Serial.println("[RECENT] atomicCommit failed");
    return;
  }

  Serial.printf("[RECENT] Recorded: %s (%d entries total)\n", title, written);
}

void RecentBooks::updateProgress(Core& core, const char* path, uint16_t progress) {
  constexpr int MAX_TRACK = MAX_RECENT;
  Entry entries[MAX_TRACK];
  int count = loadAll(core, entries, MAX_TRACK);
  
  // Find and update
  bool found = false;
  for (int i = 0; i < count; i++) {
    if (strcmp(entries[i].path, path) == 0) {
      entries[i].progress = progress;
      found = true;
      break;
    }
  }
  
  if (!found) return;

  // Write back via the shared writer (emits v3 + CRC trailer) + atomic
  // rotation so a power loss can't trash the recent list.
  FsFile file;
  if (!SdMan.atomicOpenWrite("RECENT", INDEX_PATH, file)) return;
  writeRecentFile(file, entries, static_cast<uint8_t>(count));
  if (!SdMan.atomicCommit(file, INDEX_PATH)) {
    SdMan.atomicAbort(file, INDEX_PATH);
  }
}

int RecentBooks::loadAll(Core& core, Entry* entries, int maxEntries) {
  FsFile file;
  if (!SdMan.openFileForRead("RECENT", INDEX_PATH, file)) {
    return 0;
  }

  // Accept v2 (pre-trailer) and v3 (CRC trailer). v3 reads still
  // tolerate v2 — they get migrated on next write.
  Crc32 crc;
  uint8_t version = file.read();
  if (version < 2 || version > VERSION) {
    file.close();
    return 0;
  }
  crc.update(&version, 1);

  uint8_t count = file.read();
  crc.update(&count, 1);
  // Cap BEFORE we use `count` to bound the read; clamping after the
  // read would still let a malicious file ask for too many bytes.
  uint8_t requested = count;  // file's claimed count, for CRC parity
  if (count > MAX_RECENT) count = MAX_RECENT;
  if (count > maxEntries) count = static_cast<uint8_t>(maxEntries);

  int bytesRead = file.read(reinterpret_cast<uint8_t*>(entries), sizeof(Entry) * count);
  if (bytesRead == static_cast<int>(sizeof(Entry) * count)) {
    crc.update(entries, sizeof(Entry) * count);
  }

  // Verify CRC32 trailer for v3+. Tolerant per audit policy: a
  // mismatch logs and the entries we've already read still come back.
  // If `requested` was capped (count != requested) we can't faithfully
  // compute the original CRC anyway — skip the check rather than warn.
  if (version >= VERSION_WITH_CRC && requested == count) {
    uint32_t fileCrc = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&fileCrc), 4) == 4) {
      const uint32_t computed = crc.finalize();
      if (fileCrc != computed) {
        Serial.printf("[RECENT] WARN: recent.bin CRC32 mismatch "
                      "(file=0x%08X computed=0x%08X); accepting payload\n",
                      static_cast<unsigned>(fileCrc),
                      static_cast<unsigned>(computed));
      }
    } else {
      Serial.printf("[RECENT] WARN: recent.bin v%u missing CRC32 trailer\n",
                    static_cast<unsigned>(version));
    }
  }

  file.close();
  if (bytesRead != static_cast<int>(sizeof(Entry) * count)) {
    return 0;
  }
  
  // Filter out entries whose files no longer exist
  int validCount = 0;
  for (int i = 0; i < count; i++) {
    if (!entries[i].isEmpty() && SdMan.exists(entries[i].path)) {
      if (i != validCount) {
        entries[validCount] = entries[i];
      }
      validCount++;
    }
  }
  
  return validCount;
}

bool RecentBooks::getMostRecent(Core& core, Entry& entry) {
  Entry entries[1];
  int count = loadAll(core, entries, 1);
  if (count > 0) {
    entry = entries[0];
    return true;
  }
  return false;
}

void RecentBooks::updateThumbPath(Core& core, const char* path, const char* thumbPath) {
  if (!thumbPath || thumbPath[0] == '\0') return;

  constexpr int MAX_TRACK = MAX_RECENT;
  Entry entries[MAX_TRACK];
  int count = loadAll(core, entries, MAX_TRACK);

  // Find and update
  bool found = false;
  for (int i = 0; i < count; i++) {
    if (strcmp(entries[i].path, path) == 0) {
      // Only write if thumbnail changed
      if (strcmp(entries[i].thumbPath, thumbPath) == 0) return;
      utf8SafeCopy(entries[i].thumbPath, thumbPath, THUMB_LEN);
      found = true;
      break;
    }
  }

  if (!found) return;

  // Write back via the shared writer + atomic rotation. Pre-audit
  // pass this third writer was inconsistent: bypassed `writeRecentFile`
  // (so the CRC trailer wasn't written, making subsequent loads always
  // log a missing-trailer warning), and used regular openFileForWrite
  // which lost data on power loss. Now matches recordOpen and
  // updateProgress.
  FsFile file;
  if (!SdMan.atomicOpenWrite("RECENT", INDEX_PATH, file)) return;
  writeRecentFile(file, entries, static_cast<uint8_t>(count));
  if (!SdMan.atomicCommit(file, INDEX_PATH)) {
    SdMan.atomicAbort(file, INDEX_PATH);
  }
}

void RecentBooks::clear(Core& core) {
  SdMan.remove(INDEX_PATH);
  Serial.println("[RECENT] Cleared all recent books");
}

}  // namespace sumi

#include "ReadingStats.h"

#include <Arduino.h>
#include <Crc32.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <SumiClock.h>

#include <algorithm>
#include <cstring>

namespace sumi {

namespace {
// writePod + Crc32 update in one call. Used by save() to mirror every
// emitted byte into the running CRC.
template <typename T>
inline void writePodC(FsFile& f, Crc32& c, const T& v) {
  serialization::writePod(f, v);
  c.update(&v, sizeof(T));
}

// readPod + Crc32 update. CRC is updated over what's actually on disk
// (the destination, post-read). Mirror of save's write order.
template <typename T>
inline void readPodC(FsFile& f, Crc32& c, T& v) {
  serialization::readPod(f, v);
  c.update(&v, sizeof(T));
}
template <typename T>
inline bool readPodCheckedC(FsFile& f, Crc32& c, T& v) {
  if (!serialization::readPodChecked(f, v)) return false;
  c.update(&v, sizeof(T));
  return true;
}
}  // namespace

// ── FNV-1a 32-bit hash ──────────────────────────────────────
uint32_t ReadingStats::hashPath(const char* path) {
  uint32_t hash = 2166136261u;  // FNV offset basis
  while (*path) {
    hash ^= static_cast<uint8_t>(*path++);
    hash *= 16777619u;  // FNV prime
  }
  return hash;
}

// ── Session tracking ─────────────────────────────────────────
void ReadingStats::startSession(uint32_t pathHash) {
  if (inSession_) {
    endSession();  // Close any stale session first
  }

  sessionPathHash_ = pathHash;
  sessionStartMs_ = millis();
  inSession_ = true;

  // Ensure book entry exists
  int idx = findOrCreateBook(pathHash);
  if (idx >= 0) {
    bookStats_[idx].sessions++;
  }
  totalSessions++;

  Serial.printf("[RSTATS] Session started (hash=%08X)\n", pathHash);
}

void ReadingStats::endSession() {
  if (!inSession_) return;

  uint32_t elapsed = millis() - sessionStartMs_;
  inSession_ = false;

  // Clamp: reject sessions > 24 hours (likely millis() wrapped or bug)
  constexpr uint32_t kMaxSessionMs = 24UL * 60 * 60 * 1000;
  if (elapsed > kMaxSessionMs) {
    elapsed = 0;
  }

  // Update book stats
  int idx = findBook(sessionPathHash_);
  if (idx >= 0) {
    bookStats_[idx].totalReadingMs += elapsed;
    bookStats_[idx].lastReadEpoch = SumiClock::getEpoch();  // real wall-clock if synced, else 0
  }

  // Update global stats
  totalReadingMs += elapsed;

  // Record into daily log if clock is synced
  if (elapsed > 0) {
    recordDailyReading(elapsed);
  }

  Serial.printf("[RSTATS] Session ended: %lu ms (hash=%08X)\n",
                (unsigned long)elapsed, sessionPathHash_);

  save();
}

void ReadingStats::recordPageTurn() {
  if (!inSession_) return;

  int idx = findBook(sessionPathHash_);
  if (idx >= 0 && bookStats_[idx].pagesRead < UINT16_MAX) {
    bookStats_[idx].pagesRead++;
  }
  if (totalPagesRead < UINT16_MAX) {
    totalPagesRead++;
  }
  // Don't save on every page turn -- deferred to endSession()
}

void ReadingStats::recordBookFinished() {
  if (booksFinished < UINT16_MAX) {
    booksFinished++;
  }
  save();
}

// ── Persistence ──────────────────────────────────────────────

// Binary format:
//   [4] magic = "SRST"
//   [1] version = 1
//   [4] totalReadingMs
//   [2] totalSessions
//   [2] totalPagesRead
//   [2] booksStarted
//   [2] booksFinished
//   [2] currentStreak
//   [2] longestStreak
//   [1] bookCount
//   [N * sizeof(BookStat)] bookStats array

static constexpr uint32_t kMagic = 0x54535253;  // "SRST" little-endian
// v2 adds daily log; v3 appends 4-byte CRC32 trailer over all preceding
// bytes (audit #26 follow-up). v3 reads still tolerate v2 (no trailer
// check); migration happens on next save.
static constexpr uint8_t kVersion = 3;
static constexpr uint8_t kVersionWithCrc = 3;

bool ReadingStats::load() {
  FsFile file;
  if (!SdMan.openFileForRead("RST", STATS_PATH, file)) {
    Serial.println("[RSTATS] No stats file, starting fresh");
    return false;
  }

  Crc32 crc;

  // Magic
  uint32_t magic = 0;
  if (!readPodCheckedC(file, crc, magic) || magic != kMagic) {
    Serial.println("[RSTATS] Bad magic, discarding");
    file.close();
    return false;
  }

  // Version (accept v1, v2, v3 — v3+ has CRC trailer)
  uint8_t version = 0;
  if (!readPodCheckedC(file, crc, version) || version < 1 || version > kVersion) {
    Serial.printf("[RSTATS] Unknown version %u, discarding\n", version);
    file.close();
    return false;
  }

  // Global stats
  readPodC(file, crc, totalReadingMs);
  readPodC(file, crc, totalSessions);
  readPodC(file, crc, totalPagesRead);
  readPodC(file, crc, booksStarted);
  readPodC(file, crc, booksFinished);
  readPodC(file, crc, currentStreak);
  readPodC(file, crc, longestStreak);

  // Book count -- bound-check for 380KB heap safety
  uint8_t count = 0;
  readPodC(file, crc, count);
  if (count > MAX_BOOK_STATS) {
    Serial.printf("[RSTATS] bookCount %u exceeds max %d, capping\n", count, MAX_BOOK_STATS);
    count = MAX_BOOK_STATS;
  }
  bookCount_ = count;

  // Book stats array
  for (int i = 0; i < bookCount_; i++) {
    int n = file.read(reinterpret_cast<uint8_t*>(&bookStats_[i]), sizeof(BookStat));
    if (n != static_cast<int>(sizeof(BookStat))) {
      Serial.printf("[RSTATS] Short read at book %d, truncating\n", i);
      bookCount_ = i;
      break;
    }
    crc.update(&bookStats_[i], sizeof(BookStat));
  }

  // Daily log (v2+)
  dailyLogCount_ = 0;
  if (version >= 2) {
    uint8_t dailyCount = 0;
    readPodC(file, crc, dailyCount);
    if (dailyCount > MAX_DAILY_ENTRIES) {
      Serial.printf("[RSTATS] dailyCount %u exceeds max %d, capping\n",
                    dailyCount, MAX_DAILY_ENTRIES);
      dailyCount = MAX_DAILY_ENTRIES;
    }
    dailyLogCount_ = dailyCount;
    for (int i = 0; i < dailyLogCount_; i++) {
      int n = file.read(reinterpret_cast<uint8_t*>(&dailyLog_[i]),
                        sizeof(DailyEntry));
      if (n != static_cast<int>(sizeof(DailyEntry))) {
        Serial.printf("[RSTATS] Short read at daily entry %d, truncating\n", i);
        dailyLogCount_ = i;
        break;
      }
      crc.update(&dailyLog_[i], sizeof(DailyEntry));
    }
  }

  // CRC32 trailer (v3+). Tolerant per audit policy: a mismatch logs
  // and the data we already loaded stays.
  if (version >= kVersionWithCrc) {
    uint32_t fileCrc = 0;
    if (serialization::readPodChecked(file, fileCrc)) {
      const uint32_t computed = crc.finalize();
      if (fileCrc != computed) {
        Serial.printf("[RSTATS] WARN: reading_stats.bin CRC32 mismatch "
                      "(file=0x%08X computed=0x%08X); accepting payload\n",
                      static_cast<unsigned>(fileCrc),
                      static_cast<unsigned>(computed));
      }
    } else {
      Serial.printf("[RSTATS] WARN: reading_stats.bin v%u missing CRC32 trailer\n",
                    static_cast<unsigned>(version));
    }
  }

  file.close();
  Serial.printf("[RSTATS] Loaded: %u sessions, %lu min, %d books, %d daily entries (v%u%s)\n",
                totalSessions, (unsigned long)(totalReadingMs / 60000),
                bookCount_, dailyLogCount_,
                static_cast<unsigned>(version),
                version >= kVersionWithCrc ? " +crc" : "");
  return true;
}

bool ReadingStats::save() {
  SdMan.mkdir("/.sumi");

  // Atomic — pre-audit this used regular openFileForWrite plus
  // a CRC32 trailer (audit #26). The CRC catches bit-flips but not
  // partial writes; a power loss between O_TRUNC and the first byte
  // wiped totalReadingMs / totalSessions / streaks / daily log silently
  // and the next session-end appended fresh data over the corpse.
  // atomicOpenWrite + atomicCommit closes the partial-write window the
  // same way settings.bin / recent.bin / global_bookmarks.bin do.
  FsFile file;
  if (!SdMan.atomicOpenWrite("RST", STATS_PATH, file)) {
    Serial.println("[RSTATS] atomicOpenWrite failed");
    return false;
  }

  Crc32 crc;

  // Magic + version
  writePodC(file, crc, kMagic);
  writePodC(file, crc, kVersion);

  // Global stats
  writePodC(file, crc, totalReadingMs);
  writePodC(file, crc, totalSessions);
  writePodC(file, crc, totalPagesRead);
  writePodC(file, crc, booksStarted);
  writePodC(file, crc, booksFinished);
  writePodC(file, crc, currentStreak);
  writePodC(file, crc, longestStreak);

  // Book count + array
  uint8_t count = static_cast<uint8_t>(bookCount_);
  writePodC(file, crc, count);
  for (int i = 0; i < bookCount_; i++) {
    file.write(reinterpret_cast<const uint8_t*>(&bookStats_[i]), sizeof(BookStat));
    crc.update(&bookStats_[i], sizeof(BookStat));
  }

  // Daily log (v2)
  uint8_t dailyCount = static_cast<uint8_t>(dailyLogCount_);
  writePodC(file, crc, dailyCount);
  for (int i = 0; i < dailyLogCount_; i++) {
    file.write(reinterpret_cast<const uint8_t*>(&dailyLog_[i]),
               sizeof(DailyEntry));
    crc.update(&dailyLog_[i], sizeof(DailyEntry));
  }

  // CRC32 trailer (v3+). Audit #26 follow-up.
  const uint32_t trailer = crc.finalize();
  serialization::writePod(file, trailer);

  if (!SdMan.atomicCommit(file, STATS_PATH)) {
    SdMan.atomicAbort(file, STATS_PATH);
    Serial.println("[RSTATS] atomicCommit failed");
    return false;
  }
  return true;
}

// ── Queries ──────────────────────────────────────────────────
uint32_t ReadingStats::getBookReadingMs(uint32_t pathHash) const {
  int idx = findBook(pathHash);
  return idx >= 0 ? bookStats_[idx].totalReadingMs : 0;
}

uint16_t ReadingStats::getBookSessions(uint32_t pathHash) const {
  int idx = findBook(pathHash);
  return idx >= 0 ? bookStats_[idx].sessions : 0;
}

uint16_t ReadingStats::getBookPages(uint32_t pathHash) const {
  int idx = findBook(pathHash);
  return idx >= 0 ? bookStats_[idx].pagesRead : 0;
}

// ── Internal helpers ─────────────────────────────────────────
int ReadingStats::findBook(uint32_t hash) const {
  for (int i = 0; i < bookCount_; i++) {
    if (bookStats_[i].pathHash == hash) {
      return i;
    }
  }
  return -1;
}

int ReadingStats::findOrCreateBook(uint32_t hash) {
  int idx = findBook(hash);
  if (idx >= 0) return idx;

  // Need to create a new entry
  if (bookCount_ < MAX_BOOK_STATS) {
    idx = bookCount_++;
  } else {
    // Evict LRU: find the entry with the oldest lastReadEpoch
    idx = 0;
    uint32_t oldest = bookStats_[0].lastReadEpoch;
    for (int i = 1; i < bookCount_; i++) {
      if (bookStats_[i].lastReadEpoch < oldest) {
        oldest = bookStats_[i].lastReadEpoch;
        idx = i;
      }
    }
    Serial.printf("[RSTATS] Evicting LRU book slot %d (hash=%08X)\n",
                  idx, bookStats_[idx].pathHash);
  }

  // Initialize the new entry
  memset(&bookStats_[idx], 0, sizeof(BookStat));
  bookStats_[idx].pathHash = hash;

  if (booksStarted < UINT16_MAX) {
    booksStarted++;
  }

  return idx;
}

// ── Daily log helpers ────────────────────────────────────────
uint16_t ReadingStats::epochToDayNumber(uint32_t epoch) {
  if (epoch < EPOCH_BASE) return 0;
  return static_cast<uint16_t>((epoch - EPOCH_BASE) / 86400u);
}

void ReadingStats::recordDailyReading(uint32_t elapsedMs) {
  uint32_t epoch = SumiClock::getEpoch();
  if (epoch == 0) return;  // no clock, skip

  uint16_t dayNum = epochToDayNumber(epoch);
  uint16_t minutes = static_cast<uint16_t>(elapsedMs / 60000);
  if (minutes == 0 && elapsedMs > 0) minutes = 1;  // round up sub-minute

  // Search for existing entry for this day
  for (int i = 0; i < dailyLogCount_; i++) {
    if (dailyLog_[i].dayNumber == dayNum) {
      // Add to existing entry, clamp at UINT16_MAX
      uint32_t total = dailyLog_[i].readingMinutes + minutes;
      dailyLog_[i].readingMinutes =
          total > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(total);
      return;
    }
  }

  // New day entry
  if (dailyLogCount_ < MAX_DAILY_ENTRIES) {
    dailyLog_[dailyLogCount_++] = {dayNum, minutes};
  } else {
    // Circular eviction: replace the oldest entry
    int oldest = 0;
    for (int i = 1; i < dailyLogCount_; i++) {
      if (dailyLog_[i].dayNumber < dailyLog_[oldest].dayNumber) {
        oldest = i;
      }
    }
    dailyLog_[oldest] = {dayNum, minutes};
  }
}

uint16_t ReadingStats::getDailyMinutes(uint16_t dayNumber) const {
  for (int i = 0; i < dailyLogCount_; i++) {
    if (dailyLog_[i].dayNumber == dayNumber) {
      return dailyLog_[i].readingMinutes;
    }
  }
  return 0;
}

}  // namespace sumi

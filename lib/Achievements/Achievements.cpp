#include "Achievements.h"

#include <Crc32.h>
#include <SDCardManager.h>

namespace sumi {

namespace {
// File format:
//   v1: magic 'SACH' (4) + version=1 (1) + unlocked_ (8) = 13 bytes
//   v2: above + 4-byte CRC32 trailer over the 13 bytes (audit #26 follow-up)
constexpr uint32_t ACH_MAGIC = 0x48434153u;  // 'SACH'
constexpr uint8_t  ACH_VERSION = 2;
constexpr uint8_t  ACH_VERSION_WITH_CRC = 2;
}  // namespace

// Achievement definitions — 40 achievements across 7 metrics
const AchievementDef Achievements::DEFS[ACHIEVEMENT_COUNT] = {
    // Books Started (5)
    {"First Steps", "Start your first book", AchievementMetric::BooksStarted, 1},
    {"Curious Reader", "Start 5 books", AchievementMetric::BooksStarted, 5},
    {"Explorer", "Start 10 books", AchievementMetric::BooksStarted, 10},
    {"Adventurer", "Start 25 books", AchievementMetric::BooksStarted, 25},
    {"Library Builder", "Start 50 books", AchievementMetric::BooksStarted, 50},

    // Books Finished (7)
    {"First Finish", "Finish your first book", AchievementMetric::BooksFinished, 1},
    {"Getting Started", "Finish 2 books", AchievementMetric::BooksFinished, 2},
    {"Bookworm", "Finish 5 books", AchievementMetric::BooksFinished, 5},
    {"Avid Reader", "Finish 10 books", AchievementMetric::BooksFinished, 10},
    {"Page Turner", "Finish 15 books", AchievementMetric::BooksFinished, 15},
    {"Scholar", "Finish 25 books", AchievementMetric::BooksFinished, 25},
    {"Master Reader", "Finish 50 books", AchievementMetric::BooksFinished, 50},

    // Sessions (5)
    {"First Session", "Complete your first reading session", AchievementMetric::Sessions, 1},
    {"Regular Reader", "Complete 10 sessions", AchievementMetric::Sessions, 10},
    {"Dedicated", "Complete 25 sessions", AchievementMetric::Sessions, 25},
    {"Committed", "Complete 50 sessions", AchievementMetric::Sessions, 50},
    {"Devoted", "Complete 100 sessions", AchievementMetric::Sessions, 100},

    // Total Reading Time (7)
    {"One Hour", "Read for 1 hour total", AchievementMetric::TotalReadingMinutes, 60},
    {"Five Hours", "Read for 5 hours total", AchievementMetric::TotalReadingMinutes, 300},
    {"Ten Hours", "Read for 10 hours total", AchievementMetric::TotalReadingMinutes, 600},
    {"Full Day", "Read for 24 hours total", AchievementMetric::TotalReadingMinutes, 1440},
    {"Marathon", "Read for 50 hours total", AchievementMetric::TotalReadingMinutes, 3000},
    {"Century", "Read for 100 hours total", AchievementMetric::TotalReadingMinutes, 6000},
    {"Legendary", "Read for 200 hours total", AchievementMetric::TotalReadingMinutes, 12000},

    // Pages Read (5)
    {"First Page", "Read your first page", AchievementMetric::PagesRead, 1},
    {"Chapter Done", "Read 50 pages", AchievementMetric::PagesRead, 50},
    {"Hundred Pages", "Read 100 pages", AchievementMetric::PagesRead, 100},
    {"Five Hundred", "Read 500 pages", AchievementMetric::PagesRead, 500},
    {"Thousand Pages", "Read 1000 pages", AchievementMetric::PagesRead, 1000},

    // Bookmarks (4)
    {"First Bookmark", "Add your first bookmark", AchievementMetric::BookmarksAdded, 1},
    {"Collector", "Add 10 bookmarks", AchievementMetric::BookmarksAdded, 10},
    {"Curator", "Add 25 bookmarks", AchievementMetric::BookmarksAdded, 25},
    {"Archivist", "Add 50 bookmarks", AchievementMetric::BookmarksAdded, 50},

    // Max Session Length (7)
    {"Quick Read", "Read for 15 minutes straight", AchievementMetric::MaxSessionMinutes, 15},
    {"Half Hour", "Read for 30 minutes straight", AchievementMetric::MaxSessionMinutes, 30},
    {"Deep Read", "Read for 45 minutes straight", AchievementMetric::MaxSessionMinutes, 45},
    {"Hour Session", "Read for 1 hour straight", AchievementMetric::MaxSessionMinutes, 60},
    {"Extended", "Read for 90 minutes straight", AchievementMetric::MaxSessionMinutes, 90},
    {"Two Hours", "Read for 2 hours straight", AchievementMetric::MaxSessionMinutes, 120},
    {"Unstoppable", "Read for 3 hours straight", AchievementMetric::MaxSessionMinutes, 180},
};

uint64_t Achievements::checkAndUnlock(uint32_t booksStarted, uint32_t booksFinished,
                                       uint32_t sessions, uint32_t totalReadingMinutes,
                                       uint32_t pagesRead, uint32_t bookmarksAdded,
                                       uint32_t maxSessionMinutes) {
  uint64_t newlyUnlocked = 0;

  for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
    if (isUnlocked(i)) continue;  // already unlocked

    const auto& def = DEFS[i];
    uint32_t currentValue = 0;
    switch (def.metric) {
      case AchievementMetric::BooksStarted:       currentValue = booksStarted; break;
      case AchievementMetric::BooksFinished:       currentValue = booksFinished; break;
      case AchievementMetric::Sessions:            currentValue = sessions; break;
      case AchievementMetric::TotalReadingMinutes: currentValue = totalReadingMinutes; break;
      case AchievementMetric::PagesRead:           currentValue = pagesRead; break;
      case AchievementMetric::BookmarksAdded:      currentValue = bookmarksAdded; break;
      case AchievementMetric::MaxSessionMinutes:   currentValue = maxSessionMinutes; break;
      default: continue;
    }

    if (currentValue >= def.threshold) {
      unlocked_ |= (1ULL << i);
      newlyUnlocked |= (1ULL << i);
      queuePopup(i);
    }
  }

  if (newlyUnlocked) {
    save();
  }

  return newlyUnlocked;
}

bool Achievements::load() {
  FsFile f;
  if (!SdMan.openFileForRead("ACH", SAVE_PATH, f)) return false;

  Crc32 crc;
  uint32_t magic = 0;
  int n = f.read(reinterpret_cast<uint8_t*>(&magic), 4);
  if (n != 4 || magic != ACH_MAGIC) {
    f.close();
    return false;
  }
  crc.update(&magic, 4);

  uint8_t version = 0;
  n = f.read(&version, 1);
  // Accept v1 (no CRC) and v2 (CRC trailer). v2 reads still tolerate
  // v1 files — they get migrated on next save.
  if (n != 1 || version < 1 || version > ACH_VERSION) {
    f.close();
    return false;
  }
  crc.update(&version, 1);

  n = f.read(reinterpret_cast<uint8_t*>(&unlocked_), sizeof(unlocked_));
  if (n != static_cast<int>(sizeof(unlocked_))) {
    f.close();
    unlocked_ = 0;
    return false;
  }
  crc.update(&unlocked_, sizeof(unlocked_));

  // CRC32 trailer (v2+). Tolerant per audit policy.
  if (version >= ACH_VERSION_WITH_CRC) {
    uint32_t fileCrc = 0;
    if (f.read(reinterpret_cast<uint8_t*>(&fileCrc), 4) == 4) {
      const uint32_t computed = crc.finalize();
      if (fileCrc != computed) {
        Serial.printf("[ACH] WARN: achievements.bin CRC32 mismatch "
                      "(file=0x%08X computed=0x%08X); accepting payload\n",
                      static_cast<unsigned>(fileCrc),
                      static_cast<unsigned>(computed));
      }
    } else {
      Serial.printf("[ACH] WARN: achievements.bin v%u missing CRC32 trailer\n",
                    static_cast<unsigned>(version));
    }
  }

  f.close();
  return true;
}

bool Achievements::save() {
  SdMan.mkdir("/.sumi");

  // Atomic write — see docs/ATOMIC_WRITE_DESIGN.md. The previous
  // truncate-then-overwrite path left achievements.bin empty if power
  // was lost between O_TRUNC and the first byte written; load() would
  // then read defaults (no unlocked achievements) — silent user-data
  // loss.
  FsFile f;
  if (!SdMan.atomicOpenWrite("ACH", SAVE_PATH, f)) return false;

  Crc32 crc;
  const uint32_t magic = ACH_MAGIC;
  f.write(reinterpret_cast<const uint8_t*>(&magic), 4);
  crc.update(&magic, 4);
  const uint8_t version = ACH_VERSION;
  f.write(&version, 1);
  crc.update(&version, 1);
  f.write(reinterpret_cast<const uint8_t*>(&unlocked_), sizeof(unlocked_));
  crc.update(&unlocked_, sizeof(unlocked_));
  // CRC32 trailer (v2+). Audit #26 follow-up.
  const uint32_t trailer = crc.finalize();
  f.write(reinterpret_cast<const uint8_t*>(&trailer), 4);

  if (!SdMan.atomicCommit(f, SAVE_PATH)) {
    SdMan.atomicAbort(f, SAVE_PATH);
    return false;
  }
  return true;
}

}  // namespace sumi

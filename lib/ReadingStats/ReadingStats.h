#pragma once
#include <cstdint>

namespace sumi {

// Simple reading statistics tracker.
// Persists to /.sumi/reading_stats.bin as a compact binary format.
// Designed for ESP32-C3's 380KB heap -- no JSON, no dynamic containers.
class ReadingStats {
 public:
  static constexpr int MAX_BOOK_STATS = 30;  // LRU eviction beyond this
  static constexpr const char* STATS_PATH = "/.sumi/reading_stats.bin";

  struct BookStat {
    uint32_t pathHash;          // FNV-1a hash of book path
    uint32_t totalReadingMs;    // cumulative reading time
    uint16_t sessions;          // number of reading sessions
    uint16_t pagesRead;         // pages turned (approximate)
    uint32_t lastReadEpoch;     // last read timestamp (0 if no clock)
  };

  // Global stats (always in RAM)
  uint32_t totalReadingMs = 0;
  uint16_t totalSessions = 0;
  uint16_t totalPagesRead = 0;
  uint16_t booksStarted = 0;
  uint16_t booksFinished = 0;
  uint16_t currentStreak = 0;   // consecutive days with reading
  uint16_t longestStreak = 0;

  // Session tracking
  void startSession(uint32_t pathHash);
  void endSession();                      // saves accumulated time
  void recordPageTurn();                  // increment page count
  void recordBookFinished();

  // Persistence
  bool load();
  bool save();

  // Queries
  uint32_t getBookReadingMs(uint32_t pathHash) const;
  uint16_t getBookSessions(uint32_t pathHash) const;
  uint16_t getBookPages(uint32_t pathHash) const;

  // Simple FNV-1a hash for book path
  static uint32_t hashPath(const char* path);

  // ── Daily reading log (circular buffer, last 90 days) ──
  // Epoch reference: 2024-01-01 00:00:00 UTC = 1704067200
  static constexpr uint32_t EPOCH_BASE = 1704067200u;
  static constexpr int MAX_DAILY_ENTRIES = 90;

  struct DailyEntry {
    uint16_t dayNumber;       // days since 2024-01-01
    uint16_t readingMinutes;  // total minutes read that day
  };

  DailyEntry dailyLog_[MAX_DAILY_ENTRIES] = {};
  int dailyLogCount_ = 0;

  // Record reading time for the current day (called from endSession)
  void recordDailyReading(uint32_t elapsedMs);

  // Query: get reading minutes for a specific dayNumber
  uint16_t getDailyMinutes(uint16_t dayNumber) const;

  // Convert epoch seconds to dayNumber (days since EPOCH_BASE)
  static uint16_t epochToDayNumber(uint32_t epoch);

  // Get the book stats array (read-only, for plugin display)
  const BookStat* getBookStats() const { return bookStats_; }
  int getBookCount() const { return bookCount_; }

 private:
  BookStat bookStats_[MAX_BOOK_STATS] = {};
  int bookCount_ = 0;

  // Current session
  uint32_t sessionStartMs_ = 0;
  uint32_t sessionPathHash_ = 0;
  bool inSession_ = false;

  int findBook(uint32_t hash) const;
  int findOrCreateBook(uint32_t hash);
};

}  // namespace sumi

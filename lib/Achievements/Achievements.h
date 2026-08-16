#pragma once

#include <cstdint>
#include <cstring>

namespace sumi {

// Achievement metric categories
enum class AchievementMetric : uint8_t {
  BooksStarted = 0,
  BooksFinished,
  Sessions,
  TotalReadingMinutes,
  PagesRead,
  BookmarksAdded,
  MaxSessionMinutes,
  COUNT
};

// Individual achievement definitions
struct AchievementDef {
  const char* name;
  const char* description;
  AchievementMetric metric;
  uint32_t threshold;
};

class Achievements {
public:
  // 40 achievements across 7 metrics
  static constexpr int ACHIEVEMENT_COUNT = 40;
  // unlocked_ is a uint64_t bitmask — one bit per achievement. If the
  // count grows past 64 a high-numbered achievement can never unlock
  // (silent shift-overflow). The static_assert turns that into a build
  // failure so the bug is caught at the line that introduces it.
  // Audit #48.
  static_assert(ACHIEVEMENT_COUNT <= 64,
                "ACHIEVEMENT_COUNT exceeds the 64-bit unlocked_ bitmask; "
                "widen unlocked_ to a larger type or split into multiple words");
  static constexpr const char* SAVE_PATH = "/.sumi/achievements.bin";

  static const AchievementDef DEFS[ACHIEVEMENT_COUNT];

  // Check which achievements are unlocked based on current stats
  // Returns bitmask of newly unlocked achievements (bits set = just unlocked)
  uint64_t checkAndUnlock(uint32_t booksStarted, uint32_t booksFinished,
                          uint32_t sessions, uint32_t totalReadingMinutes,
                          uint32_t pagesRead, uint32_t bookmarksAdded,
                          uint32_t maxSessionMinutes);

  // Get the unlock state
  bool isUnlocked(int index) const {
    if (index < 0 || index >= ACHIEVEMENT_COUNT) return false;
    return (unlocked_ >> index) & 1;
  }

  int unlockedCount() const {
    int count = 0;
    uint64_t bits = unlocked_;
    while (bits) { count += bits & 1; bits >>= 1; }
    return count;
  }

  // Persistence
  bool load();
  bool save();

  // Popup queue for newly unlocked achievements
  int popupQueue[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  int popupCount = 0;

  void queuePopup(int achievementIndex) {
    if (popupCount < 8) {
      popupQueue[popupCount++] = achievementIndex;
    }
  }

  int dequeuePopup() {
    if (popupCount == 0) return -1;
    int idx = popupQueue[0];
    for (int i = 1; i < popupCount; i++) popupQueue[i - 1] = popupQueue[i];
    popupCount--;
    return idx;
  }

private:
  uint64_t unlocked_ = 0;  // bitmask: bit N = achievement N unlocked
};

}  // namespace sumi

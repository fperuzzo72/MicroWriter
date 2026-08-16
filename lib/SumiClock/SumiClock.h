#pragma once
#include <cstdint>
#include <ctime>

namespace sumi {

// Simple wall-clock for SUMI. Time source: BLE sync from phone/browser.
// Survives deep sleep via ESP-IDF's RTC-backed system clock.
// Persists last-known time to NVS flash for graceful degradation
// after full power loss (RTC memory is lost on power cycle).
class SumiClock {
public:
  // Set the clock from an epoch timestamp (seconds since 1970-01-01 UTC)
  static void setTime(uint32_t epochSeconds);

  // Get current epoch seconds (0 if never synced)
  static uint32_t getEpoch();

  // Get formatted time string (HH:MM or HH:MM AM/PM)
  // Returns empty string if never synced
  static void getTimeStr(char* buf, int bufSize, bool use24h = true);

  // Get formatted date string (YYYY-MM-DD)
  static void getDateStr(char* buf, int bufSize);

  // Has the clock been synced at least once this boot?
  static bool isSynced();

  // Has the clock ever been synced? (uses NVS fallback)
  static bool hasTime();

  // Initialize: restore from RTC-backed system clock or NVS
  static void init();

  // Save current time to NVS flash (call before deep sleep)
  static void saveToFlash();

  // Timezone offset applied to formatted time/date strings.
  // Positive minutes east of UTC (e.g. +60 = UTC+1, -480 = UTC-8).
  // The epoch returned by getEpoch() stays UTC; only display formatters
  // shift. Default 0 keeps the pre-Batch-9 UTC display.
  // Audit #47.
  static void setTimeZoneOffsetMinutes(int16_t offsetMinutes);
  static int16_t getTimeZoneOffsetMinutes();

private:
  static bool synced_;    // synced this boot via BLE
  static bool hasTime_;   // has any time (even stale from NVS)
  static int16_t tzOffsetMinutes_;  // [-720..+840] inclusive
};

}  // namespace sumi

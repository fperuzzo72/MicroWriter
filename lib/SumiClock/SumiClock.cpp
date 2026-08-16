#include "SumiClock.h"

#include <Arduino.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>

// ESP-IDF's time() uses the RTC hardware timer which survives deep sleep
// on ESP32-C3. We rely on settimeofday()/time() instead of manual RTC
// memory structs — much simpler and the RTC timer keeps ticking during
// deep sleep automatically.

// Reasonable minimum epoch: 2024-01-01 00:00:00 UTC
static constexpr uint32_t MIN_VALID_EPOCH = 1704067200;

// NVS namespace and key
static constexpr const char* NVS_NAMESPACE = "sumiclock";
static constexpr const char* NVS_KEY_EPOCH = "epoch";

namespace sumi {

bool SumiClock::synced_ = false;
bool SumiClock::hasTime_ = false;
int16_t SumiClock::tzOffsetMinutes_ = 0;

void SumiClock::setTime(uint32_t epochSeconds) {
  if (epochSeconds < MIN_VALID_EPOCH) return;

  struct timeval tv;
  tv.tv_sec = epochSeconds;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  synced_ = true;
  hasTime_ = true;

  Serial.printf("[CLOCK] Time set: epoch=%u\n", epochSeconds);
}

uint32_t SumiClock::getEpoch() {
  if (!hasTime_) return 0;
  time_t now = time(nullptr);
  if (now < (time_t)MIN_VALID_EPOCH) return 0;
  return static_cast<uint32_t>(now);
}

void SumiClock::getTimeStr(char* buf, int bufSize, bool use24h) {
  if (bufSize <= 0) return;
  buf[0] = '\0';
  if (!hasTime_) return;

  uint32_t epoch = getEpoch();
  if (epoch == 0) return;

  // Apply tz offset on the formatting side; the epoch we store stays UTC.
  // gmtime_r is the reentrant variant — gmtime returns a pointer to a
  // shared static buffer, which corrupts under concurrent calls (e.g.
  // status bar render + plugin clock display). Audit #47.
  time_t t = static_cast<time_t>(epoch) + (static_cast<time_t>(tzOffsetMinutes_) * 60);
  struct tm tmBuf;
  if (!gmtime_r(&t, &tmBuf)) return;

  if (use24h) {
    snprintf(buf, bufSize, "%02d:%02d", tmBuf.tm_hour, tmBuf.tm_min);
  } else {
    int hour12 = tmBuf.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = tmBuf.tm_hour < 12 ? "AM" : "PM";
    snprintf(buf, bufSize, "%d:%02d %s", hour12, tmBuf.tm_min, ampm);
  }
}

void SumiClock::getDateStr(char* buf, int bufSize) {
  if (bufSize <= 0) return;
  buf[0] = '\0';
  if (!hasTime_) return;

  uint32_t epoch = getEpoch();
  if (epoch == 0) return;

  // Same tz + reentrant rationale as getTimeStr.
  time_t t = static_cast<time_t>(epoch) + (static_cast<time_t>(tzOffsetMinutes_) * 60);
  struct tm tmBuf;
  if (!gmtime_r(&t, &tmBuf)) return;

  snprintf(buf, bufSize, "%04d-%02d-%02d",
           tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday);
}

void SumiClock::setTimeZoneOffsetMinutes(int16_t offsetMinutes) {
  // Clamp to a safe IANA-ish range: -720 (UTC-12) to +840 (UTC+14).
  if (offsetMinutes < -720) offsetMinutes = -720;
  if (offsetMinutes > 840) offsetMinutes = 840;
  tzOffsetMinutes_ = offsetMinutes;
}

int16_t SumiClock::getTimeZoneOffsetMinutes() { return tzOffsetMinutes_; }

bool SumiClock::isSynced() { return synced_; }

bool SumiClock::hasTime() { return hasTime_; }

void SumiClock::init() {
  // Check if the RTC-backed system clock already has a valid time
  // (survives deep sleep, lost on full power cycle)
  time_t now = time(nullptr);
  if (now >= (time_t)MIN_VALID_EPOCH) {
    hasTime_ = true;
    Serial.printf("[CLOCK] Init: RTC time valid, epoch=%ld\n", (long)now);
    return;
  }

  // RTC time is stale (power cycle). Try to restore from NVS flash.
  // NVS reads are slow, so only do this once at boot.
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {  // read-only
    uint32_t savedEpoch = prefs.getULong(NVS_KEY_EPOCH, 0);
    prefs.end();

    if (savedEpoch >= MIN_VALID_EPOCH) {
      // Restore the saved time. It will be stale (frozen at the moment
      // we entered deep sleep) but better than nothing — the status bar
      // can show an approximate time until the next BLE sync.
      struct timeval tv;
      tv.tv_sec = savedEpoch;
      tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
      hasTime_ = true;
      Serial.printf("[CLOCK] Init: restored from NVS, epoch=%u (stale)\n", savedEpoch);
      return;
    }
  }

  Serial.println("[CLOCK] Init: no time available");
}

void SumiClock::saveToFlash() {
  uint32_t epoch = getEpoch();
  if (epoch == 0) return;

  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {  // read-write
    prefs.putULong(NVS_KEY_EPOCH, epoch);
    prefs.end();
    Serial.printf("[CLOCK] Saved to NVS: epoch=%u\n", epoch);
  }
}

}  // namespace sumi

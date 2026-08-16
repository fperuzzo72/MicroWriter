#pragma once

#include "../config.h"

#if FEATURE_PLUGINS

#include <ReadingStats.h>
#include <SumiClock.h>

#include <cstdio>
#include <ctime>

#include "PluginHelpers.h"
#include "PluginInterface.h"
#include "PluginRenderer.h"
#include "../ThemeManager.h"

// Defined in main.cpp
extern sumi::ReadingStats readingStats;

namespace sumi {

/**
 * @file ReadingStatsApp.h
 * @brief Reading activity visualizer for SUMI
 *
 * Two screens:
 *   1. Overview  -- total reading time, sessions, pages, books, streak
 *   2. Heatmap   -- monthly calendar with heat-colored cells per day
 *
 * Inspired by VCodex's ReadingHeatmapActivity.
 */
class ReadingStatsApp : public PluginInterface {
 public:
  explicit ReadingStatsApp(PluginRenderer& renderer) : renderer_(renderer) {}

  const char* name() const override { return "Reading Stats"; }
  PluginRunMode runMode() const override { return PluginRunMode::Simple; }

  void init(int screenW, int screenH) override {
    screenW_ = screenW;
    screenH_ = screenH;
    screen_ = Screen::Overview;
    needsFullRedraw = true;

    // Default the heatmap to the current month if the clock is synced
    uint32_t epoch = SumiClock::getEpoch();
    if (epoch > 0) {
      time_t t = static_cast<time_t>(epoch);
      struct tm* tm = gmtime(&t);
      if (tm) {
        heatmapYear_ = tm->tm_year + 1900;
        heatmapMonth_ = tm->tm_mon + 1;  // 1-based
      }
    }
  }

  void cleanup() override {}

  // ─── Input ───────────────────────────────────────────────────
  bool handleInput(PluginButton btn) override {
    switch (btn) {
      case PluginButton::Left:
        if (screen_ == Screen::Heatmap) {
          screen_ = Screen::Overview;
          needsFullRedraw = true;
          return true;
        }
        return true;  // consume on overview (no further left)

      case PluginButton::Right:
        if (screen_ == Screen::Overview) {
          screen_ = Screen::Heatmap;
          needsFullRedraw = true;
          return true;
        }
        return true;  // consume on heatmap

      case PluginButton::Up:
        if (screen_ == Screen::Heatmap) {
          prevMonth();
          needsFullRedraw = true;
          return true;
        }
        return true;

      case PluginButton::Down:
        if (screen_ == Screen::Heatmap) {
          nextMonth();
          needsFullRedraw = true;
          return true;
        }
        return true;

      case PluginButton::Back:
        return false;  // let host exit

      default:
        return true;
    }
  }

  // ─── Drawing ─────────────────────────────────────────────────
  void draw() override {
    if (screen_ == Screen::Overview) {
      drawOverview();
    } else {
      drawHeatmap();
    }
    needsFullRedraw = false;
  }

  void drawPartial() override { draw(); }

 private:
  enum class Screen : uint8_t { Overview, Heatmap };

  PluginRenderer& renderer_;
  int screenW_ = 0;
  int screenH_ = 0;
  Screen screen_ = Screen::Overview;

  // Heatmap month navigation
  int heatmapYear_ = 2026;
  int heatmapMonth_ = 1;  // 1-based

  // ─── Month navigation ────────────────────────────────────────
  void prevMonth() {
    heatmapMonth_--;
    if (heatmapMonth_ < 1) {
      heatmapMonth_ = 12;
      heatmapYear_--;
    }
  }

  void nextMonth() {
    heatmapMonth_++;
    if (heatmapMonth_ > 12) {
      heatmapMonth_ = 1;
      heatmapYear_++;
    }
  }

  // ─── Overview screen ─────────────────────────────────────────
  void drawOverview() {
    const Theme& t = THEME_MANAGER.current();
    GfxRenderer& gfx = renderer_.gfx();

    gfx.clearScreen(t.backgroundColor);

    // Title
    gfx.drawCenteredText(t.uiFontId, 50, "Reading Stats", t.primaryTextBlack);
    gfx.drawLine(20, 65, screenW_ - 20, 65, t.primaryTextBlack);

    // Stats rows
    const int leftX = 30;
    const int valueX = screenW_ - 30;
    int y = 100;
    const int rowH = 50;
    const int fontId = t.uiFontId;

    // Total Reading Time
    {
      char buf[16];
      formatDuration(readingStats.totalReadingMs, buf, sizeof(buf));
      drawStatRow(gfx, t, fontId, y, leftX, valueX, "Total Reading", buf);
      y += rowH;
    }

    // Sessions
    {
      char buf[16];
      formatNumber(readingStats.totalSessions, buf, sizeof(buf));
      drawStatRow(gfx, t, fontId, y, leftX, valueX, "Sessions", buf);
      y += rowH;
    }

    // Pages Read
    {
      char buf[16];
      formatNumber(readingStats.totalPagesRead, buf, sizeof(buf));
      drawStatRow(gfx, t, fontId, y, leftX, valueX, "Pages Read", buf);
      y += rowH;
    }

    // Books Started
    {
      char buf[16];
      formatNumber(readingStats.booksStarted, buf, sizeof(buf));
      drawStatRow(gfx, t, fontId, y, leftX, valueX, "Books Started", buf);
      y += rowH;
    }

    // Books Finished
    {
      char buf[16];
      formatNumber(readingStats.booksFinished, buf, sizeof(buf));
      drawStatRow(gfx, t, fontId, y, leftX, valueX, "Books Finished", buf);
      y += rowH;
    }

    // Current Streak
    {
      char buf[16];
      snprintf(buf, sizeof(buf), "%u day%s",
               readingStats.currentStreak,
               readingStats.currentStreak == 1 ? "" : "s");
      drawStatRow(gfx, t, fontId, y, leftX, valueX, "Current Streak", buf);
      y += rowH;
    }

    // Footer
    drawNavFooter(gfx, t, "Heatmap \xe2\x86\x92", "\xe2\x86\x90 Back");
  }

  void drawStatRow(GfxRenderer& gfx, const Theme& t, int fontId,
                   int y, int leftX, int valueX,
                   const char* label, const char* value) {
    gfx.drawText(fontId, leftX, y, label, t.primaryTextBlack);
    int vw = gfx.getTextWidth(fontId, value);
    gfx.drawText(fontId, valueX - vw, y, value, t.primaryTextBlack);
  }

  // ─── Heatmap screen ──────────────────────────────────────────
  void drawHeatmap() {
    const Theme& t = THEME_MANAGER.current();
    GfxRenderer& gfx = renderer_.gfx();

    gfx.clearScreen(t.backgroundColor);

    // Check if clock is synced
    if (!SumiClock::hasTime()) {
      gfx.drawCenteredText(t.uiFontId, screenH_ / 2 - 10,
                           "Sync time via BLE", t.primaryTextBlack);
      gfx.drawCenteredText(t.uiFontId, screenH_ / 2 + 20,
                           "to enable heatmap.", t.primaryTextBlack);
      drawNavFooter(gfx, t, "\xe2\x86\x90 Overview", "Back \xe2\x86\x92");
      return;
    }

    // Month/Year title
    {
      char title[32];
      const char* monthNames[] = {
        "January", "February", "March", "April",
        "May", "June", "July", "August",
        "September", "October", "November", "December"
      };
      int mi = heatmapMonth_ - 1;
      if (mi < 0) mi = 0;
      if (mi > 11) mi = 11;
      snprintf(title, sizeof(title), "%s %d", monthNames[mi], heatmapYear_);
      gfx.drawCenteredText(t.uiFontId, 40, title, t.primaryTextBlack);
    }

    // Day-of-week headers
    const int headerY = 68;
    const int gridMargin = 16;
    const int cellSize = (screenW_ - 2 * gridMargin) / 7;
    const int gridX = (screenW_ - cellSize * 7) / 2;  // center the grid

    {
      const char* dayNames[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
      for (int d = 0; d < 7; d++) {
        int cx = gridX + d * cellSize + cellSize / 2;
        int tw = gfx.getTextWidth(t.smallFontId, dayNames[d]);
        gfx.drawText(t.smallFontId, cx - tw / 2, headerY, dayNames[d],
                     t.primaryTextBlack);
      }
    }

    // Calendar grid
    int daysInMonth = getDaysInMonth(heatmapYear_, heatmapMonth_);
    int firstDow = getDayOfWeek(heatmapYear_, heatmapMonth_, 1);  // 0=Mon

    const int gridY = headerY + 30;
    const int cellPad = 3;  // padding inside each cell

    for (int day = 1; day <= daysInMonth; day++) {
      int col = (firstDow + day - 1) % 7;
      int row = (firstDow + day - 1) / 7;

      int cx = gridX + col * cellSize;
      int cy = gridY + row * cellSize;
      int innerX = cx + cellPad;
      int innerY = cy + cellPad;
      int innerW = cellSize - 2 * cellPad;
      int innerH = cellSize - 2 * cellPad;

      // Look up reading minutes for this day
      uint16_t dayNum = dayNumberForDate(heatmapYear_, heatmapMonth_, day);
      uint16_t minutes = readingStats.getDailyMinutes(dayNum);

      // Draw heat level
      drawHeatCell(gfx, t, innerX, innerY, innerW, innerH, minutes);

      // Draw day number text centered in cell
      char dayBuf[4];
      snprintf(dayBuf, sizeof(dayBuf), "%d", day);
      int tw = gfx.getTextWidth(t.smallFontId, dayBuf);
      int th = gfx.getLineHeight(t.smallFontId);
      int textX = cx + (cellSize - tw) / 2;
      int textY = cy + (cellSize - th) / 2;

      // Inverted text for fully-filled cells
      bool textBlack = (minutes > 60) ? !t.primaryTextBlack : t.primaryTextBlack;
      gfx.drawText(t.smallFontId, textX, textY, dayBuf, textBlack);
    }

    // Legend
    drawLegend(gfx, t, gridY + ((firstDow + daysInMonth - 1) / 7 + 1) * cellSize + 10);

    // Footer
    drawNavFooter(gfx, t, "\xe2\x86\x90 Overview", "Back \xe2\x86\x92");
  }

  // ─── Heat cell rendering ─────────────────────────────────────
  // Increasing visual intensity for more reading:
  //   0 min:      empty
  //   < 10 min:   thin border (1px)
  //   10-30 min:  thick border (3px)
  //   30-60 min:  half-filled (bottom half)
  //   > 60 min:   fully filled (inverted)
  void drawHeatCell(GfxRenderer& gfx, const Theme& t,
                    int x, int y, int w, int h, uint16_t minutes) {
    if (minutes == 0) {
      // Empty cell -- light outline only
      gfx.drawRect(x, y, w, h, t.primaryTextBlack);
      return;
    }

    if (minutes < 10) {
      // Light border: single-pixel rect
      gfx.drawRect(x, y, w, h, t.primaryTextBlack);
    } else if (minutes < 30) {
      // Thick border: 3-pixel rect
      gfx.drawRect(x, y, w, h, t.primaryTextBlack);
      gfx.drawRect(x + 1, y + 1, w - 2, h - 2, t.primaryTextBlack);
      gfx.drawRect(x + 2, y + 2, w - 4, h - 4, t.primaryTextBlack);
    } else if (minutes <= 60) {
      // Half-filled: outline + bottom half solid
      gfx.drawRect(x, y, w, h, t.primaryTextBlack);
      int halfH = h / 2;
      gfx.fillRect(x, y + h - halfH, w, halfH, t.primaryTextBlack);
    } else {
      // Fully filled (inverted)
      gfx.fillRect(x, y, w, h, t.primaryTextBlack);
    }
  }

  // ─── Legend ──────────────────────────────────────────────────
  void drawLegend(GfxRenderer& gfx, const Theme& t, int y) {
    // Compact legend: "Less" [ ][ ][ ][ ] "More"
    const int boxSize = 16;
    const int gap = 6;
    const int totalW = gfx.getTextWidth(t.smallFontId, "Less") + 4 * (boxSize + gap)
                       + gfx.getTextWidth(t.smallFontId, "More") + gap * 2;
    int x = (screenW_ - totalW) / 2;

    gfx.drawText(t.smallFontId, x, y, "Less", t.primaryTextBlack);
    x += gfx.getTextWidth(t.smallFontId, "Less") + gap;

    // Level 0: border only
    gfx.drawRect(x, y, boxSize, boxSize, t.primaryTextBlack);
    x += boxSize + gap;

    // Level 1: thick border
    gfx.drawRect(x, y, boxSize, boxSize, t.primaryTextBlack);
    gfx.drawRect(x + 1, y + 1, boxSize - 2, boxSize - 2, t.primaryTextBlack);
    gfx.drawRect(x + 2, y + 2, boxSize - 4, boxSize - 4, t.primaryTextBlack);
    x += boxSize + gap;

    // Level 2: half filled
    gfx.drawRect(x, y, boxSize, boxSize, t.primaryTextBlack);
    gfx.fillRect(x, y + boxSize / 2, boxSize, boxSize / 2, t.primaryTextBlack);
    x += boxSize + gap;

    // Level 3: full filled
    gfx.fillRect(x, y, boxSize, boxSize, t.primaryTextBlack);
    x += boxSize + gap;

    gfx.drawText(t.smallFontId, x, y, "More", t.primaryTextBlack);
  }

  // ─── Footer ──────────────────────────────────────────────────
  void drawNavFooter(GfxRenderer& gfx, const Theme& t,
                     const char* leftLabel, const char* rightLabel) {
    int y = screenH_ - PLUGIN_FOOTER_H;
    gfx.drawLine(0, y, screenW_, y, t.primaryTextBlack);

    int textY = y + (PLUGIN_FOOTER_H - gfx.getLineHeight(t.smallFontId)) / 2;

    if (leftLabel && leftLabel[0]) {
      gfx.drawText(t.smallFontId, PLUGIN_MARGIN, textY,
                   leftLabel, t.primaryTextBlack);
    }
    if (rightLabel && rightLabel[0]) {
      int rw = gfx.getTextWidth(t.smallFontId, rightLabel);
      gfx.drawText(t.smallFontId, screenW_ - rw - PLUGIN_MARGIN, textY,
                   rightLabel, t.primaryTextBlack);
    }

    // Up/Down hint on heatmap
    if (screen_ == Screen::Heatmap && SumiClock::hasTime()) {
      const char* hint = "\xe2\x96\xb2\xe2\x96\xbc Month";  // triangle up/down + "Month"
      int hw = gfx.getTextWidth(t.smallFontId, hint);
      gfx.drawText(t.smallFontId, (screenW_ - hw) / 2, textY,
                   hint, t.primaryTextBlack);
    }
  }

  // ─── Calendar math ───────────────────────────────────────────
  static bool isLeapYear(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
  }

  static int getDaysInMonth(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    int d = days[month - 1];
    if (month == 2 && isLeapYear(year)) d = 29;
    return d;
  }

  // Day of week for a date: 0=Monday .. 6=Sunday (ISO week)
  // Uses Tomohiko Sakamoto's algorithm
  static int getDayOfWeek(int y, int m, int d) {
    static const int tbl[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    int dow = (y + y / 4 - y / 100 + y / 400 + tbl[m - 1] + d) % 7;
    // Sakamoto returns 0=Sunday. Convert to 0=Monday.
    return (dow + 6) % 7;
  }

  // Convert a calendar date to our dayNumber (days since 2024-01-01)
  static uint16_t dayNumberForDate(int year, int month, int day) {
    // Build a struct tm and use mktime for correctness
    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 12;  // noon to avoid DST edge cases
    time_t t = mktime(&tm);
    if (t == static_cast<time_t>(-1)) return 0;
    uint32_t epoch = static_cast<uint32_t>(t);
    return ReadingStats::epochToDayNumber(epoch);
  }

  // ─── Formatting helpers ──────────────────────────────────────
  static void formatDuration(uint32_t ms, char* buf, int bufSize) {
    uint32_t totalMin = ms / 60000;
    uint32_t hours = totalMin / 60;
    uint32_t mins = totalMin % 60;
    if (hours > 0) {
      snprintf(buf, bufSize, "%luh %lum", (unsigned long)hours, (unsigned long)mins);
    } else {
      snprintf(buf, bufSize, "%lum", (unsigned long)mins);
    }
  }

  static void formatNumber(uint16_t n, char* buf, int bufSize) {
    if (n >= 1000) {
      snprintf(buf, bufSize, "%u,%03u", n / 1000, n % 1000);
    } else {
      snprintf(buf, bufSize, "%u", n);
    }
  }
};

}  // namespace sumi

#endif  // FEATURE_PLUGINS

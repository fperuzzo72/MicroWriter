#pragma once

#include "PluginInterface.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Utf8.h>

#include <cstring>

#include "../Theme.h"
#include "ThemeManager.h"

namespace sumi {

// Daily Quote — reads quotes.txt from SD and shows one per day.
// File format: one quote per line, optionally with attribution after a pipe:
//   "The only way to do great work is to love what you do.|Steve Jobs"
// If no quotes file exists, shows a welcome message with format instructions.
class DailyQuoteApp : public PluginInterface {
  PluginRenderer& renderer_;

  static constexpr int MAX_QUOTE_LEN = 300;
  static constexpr int MAX_ATTR_LEN = 64;

  char quote_[MAX_QUOTE_LEN] = {};
  char attribution_[MAX_ATTR_LEN] = {};
  bool loaded_ = false;
  int totalQuotes_ = 0;
  int quoteIndex_ = 0;

 public:
  DailyQuoteApp(PluginRenderer& r) : renderer_(r) {}

  const char* name() const override { return "Daily Quote"; }

  void init(int w, int h) override {
    loadQuoteForToday();
    needsFullRedraw = true;
  }

  void cleanup() override {}

  bool handleInput(PluginButton btn) override {
    switch (btn) {
      case PluginButton::Left:
        // Previous quote
        if (totalQuotes_ > 0) {
          quoteIndex_ = (quoteIndex_ == 0) ? totalQuotes_ - 1 : quoteIndex_ - 1;
          loadQuoteAtIndex(quoteIndex_);
          needsFullRedraw = true;
        }
        return true;
      case PluginButton::Right:
        // Next quote
        if (totalQuotes_ > 0) {
          quoteIndex_ = (quoteIndex_ + 1) % totalQuotes_;
          loadQuoteAtIndex(quoteIndex_);
          needsFullRedraw = true;
        }
        return true;
      case PluginButton::Back:
        return false;  // exit
      default:
        return true;
    }
  }

  void draw() override {
    if (!needsFullRedraw) return;
    const Theme& t = THEME_MANAGER.current();
    GfxRenderer& gfx = renderer_.gfx();
    const int W = gfx.getScreenWidth();
    const int H = gfx.getScreenHeight();

    gfx.clearScreen(t.backgroundColor);

    if (!loaded_) {
      gfx.drawCenteredText(t.uiFontId, 60, "Daily Quote", t.primaryTextBlack);
      gfx.drawCenteredText(t.smallFontId, H / 2 - 30,
                           "Create /quotes.txt on your SD card", t.primaryTextBlack);
      gfx.drawCenteredText(t.smallFontId, H / 2,
                           "with one quote per line:", t.primaryTextBlack);
      gfx.drawCenteredText(t.smallFontId, H / 2 + 30,
                           "Quote text here|Author Name", t.primaryTextBlack);
    } else {
      // Title with quote index
      char title[32];
      snprintf(title, sizeof(title), "Quote %d of %d", quoteIndex_ + 1, totalQuotes_);
      gfx.drawCenteredText(t.smallFontId, 20, title, t.primaryTextBlack);

      // Quote text — word-wrapped
      auto lines = gfx.wrapTextWithHyphenation(
          t.uiFontId, quote_, W - 60, 8);

      int y = H / 2 - static_cast<int>(lines.size()) * 28 / 2;
      for (const auto& line : lines) {
        int lw = gfx.getTextWidth(t.uiFontId, line.c_str());
        gfx.drawText(t.uiFontId, (W - lw) / 2, y, line.c_str(), t.primaryTextBlack);
        y += 28;
      }

      // Attribution
      if (attribution_[0] != '\0') {
        char attr[80];
        snprintf(attr, sizeof(attr), "— %s", attribution_);
        int aw = gfx.getTextWidth(t.smallFontId, attr);
        gfx.drawText(t.smallFontId, (W - aw) / 2, y + 20, attr, t.primaryTextBlack);
      }

      // Navigation hint
      if (totalQuotes_ > 1) {
        gfx.drawCenteredText(t.smallFontId, H - 30,
                             "Left / Right to browse", t.primaryTextBlack);
      }
    }

    gfx.displayBuffer();
    needsFullRedraw = false;
  }

 private:
  void loadQuoteForToday() {
    // Count total quotes
    totalQuotes_ = countQuotes();
    if (totalQuotes_ == 0) return;

    // Pick quote index based on millis (pseudo-daily rotation)
    // With BLE clock synced, millis() effectively gives us a day-based seed
    quoteIndex_ = static_cast<int>((millis() / 1000) % totalQuotes_);
    loadQuoteAtIndex(quoteIndex_);
  }

  int countQuotes() {
    FsFile f;
    if (!SdMan.openFileForRead("QOT", "/quotes.txt", f)) return 0;

    int count = 0;
    char buf[4];
    bool inLine = false;
    while (true) {
      int n = f.read(reinterpret_cast<uint8_t*>(buf), 1);
      if (n <= 0) break;
      if (buf[0] == '\n') {
        if (inLine) count++;
        inLine = false;
      } else if (buf[0] != '\r') {
        inLine = true;
      }
    }
    if (inLine) count++;  // last line without newline
    f.close();
    return count;
  }

  void loadQuoteAtIndex(int index) {
    FsFile f;
    if (!SdMan.openFileForRead("QOT", "/quotes.txt", f)) return;

    int lineNum = 0;
    char lineBuf[MAX_QUOTE_LEN + MAX_ATTR_LEN + 4];
    int pos = 0;

    while (true) {
      int n = f.read(reinterpret_cast<uint8_t*>(lineBuf + pos), 1);
      if (n <= 0) {
        // End of file — process last line if we're on target
        if (lineNum == index && pos > 0) {
          lineBuf[pos] = '\0';
          parseLine(lineBuf);
          loaded_ = true;
        }
        break;
      }

      if (lineBuf[pos] == '\n' || lineBuf[pos] == '\r') {
        if (pos > 0) {
          lineBuf[pos] = '\0';
          if (lineNum == index) {
            parseLine(lineBuf);
            loaded_ = true;
            break;
          }
          lineNum++;
          pos = 0;
        }
        continue;
      }

      pos++;
      if (pos >= static_cast<int>(sizeof(lineBuf) - 1)) {
        // Line too long — skip to next
        pos = 0;
        // Drain remaining chars until newline
        while (true) {
          int m = f.read(reinterpret_cast<uint8_t*>(lineBuf), 1);
          if (m <= 0 || lineBuf[0] == '\n') break;
        }
        lineNum++;
      }
    }

    f.close();
  }

  void parseLine(const char* line) {
    // Find pipe separator for attribution
    const char* pipe = strchr(line, '|');
    if (pipe) {
      // NUL-terminate the quote portion into a scratch buffer, then
      // utf8SafeCopy so a CJK quote longer than MAX_QUOTE_LEN doesn't
      // truncate mid-codepoint (memcpy alone would).
      const size_t rawQuoteLen = static_cast<size_t>(pipe - line);
      char scratch[MAX_QUOTE_LEN + MAX_ATTR_LEN + 4];
      const size_t cap = rawQuoteLen < sizeof(scratch) - 1 ? rawQuoteLen : sizeof(scratch) - 1;
      memcpy(scratch, line, cap);
      scratch[cap] = '\0';
      utf8SafeCopy(quote_, scratch, sizeof(quote_));
      utf8SafeCopy(attribution_, pipe + 1, sizeof(attribution_));
    } else {
      utf8SafeCopy(quote_, line, sizeof(quote_));
      attribution_[0] = '\0';
    }

    // Trim trailing whitespace from quote
    int len = strlen(quote_);
    while (len > 0 && (quote_[len - 1] == ' ' || quote_[len - 1] == '\r')) {
      quote_[--len] = '\0';
    }
  }
};

}  // namespace sumi

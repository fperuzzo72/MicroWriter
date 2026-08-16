#pragma once

#include "../config.h"

#if FEATURE_PLUGINS

#include <SDCardManager.h>
#include <Utf8.h>
#include "PluginInterface.h"
#include "PluginRenderer.h"
#include "../ThemeManager.h"

#include <cstring>

namespace sumi {

// "If Found" contact card — displays owner info from /if_found.txt.
// Lets strangers return a lost device without needing to power-on
// into the full reader. One line per field (name, email, phone, etc.).
class IfFoundApp : public PluginInterface {
public:
  explicit IfFoundApp(PluginRenderer& r) : renderer_(r) {}

  const char* name() const override { return "If Found"; }

  void init(int /*w*/, int /*h*/) override {
    // Try to load /if_found.txt
    FsFile f;
    if (SdMan.openFileForRead("IFF", "/if_found.txt", f)) {
      char buf[512];
      int n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf) - 1);
      f.close();
      if (n > 0) {
        buf[n] = '\0';
        // Split into lines
        lineCount_ = 0;
        char* line = strtok(buf, "\n");
        while (line && lineCount_ < MAX_LINES) {
          // trim \r
          char* cr = strchr(line, '\r');
          if (cr) *cr = '\0';
          utf8SafeCopy(lines_[lineCount_], line, sizeof(lines_[0]));
          lineCount_++;
          line = strtok(nullptr, "\n");
        }
        loaded_ = true;
      }
    }
    needsFullRedraw = true;
  }

  void cleanup() override {}

  bool handleInput(PluginButton btn) override {
    if (btn == PluginButton::Back) return false;  // exit plugin
    return true;
  }

  void draw() override {
    if (!needsFullRedraw) return;

    const Theme& t = THEME_MANAGER.current();
    GfxRenderer& gfx = renderer_.gfx();

    gfx.clearScreen(t.backgroundColor);

    if (!loaded_) {
      gfx.drawCenteredText(t.uiFontId, 60, "If Found", t.primaryTextBlack);
      int midY = gfx.getScreenHeight() / 2;
      gfx.drawCenteredText(t.smallFontId, midY - 20,
                           "Create /if_found.txt on your SD card",
                           t.primaryTextBlack);
      gfx.drawCenteredText(t.smallFontId, midY + 10,
                           "with your contact information.",
                           t.primaryTextBlack);
      gfx.drawCenteredText(t.smallFontId, midY + 40,
                           "One line per field:",
                           t.primaryTextBlack);
      gfx.drawCenteredText(t.smallFontId, midY + 70,
                           "Name / Email / Phone",
                           t.primaryTextBlack);
    } else {
      gfx.drawCenteredText(t.uiFontId, 60,
                           "If Found, Please Contact:",
                           t.primaryTextBlack);

      int y = 140;
      constexpr int lineHeight = 45;
      for (int i = 0; i < lineCount_; i++) {
        gfx.drawCenteredText(t.uiFontId, y, lines_[i], t.primaryTextBlack);
        y += lineHeight;
      }
    }

    gfx.displayBuffer();
    needsFullRedraw = false;
  }

private:
  static constexpr int MAX_LINES = 8;

  PluginRenderer& renderer_;
  char lines_[MAX_LINES][64];  // up to 8 lines, 63 chars each
  int lineCount_ = 0;
  bool loaded_ = false;
};

}  // namespace sumi

#endif  // FEATURE_PLUGINS

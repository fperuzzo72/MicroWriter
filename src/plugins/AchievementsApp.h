#pragma once

#include "../config.h"

#if FEATURE_PLUGINS

#include <Achievements.h>
#include <Arduino.h>
#include <GfxRenderer.h>

#include "PluginHelpers.h"
#include "PluginInterface.h"
#include "PluginRenderer.h"
#include "ThemeManager.h"

namespace sumi {

extern Achievements achievements;

class AchievementsApp : public PluginInterface {
  PluginRenderer& renderer_;
  int selected_ = 0;
  int scrollOffset_ = 0;
  int screenW_ = 480;
  int screenH_ = 800;

 public:
  explicit AchievementsApp(PluginRenderer& r) : renderer_(r) {}

  const char* name() const override { return "Achievements"; }
  PluginRunMode runMode() const override { return PluginRunMode::Simple; }

  void init(int w, int h) override {
    screenW_ = w;
    screenH_ = h;
    selected_ = 0;
    scrollOffset_ = 0;
    needsFullRedraw = true;
  }

  void cleanup() override {}

  bool handleInput(PluginButton btn) override {
    switch (btn) {
      case PluginButton::Up:
        if (selected_ > 0) {
          selected_--;
          ensureVisible();
          needsFullRedraw = true;
        }
        return true;
      case PluginButton::Down:
        if (selected_ < Achievements::ACHIEVEMENT_COUNT - 1) {
          selected_++;
          ensureVisible();
          needsFullRedraw = true;
        }
        return true;
      case PluginButton::Back:
        return false;
      default:
        return true;
    }
  }

  void draw() override {
    if (!needsFullRedraw) return;

    const Theme& t = THEME_MANAGER.current();
    GfxRenderer& gfx = renderer_.gfx();

    gfx.clearScreen(t.backgroundColor);

    // Header with count
    char header[40];
    snprintf(header, sizeof(header), "Achievements (%d/%d)",
             achievements.unlockedCount(), Achievements::ACHIEVEMENT_COUNT);
    PluginUI::drawHeader(renderer_, header, screenW_);

    // Calculate layout
    const int startY = PLUGIN_HEADER_H + 8;
    const int itemH = 52;  // name + description
    const int footerY = screenH_ - PLUGIN_FOOTER_H;
    const int maxVisible = (footerY - startY) / itemH;

    int end = scrollOffset_ + maxVisible;
    if (end > Achievements::ACHIEVEMENT_COUNT) end = Achievements::ACHIEVEMENT_COUNT;

    for (int i = scrollOffset_; i < end; i++) {
      const int y = startY + (i - scrollOffset_) * itemH;
      const auto& def = Achievements::DEFS[i];
      bool unlocked = achievements.isUnlocked(i);
      bool sel = (i == selected_);

      // Selection highlight
      if (sel) {
        gfx.fillRect(4, y - 2, screenW_ - 8, itemH - 4, !t.primaryTextBlack);
      }

      bool textColor = sel ? !t.primaryTextBlack : t.primaryTextBlack;

      // Achievement name with unlock indicator
      char nameStr[64];
      snprintf(nameStr, sizeof(nameStr), "%s %s", unlocked ? "[*]" : "[ ]", def.name);
      gfx.drawText(t.uiFontId, 12, y + 2, nameStr, textColor);

      // Description (smaller, secondary color if not selected)
      bool descColor = sel ? !t.primaryTextBlack : t.secondaryTextBlack;
      gfx.drawText(t.smallFontId, 36, y + 24, def.description, descColor);
    }

    // Scroll indicators
    if (scrollOffset_ > 0) {
      int cx = screenW_ / 2;
      gfx.drawLine(cx, startY - 6, cx - 6, startY - 1, t.primaryTextBlack);
      gfx.drawLine(cx, startY - 6, cx + 6, startY - 1, t.primaryTextBlack);
    }
    if (end < Achievements::ACHIEVEMENT_COUNT) {
      int cx = screenW_ / 2;
      gfx.drawLine(cx, footerY - 6, cx - 6, footerY - 12, t.primaryTextBlack);
      gfx.drawLine(cx, footerY - 6, cx + 6, footerY - 12, t.primaryTextBlack);
    }

    // Footer
    PluginUI::drawFooter(renderer_, "Back", "", screenW_, screenH_);

    gfx.displayBuffer();
    needsFullRedraw = false;
  }

 private:
  void ensureVisible() {
    const int footerY = screenH_ - PLUGIN_FOOTER_H;
    const int startY = PLUGIN_HEADER_H + 8;
    const int itemH = 52;
    const int maxVisible = (footerY - startY) / itemH;

    if (selected_ < scrollOffset_) scrollOffset_ = selected_;
    if (selected_ >= scrollOffset_ + maxVisible) scrollOffset_ = selected_ - maxVisible + 1;
  }
};

}  // namespace sumi

#endif  // FEATURE_PLUGINS

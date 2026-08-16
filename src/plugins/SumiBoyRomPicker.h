#pragma once
/**
 * @file SumiBoyRomPicker.h
 * @brief ROM selection UI for SumiBoy — scans /games/ for .gb files
 *
 * Shows a scrollable list of Game Boy ROMs found on the SD card.
 * On selection, creates a SumiBoyEmulator instance and transitions to it.
 */

#include <Arduino.h>
#include <SDCardManager.h>

#include "../config.h"

#if FEATURE_PLUGINS && FEATURE_GAMES

#include "PluginInterface.h"
#include "PluginRenderer.h"
#include "SumiBoyEmulator.h"
#include "../core/BootMode.h"
#include "../core/Core.h"
#include <Utf8.h>
#include "../ThemeManager.h"

#include <vector>

namespace sumi {

class SumiBoyRomPicker : public PluginInterface {
 public:
  explicit SumiBoyRomPicker(PluginRenderer& renderer) : d_(renderer) {}

  const char* name() const override { return "SumiBoy"; }
  PluginRunMode runMode() const override {
    return (emulator_ && emulator_->isReady()) ? PluginRunMode::WithUpdate
                                               : PluginRunMode::Simple;
  }

  void init(int screenW, int screenH) override {
    w_ = screenW;
    h_ = screenH;
    selected_ = 0;
    scrollOffset_ = 0;

    d_.setRegularFontId(UI_FONT_ID);

    // Show a "Preparing…" splash IMMEDIATELY so the user has visual
    // feedback that the device is doing something while we reclaim
    // heap / release caches for the emulator. Without it the picker
    // pops up instantly with the last bit of fragmented heap and the
    // first ROM load fails for opaque reasons.
    drawPreparingSplash();
    Serial.printf("[SumiBoyPicker] Heap before prep: free=%lu, largest=%lu\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxAllocHeap());

    // Clear every cache the emulator doesn't need so the 32 KB ROM
    // + 47 KB emulator buffers have the best possible contiguous slot
    // to land in. ThemeManager cache is the one user-observable KB
    // we can drop without visible side effects — gets rebuilt on
    // the next theme use.
    THEME_MANAGER.clearCache();

    Serial.printf("[SumiBoyPicker] Heap after prep:  free=%lu, largest=%lu\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxAllocHeap());

    // Scan /games/ for .gb and .gbc files
    romFiles_.clear();
    auto files = SdMan.listFiles(PLUGINS_GAMES_DIR, 50);
    for (const auto& f : files) {
      String lower = f;
      lower.toLowerCase();
      if (lower.endsWith(".gb") || lower.endsWith(".gbc")) {
        romFiles_.push_back(f);
      }
    }

    Serial.printf("[SumiBoyPicker] Found %d ROM(s) in %s\n",
                  (int)romFiles_.size(), PLUGINS_GAMES_DIR);

    if (romFiles_.size() == 1) {
      // Single ROM — launch directly, skip picker UI
      launchRom(0);
    }
  }

  void drawPreparingSplash() {
    d_.fillScreen(0);  // White
    const int lineH = (d_.getLineHeight() > 0) ? d_.getLineHeight() : 20;
    const int cy = h_ / 2 - lineH;

    const char* t1 = "SumiBoy";
    int tw1 = d_.getTextWidth(t1);
    d_.setCursor((w_ - tw1) / 2, cy);
    d_.print(t1);

    const char* t2 = "Preparing...";
    int tw2 = d_.getTextWidth(t2);
    d_.setCursor((w_ - tw2) / 2, cy + lineH + 10);
    d_.print(t2);

    d_.display();
  }

  void draw() override {
    d_.fillScreen(0);  // White

    if (emulator_ && emulator_->isReady()) {
      // Emulator is running — delegate draw to it.
      emulator_->draw();
      return;
    }

    if (emulator_ && !emulator_->isReady()) {
      // Construction completed but init() bailed out (heap exhaustion or
      // ROM read failure). Show a recoverable error instead of hanging
      // on the "Loading..." splash the wrapper drew before we got here.
      drawLoadError();
      d_.display();
      return;
    }

    if (romFiles_.empty()) {
      drawNoRoms();
    } else {
      drawRomList();
    }

    d_.display();
  }

  bool handleRelease(PluginButton btn) override {
    if (emulator_ && emulator_->isReady()) return emulator_->handleRelease(btn);
    return false;
  }

  bool handleInput(PluginButton btn) override {
    if (emulator_ && emulator_->isReady()) {
      bool consumed = emulator_->handleInput(btn);
      needsFullRedraw = emulator_->needsFullRedraw;
      return consumed;
    }

    // Failed-init path: any button returns to the picker.
    if (emulator_ && !emulator_->isReady()) {
      emulator_->cleanup();
      delete emulator_;
      emulator_ = nullptr;
      needsFullRedraw = true;
      return true;
    }

    if (romFiles_.empty()) {
      return false;  // Any button exits
    }

    switch (btn) {
      case PluginButton::Up:
        // Wrap to bottom when pressing Up on the first ROM — matches
        // how every other SUMI menu behaves (file browser, settings,
        // in-reader settings overlay). Stopping at the top felt broken.
        if (selected_ > 0) {
          selected_--;
        } else {
          selected_ = (int)romFiles_.size() - 1;
        }
        if (selected_ < scrollOffset_) scrollOffset_ = selected_;
        if (selected_ >= scrollOffset_ + maxVisible()) scrollOffset_ = selected_ - maxVisible() + 1;
        needsFullRedraw = true;
        return true;

      case PluginButton::Down:
        // Wrap to top when pressing Down on the last ROM.
        if (selected_ < (int)romFiles_.size() - 1) {
          selected_++;
        } else {
          selected_ = 0;
        }
        if (selected_ >= scrollOffset_ + maxVisible()) scrollOffset_ = selected_ - maxVisible() + 1;
        if (selected_ < scrollOffset_) scrollOffset_ = selected_;
        needsFullRedraw = true;
        return true;

      case PluginButton::Center:
        launchRom(selected_);
        return true;

      case PluginButton::Back:
        if (emulator_) {
          // Exit emulator back to picker
          emulator_->cleanup();
          delete emulator_;
          emulator_ = nullptr;
          needsFullRedraw = true;
          return true;
        }
        return false;  // Exit plugin

      default:
        return true;
    }
  }

  bool update() override {
    if (emulator_ && emulator_->isReady()) {
      return emulator_->update();
    }
    return false;
  }

  // When emulator is running, delegate to emulator's orientation preference
  bool wantsLandscape() const override {
    return (emulator_ && emulator_->isReady()) ? emulator_->wantsLandscape() : false;
  }

  // Only cede refresh control when the emulator actually booted. If init
  // failed, we stay in picker-owned refresh mode so drawLoadError() gets
  // pushed to the screen by PluginHostState::render().
  bool handlesOwnRefresh() const override {
    return emulator_ && emulator_->isReady();
  }

  void cleanup() override {
    if (emulator_) {
      emulator_->cleanup();
      delete emulator_;
      emulator_ = nullptr;
    }
  }

 private:
  PluginRenderer& d_;
  int w_ = 0, h_ = 0;
  int selected_ = 0;
  int scrollOffset_ = 0;
  std::vector<String> romFiles_;
  SumiBoyEmulator* emulator_ = nullptr;

  int maxVisible() const {
    int lineH = d_.getLineHeight();
    if (lineH <= 0) lineH = 20;
    // Must match drawRomList's layout: title (lineH+10) + subtitle
    // (lineH+8) + listStartY gap (lineH+18) + bottom padding (20).
    const int headerH = 3 * lineH + 56;
    const int itemH = lineH + 16;
    const int avail = h_ - headerH - 20;
    return avail > 0 ? avail / itemH : 1;
  }

  void launchRom(int index) {
    if (index < 0 || index >= (int)romFiles_.size()) return;

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", PLUGINS_GAMES_DIR, romFiles_[index].c_str());

    Serial.printf("[SumiBoyPicker] Launching ROM via dual-boot: %s\n", path);

    // Dual-boot trigger: write the ROM path into settings.lastBookPath,
    // set pendingTransition = 3 (BootMode::EMULATOR), then ESP.restart().
    // The next boot's setup() detects the flag and runs initEmulator()
    // instead of initSystem() — so the GB core gets the full ~120 KB
    // heap with no fonts / themes / plugins / reading-stats / BLE
    // competing for fragmented contig blocks. This is the "not enough
    // memory to open Tetris" fix: in-process construction was failing
    // the 32 KB contig allocation under fragmented heap; the cold-boot
    // path doesn't fragment in the first place.
    //
    // On exit (long-press Back), initEmulator saves pendingTransition=1
    // (UI mode) and ESP.restarts back here.
    d_.fillScreen(0);
    int cy = h_ / 2 - 40;
    d_.setCursor(w_ / 2 - 80, cy);
    d_.print("Loading...");
    cy += 40;
    d_.setCursor(20, cy);
    d_.print("Booting SumiBoy in");
    cy += 25;
    d_.setCursor(20, cy);
    d_.print("dedicated mode. Hold Back");
    cy += 25;
    d_.setCursor(20, cy);
    d_.print("for 1 sec to exit.");
    d_.display();

    // Hand the ROM path off via TWO redundant channels:
    //   1. RTC retention memory (rtcEmuMagic + rtcEmuRomPath) — survives
    //      soft reset cleanly on the emulator and real HW. Primary signal.
    //   2. /.sumi/transition.bin (Batch 3b — audit #41) — backstop for
    //      the RTC-RAM-wiped edge case (cold power loss between save and
    //      restart). Real-HW only; the emulator's SD has the soft-reset
    //      cluster-chain quirk that makes this path flaky there.
    extern uint32_t rtcEmuMagic;
    extern char rtcEmuRomPath[200];
    extern const uint32_t RTC_EMU_MAGIC;
    rtcEmuMagic = RTC_EMU_MAGIC;
    utf8SafeCopy(rtcEmuRomPath, path, sizeof(rtcEmuRomPath));

    // saveTransition writes transition.bin and mirrors lastBookPath into
    // settings.bin (so the "Last Document" feature picks up the ROM if
    // the user later disables dual-boot). One transition.bin write +
    // one settings.bin write per dual-boot — vs the v0.6.0 pattern of
    // one settings.bin write per dual-boot. The boot-time clear path
    // (main.cpp) now drops transition.bin instead of writing settings.
    saveTransition(BootMode::EMULATOR, path, ReturnTo::HOME);

    Serial.println("[SumiBoyPicker] Saved EMULATOR transition (RTC+SD); ESP.restart()");
    delay(300);  // give the splash a chance to render
    ESP.restart();
  }

  void drawLoadError() {
    int cy = h_ / 2 - 60;
    d_.setCursor(w_ / 2 - 70, cy);
    d_.print("Load Failed");
    cy += 40;

    d_.setCursor(20, cy);
    d_.print("Not enough memory to");
    cy += 25;
    d_.setCursor(20, cy);
    d_.print("start this ROM.");
    cy += 35;

    d_.setCursor(20, cy);
    d_.print("Try smaller/simpler ROMs");
    cy += 25;
    d_.setCursor(20, cy);
    d_.print("or reboot the device to");
    cy += 25;
    d_.setCursor(20, cy);
    d_.print("defragment the heap.");
    cy += 35;

    d_.setCursor(20, cy);
    d_.print("Press any button to return");
  }

  void drawNoRoms() {
    int cy = h_ / 2 - 60;

    d_.setCursor(w_ / 2 - 60, cy);
    d_.print("SumiBoy");
    cy += 40;

    d_.setCursor(w_ / 2 - 100, cy);
    d_.print("No ROMs found");
    cy += 35;

    d_.setCursor(20, cy);
    d_.print("Place .gb files in:");
    cy += 25;

    d_.setCursor(20, cy);
    d_.print(PLUGINS_GAMES_DIR);
    cy += 35;

    d_.setCursor(20, cy);
    d_.print("Transfer via BLE or");
    cy += 25;

    d_.setCursor(20, cy);
    d_.print("copy to SD card");
  }

  void drawRomList() {
    // Layout matches the rest of SUMI's menus: title at the top with
    // breathing room, subtitle below with proper gap (not 15 px from
    // the baseline of the title!), then list items sized like the
    // apps / settings menus.
    const int lineH = (d_.getLineHeight() > 0) ? d_.getLineHeight() : 20;
    const int titleY = lineH + 10;
    const int subtitleY = titleY + lineH + 8;          // ~28 px gap below title
    const int listStartY = subtitleY + lineH + 18;     // more breathing room before list
    const int itemH = lineH + 16;                      // match settings menu density

    // Title — centred
    {
      const char* title = "SumiBoy";
      int tw = d_.getTextWidth(title);
      d_.setCursor((w_ - tw) / 2, titleY);
      d_.print(title);
    }

    // Subtitle — "N games" centred, visibly spaced below the title
    {
      char buf[32];
      snprintf(buf, sizeof(buf), "%d game%s",
               (int)romFiles_.size(), romFiles_.size() == 1 ? "" : "s");
      int tw = d_.getTextWidth(buf);
      d_.setCursor((w_ - tw) / 2, subtitleY);
      d_.print(buf);
    }

    int maxVis = maxVisible();

    for (int i = 0; i < maxVis && (scrollOffset_ + i) < (int)romFiles_.size(); i++) {
      int idx = scrollOffset_ + i;
      int itemY = listStartY + i * itemH;

      // Strip extension for display
      String display = romFiles_[idx];
      int dotPos = display.lastIndexOf('.');
      if (dotPos > 0) display = display.substring(0, dotPos);

      // Truncate long names on a UTF-8 codepoint boundary so CJK ROM
      // filenames don't end in a broken lead byte rendered as '?'.
      if (display.length() > 28) {
        size_t cutBytes = 25;
        while (cutBytes > 0
               && (static_cast<unsigned char>(display[cutBytes]) & 0xC0) == 0x80) {
          cutBytes--;
        }
        display = display.substring(0, cutBytes) + "...";
      }

      if (idx == selected_) {
        // Selected item — inverted bar, same style as ui::menuItem.
        d_.fillRect(10, itemY - 2, w_ - 20, itemH, true);
        d_.setTextColor(false);  // White text
        d_.setCursor(20, itemY + lineH + 2);
        d_.print(display.c_str());
        d_.setTextColor(true);   // Reset
      } else {
        d_.setCursor(20, itemY + lineH + 2);
        d_.print(display.c_str());
      }
    }

    // Scroll indicators — centred arrows above/below the list, matching
    // the visual language of other menus.
    if (scrollOffset_ > 0) {
      d_.setCursor(w_ / 2 - 4, listStartY - 10);
      d_.print("^");
    }
    if (scrollOffset_ + maxVis < (int)romFiles_.size()) {
      d_.setCursor(w_ / 2 - 4, listStartY + maxVis * itemH + 4);
      d_.print("v");
    }
  }
};

}  // namespace sumi

#endif  // FEATURE_PLUGINS && FEATURE_GAMES

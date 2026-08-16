#pragma once

#include "../config.h"

#if FEATURE_PLUGINS

#include <Arduino.h>
#include <Bitmap.h>
#include <CoverHelpers.h>
#include <FsHelpers.h>
#include <SDCardManager.h>
#include <Utf8.h>

#include "PluginHelpers.h"
#include "PluginInterface.h"
#include "PluginRenderer.h"

namespace sumi {

/**
 * @file SleepPickerApp.h
 * @brief Browse and preview sleep screen images from /sleep/
 *
 * Lists all BMP files in /sleep/ and lets the user preview them
 * full-screen (using the same Bitmap rendering path as SleepState).
 */
class SleepPickerApp : public PluginInterface {
public:
  static constexpr int MAX_IMAGES = 50;
  static constexpr int MAX_NAME_LEN = 48;

  explicit SleepPickerApp(PluginRenderer& renderer) : d_(renderer) {}

  const char* name() const override { return "Sleep Screens"; }
  PluginRunMode runMode() const override { return PluginRunMode::Simple; }

  void init(int screenW, int screenH) override {
    screenW_ = screenW;
    screenH_ = screenH;
    itemH_ = 36;
    itemsPerPage_ = (screenH_ - PLUGIN_HEADER_H - PLUGIN_FOOTER_H - 8) / itemH_;
    scanSleepDir();
    needsFullRedraw = true;
  }

  void cleanup() override {}

  bool handleInput(PluginButton btn) override {
    if (showingPreview_) {
      // Any button exits preview
      showingPreview_ = false;
      needsFullRedraw = true;
      return true;
    }

    switch (btn) {
      case PluginButton::Up:
        if (imageCount_ > 0) {
          selectedIndex_ = (selectedIndex_ == 0) ? imageCount_ - 1 : selectedIndex_ - 1;
          ensureVisible();
          needsFullRedraw = true;
        }
        return true;
      case PluginButton::Down:
        if (imageCount_ > 0) {
          selectedIndex_ = (selectedIndex_ + 1) % imageCount_;
          ensureVisible();
          needsFullRedraw = true;
        }
        return true;
      case PluginButton::Center:
      case PluginButton::Right:
        if (imageCount_ > 0) {
          previewImage();
        }
        return true;
      case PluginButton::Back:
        return false;  // exit plugin
      default:
        return true;
    }
  }

  void draw() override {
    if (showingPreview_) {
      drawPreview();
    } else {
      drawBrowser();
    }
  }

private:
  PluginRenderer& d_;

  char names_[MAX_IMAGES][MAX_NAME_LEN];  // filenames in /sleep/
  int imageCount_ = 0;
  int selectedIndex_ = 0;
  int scrollOffset_ = 0;
  bool showingPreview_ = false;
  int screenW_ = 0;
  int screenH_ = 0;
  int itemH_ = 36;
  int itemsPerPage_ = 10;

  void ensureVisible() {
    if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
    if (selectedIndex_ >= scrollOffset_ + itemsPerPage_)
      scrollOffset_ = selectedIndex_ - itemsPerPage_ + 1;
  }

  void scanSleepDir() {
    imageCount_ = 0;

    FsFile dir = SdMan.open("/sleep");
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      Serial.println("[SLP-PICK] /sleep/ directory not found");
      return;
    }

    char fname[64];
    FsFile entry;
    while (imageCount_ < MAX_IMAGES && entry.openNext(&dir, O_RDONLY)) {
      if (entry.isDirectory()) {
        entry.close();
        continue;
      }

      entry.getName(fname, sizeof(fname));

      // Skip hidden files
      if (fname[0] == '.') {
        entry.close();
        continue;
      }

      // Only BMP files
      if (!FsHelpers::isBmpFile(fname)) {
        entry.close();
        continue;
      }

      // UTF-8 safe copy for CJK filenames
      utf8SafeCopy(names_[imageCount_], fname, MAX_NAME_LEN);
      imageCount_++;
      entry.close();
    }
    dir.close();

    Serial.printf("[SLP-PICK] Found %d sleep screen image(s)\n", imageCount_);
  }

  void previewImage() {
    char fullPath[80];
    snprintf(fullPath, sizeof(fullPath), "/sleep/%s", names_[selectedIndex_]);

    FsFile file;
    if (!SdMan.openFileForRead("SLP", fullPath, file)) {
      Serial.printf("[SLP-PICK] Failed to open: %s\n", fullPath);
      return;
    }

    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      Serial.printf("[SLP-PICK] Invalid BMP: %s\n", fullPath);
      file.close();
      return;
    }

    // Render centered on screen, same pattern as SleepState::renderBitmapSleepScreen
    auto rect = CoverHelpers::calculateCenteredRect(
        bitmap.getWidth(), bitmap.getHeight(), 0, 0, screenW_, screenH_);

    GfxRenderer& gfx = d_.gfx();
    gfx.clearScreen();
    gfx.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);

    file.close();
    showingPreview_ = true;
    needsFullRedraw = true;
  }

  void drawBrowser() {
    d_.fillScreen(GxEPD_WHITE);
    PluginUI::drawHeader(d_, "Sleep Screens", screenW_);

    if (imageCount_ == 0) {
      d_.setCursor(20, screenH_ / 2 - 20);
      d_.print("No images in /sleep/");
      d_.setCursor(20, screenH_ / 2 + 10);
      d_.print("Add BMP files to /sleep/ on SD");
      PluginUI::drawFooter(d_, "", "", screenW_, screenH_);
      return;
    }

    int y = PLUGIN_HEADER_H + 4;
    int end = selectedIndex_ < scrollOffset_ + itemsPerPage_
                  ? scrollOffset_ + itemsPerPage_
                  : selectedIndex_ + 1;
    if (end > imageCount_) end = imageCount_;

    for (int i = scrollOffset_; i < end; i++) {
      bool sel = (i == selectedIndex_);
      PluginUI::drawMenuItem(d_, names_[i], PLUGIN_MARGIN, y,
                             screenW_ - 2 * PLUGIN_MARGIN, itemH_ - 4, sel);
      y += itemH_;
    }

    char status[32];
    snprintf(status, sizeof(status), "%d/%d", selectedIndex_ + 1, imageCount_);
    PluginUI::drawFooter(d_, status, "OK:Preview", screenW_, screenH_);
  }

  void drawPreview() {
    // Preview was already rendered in previewImage() via GfxRenderer.
    // Draw a small hint bar at the bottom.
    d_.fillRect(0, screenH_ - 20, screenW_, 20, GxEPD_WHITE);
    d_.drawLine(0, screenH_ - 20, screenW_, screenH_ - 20, GxEPD_BLACK);
    d_.setCursor(PLUGIN_MARGIN, screenH_ - 5);
    d_.print(names_[selectedIndex_]);

    int16_t tx, ty;
    uint16_t tw, th;
    const char* hint = "Any key: Back";
    d_.getTextBounds(hint, 0, 0, &tx, &ty, &tw, &th);
    d_.setCursor(screenW_ - tw - PLUGIN_MARGIN, screenH_ - 5);
    d_.print(hint);
  }
};

}  // namespace sumi

#endif  // FEATURE_PLUGINS

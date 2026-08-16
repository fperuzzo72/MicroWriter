#include "SleepState.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <CoverHelpers.h>
#include <EInkDisplay.h>
#include <Epub.h>
#include <I18n.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <InputManager.h>
#include <Markdown.h>
#include <SDCardManager.h>
#include <SumiClock.h>
#include <Txt.h>
#include <Xtc.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include <string>
#include <vector>

#include "../ThemeManager.h"
#include "../config.h"
#include "../core/Core.h"
#include "../images/SumiLogo.h"
#include "../util/SleepScreenCache.h"

extern InputManager inputManager;
extern uint16_t rtcPowerButtonDurationMs;

namespace sumi {

SleepState::SleepState(GfxRenderer& renderer) : renderer_(renderer) {}

void SleepState::enter(Core& core) {
  Serial.println("[STATE] SleepState::enter - rendering sleep screen");

  // Show immediate feedback before rendering sleep screen
  renderer_.clearScreen(THEME.backgroundColor);
  renderer_.drawCenteredText(THEME.uiFontId, renderer_.getScreenHeight() / 2, "Sleeping...", THEME.primaryTextBlack);
  renderer_.displayBuffer(EInkDisplay::FAST_REFRESH);

  // Render the appropriate sleep screen based on settings
  switch (core.settings.sleepScreen) {
    case Settings::SleepCustom:
      renderCustomSleepScreen(core);
      break;
    case Settings::SleepCover:
      renderCoverSleepScreen(core);
      break;
    case Settings::SleepPageOverlay:
      renderPageOverlaySleepScreen();
      break;
    default:
      renderDefaultSleepScreen(core);
      break;
  }

  // Save power button duration to RTC memory for wake-up verification
  rtcPowerButtonDurationMs = core.settings.getPowerButtonDuration();

  // Persist clock to NVS so it survives a full power cycle
  sumi::SumiClock::saveToFlash();

  // Put display into low-power mode after rendering
  core.display.sleep();


  // Configure wake-up source (power button)
  esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);

  // Wait for power button release before entering deep sleep
  waitForPowerRelease();

  // Hold GPIO pins to keep LDO enabled during sleep
  gpio_deep_sleep_hold_en();

  Serial.printf("[%lu] Entering deep sleep\n", millis());

  // Enter deep sleep - this never returns
  esp_deep_sleep_start();
}

void SleepState::exit(Core& core) {
  // This should never be called - enter() calls esp_deep_sleep_start() and never returns
  Serial.println("[STATE] SleepState::exit (unexpected)");
}

StateTransition SleepState::update(Core& core) {
  // This should never be called - enter() calls esp_deep_sleep_start() and never returns
  Serial.println("[STATE] SleepState::update (unexpected - enter() should not return)");
  return StateTransition::stay(StateId::Sleep);
}

void SleepState::renderDefaultSleepScreen(const Core& core) const {
  const auto pageWidth = renderer_.getScreenWidth();
  const auto pageHeight = renderer_.getScreenHeight();

  renderer_.clearScreen(THEME.backgroundColor);
  renderer_.drawImage(SumiLogo, (pageWidth + 128) / 2, (pageHeight - 128) / 2, 128, 128);
  renderer_.drawCenteredText(THEME.uiFontId, pageHeight / 2 + 70, "SUMI", THEME.primaryTextBlack, BOLD);
  renderer_.drawCenteredText(THEME.smallFontId, pageHeight / 2 + 110, _tr(SLEEP_SLEEPING), THEME.primaryTextBlack);
  renderer_.drawCenteredText(THEME.smallFontId, pageHeight - 30, SUMI_VERSION, THEME.primaryTextBlack);

  // The content above was drawn using THEME colors. `sleepScreen` is the
  // polarity the user *wants* the sleep screen to be shown in — so we need
  // to invert iff the current theme polarity differs from the requested one.
  // (Previously this unconditionally inverted when not `SleepLight`, which
  // assumed the UI theme was always light and produced the wrong polarity
  // on dark themes — reported as "sleep screen is inverted" by the user.)
  const bool wantLight = (core.settings.sleepScreen == Settings::SleepLight);
  const bool themeIsLight = THEME.primaryTextBlack;  // light theme draws black text
  if (wantLight != themeIsLight) {
    renderer_.invertScreen();
  }

  renderer_.displayBuffer(EInkDisplay::HALF_REFRESH);
}

void SleepState::renderCustomSleepScreen(const Core& core) const {
  // Check if we have a /sleep directory
  auto dir = SdMan.open("/sleep");
  if (dir && dir.isDirectory()) {
    // Collect candidate filenames cheaply (extension check only). Pre-
    // Batch-9 followup we opened+parseHeaders'd every BMP up front to
    // pre-validate, which on a power-user's 50-screen `/sleep` was
    // 50 SD opens × ~5-20 ms each = 250 ms-1 s of lag at every sleep
    // transition. The lazy variant: collect names, pick random,
    // validate only the chosen one — bad pick falls through to the
    // tile screen. Audit #12.
    std::vector<std::string> files;
    char name[256];  // FAT32 LFN max is 255 chars; reduced from 500 to save stack
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) {
        file.close();
        continue;
      }
      file.getName(name, sizeof(name));
      file.close();
      if (name[0] == '.') continue;
      if (!FsHelpers::isBmpFile(name)) {
        Serial.printf("[%lu] [SLP] Skipping non-.bmp file name: %s\n", millis(), name);
        continue;
      }
      files.emplace_back(name);
    }
    const auto numFiles = files.size();
    if (numFiles > 0) {
      // Generate a random number between 0 and numFiles-1
      const auto randomFileIndex = random(numFiles);
      const auto filename = "/sleep/" + files[randomFileIndex];

      // Check sleep screen cache first
      uint32_t cacheKey = SleepScreenCache::computeKey(
          core.settings.sleepScreen, filename.c_str());
      if (SleepScreenCache::load(cacheKey, renderer_.getFrameBuffer())) {
        renderer_.displayBuffer(EInkDisplay::HALF_REFRESH);
        dir.close();
        return;
      }

      FsFile file;
      if (SdMan.openFileForRead("SLP", filename, file)) {
        Serial.printf("[%lu] [SLP] Randomly loading: /sleep/%s\n", millis(), files[randomFileIndex].c_str());
        delay(100);
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          // Cache the rendered 1-bit framebuffer for next time
          if (!bitmap.hasGreyscale()) {
            SleepScreenCache::save(cacheKey, renderer_.getFrameBuffer());
          }
          file.close();
          dir.close();
          return;
        }
        file.close();
      }
    }
  }
  if (dir) dir.close();

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  {
    uint32_t cacheKey = SleepScreenCache::computeKey(
        core.settings.sleepScreen, "/sleep.bmp");
    if (SleepScreenCache::load(cacheKey, renderer_.getFrameBuffer())) {
      renderer_.displayBuffer(EInkDisplay::HALF_REFRESH);
      return;
    }

    FsFile file;
    if (SdMan.openFileForRead("SLP", "/sleep.bmp", file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        Serial.printf("[%lu] [SLP] Loading: /sleep.bmp\n", millis());
        renderBitmapSleepScreen(bitmap);
        if (!bitmap.hasGreyscale()) {
          SleepScreenCache::save(cacheKey, renderer_.getFrameBuffer());
        }
        file.close();
        return;
      }
      file.close();
    }
  }

  renderDefaultSleepScreen(core);
}

void SleepState::renderCoverSleepScreen(Core& core) const {
  if (core.settings.lastBookPath[0] == '\0') {
    return renderDefaultSleepScreen(core);
  }

  std::string coverBmpPath;
  const char* bookPath = core.settings.lastBookPath;

  // Generate cover BMP based on file type (creates temporary wrapper to generate cover)
  if (FsHelpers::isXtcFile(bookPath)) {
    Xtc xtc(bookPath, SUMI_CACHE_DIR);
    if (xtc.load() && xtc.generateCoverBmp()) {
      coverBmpPath = xtc.getCoverBmpPath();
    }
  } else if (FsHelpers::isTxtFile(bookPath)) {
    Txt txt(bookPath, SUMI_CACHE_DIR);
    if (txt.load() && txt.generateCoverBmp(true)) {
      coverBmpPath = txt.getCoverBmpPath();
    }
  } else if (FsHelpers::isMarkdownFile(bookPath)) {
    Markdown md(bookPath, SUMI_CACHE_DIR);
    if (md.load() && md.generateCoverBmp(true)) {
      coverBmpPath = md.getCoverBmpPath();
    }
  } else if (FsHelpers::isEpubFile(bookPath)) {
    Epub epub(bookPath, SUMI_CACHE_DIR);
    if (epub.load() && epub.generateCoverBmp(true)) {
      coverBmpPath = epub.getCoverBmpPath();
    }
  }

  if (coverBmpPath.empty()) {
    Serial.println("[SLP] No cover BMP available");
    return renderDefaultSleepScreen(core);
  }

  FsFile file;
  if (SdMan.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      renderBitmapSleepScreen(bitmap);
      file.close();
      return;
    }
    file.close();
  }

  renderDefaultSleepScreen(core);
}

void SleepState::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  const auto pageWidth = renderer_.getScreenWidth();
  const auto pageHeight = renderer_.getScreenHeight();

  auto rect = CoverHelpers::calculateCenteredRect(bitmap.getWidth(), bitmap.getHeight(), 0, 0, pageWidth, pageHeight);

  renderer_.clearScreen();
  renderer_.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
  renderer_.displayBuffer(EInkDisplay::HALF_REFRESH);

  if (bitmap.hasGreyscale()) {
    bitmap.rewindToData();
    renderer_.clearScreen(0x00);
    renderer_.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer_.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
    renderer_.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer_.clearScreen(0x00);
    renderer_.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer_.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
    renderer_.copyGrayscaleMsbBuffers();

    renderer_.displayGrayBuffer();
    renderer_.setRenderMode(GfxRenderer::BW);
  }
}

void SleepState::renderPageOverlaySleepScreen() const {
  // The framebuffer still contains the last-rendered page content.
  // Just push it to the display with a half refresh for a clean image.
  renderer_.displayBuffer(EInkDisplay::HALF_REFRESH);
}

void SleepState::waitForPowerRelease() const {
  inputManager.update();
  while (inputManager.isPressed(InputManager::BTN_POWER)) {
    delay(50);
    inputManager.update();
  }
}

}  // namespace sumi

#include <Arduino.h>
#include <EInkDisplay.h>
#include <HardwareDetect.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <InputManager.h>
#include <SDCardManager.h>
#include <ReadingStats.h>
#include <Achievements.h>
#include <SumiClock.h>
#include <Utf8.h>
// SdFat and FS.h both define FILE_READ/FILE_WRITE - undef before LittleFS re-includes FS.h
#undef FILE_READ
#undef FILE_WRITE
#include <LittleFS.h>
#include <SPI.h>
#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>
#include <builtinFonts/reader_italic_2b.h>
// XSmall font (12pt)
#include <builtinFonts/reader_xsmall_bold_2b.h>
#include <builtinFonts/reader_xsmall_italic_2b.h>
#include <builtinFonts/reader_xsmall_regular_2b.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
// Medium font (16pt)
#include <builtinFonts/reader_medium_2b.h>
#include <builtinFonts/reader_medium_bold_2b.h>
#include <builtinFonts/reader_medium_italic_2b.h>
// Large font (18pt)
#include <builtinFonts/reader_large_2b.h>
#include <builtinFonts/reader_large_bold_2b.h>
#include <builtinFonts/reader_large_italic_2b.h>
#include <builtinFonts/small14.h>
#include <builtinFonts/ui_12.h>
#include <builtinFonts/ui_bold_12.h>

#include "Battery.h"
#include "FontManager.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "config.h"
#include "content/ContentTypes.h"
#include "ui/Elements.h"

// New refactored core system
#include "core/Core.h"
#include "core/MemoryArena.h"
#include "core/StateMachine.h"
#include "images/SumiLogo.h"
#include "states/ErrorState.h"
#include "states/FileListState.h"
#include "states/HomeState.h"
#include "states/ReaderState.h"
#include "states/SettingsState.h"
#include "states/SleepState.h"
#include "states/StartupState.h"
#include "ui/views/BootSleepViews.h"

// Plugin system
#if FEATURE_PLUGINS
#include "states/PluginHostState.h"
#include "states/PluginListState.h"
#include "plugins/PluginRenderer.h"

// Games
#if FEATURE_GAMES
#include "plugins/Minesweeper.h"
#include "plugins/Checkers.h"
#include "plugins/Solitaire.h"
#include "plugins/Sudoku.h"
#include "plugins/ChessGame.h"
#include "plugins/SumiBoy.h"
#include "plugins/Game2048App.h"
#endif

// Productivity
#include "plugins/TodoList.h"
#include "plugins/Notes.h"
#include "plugins/ToolSuite.h"
#include "plugins/Images.h"
#include "plugins/Maps.h"
#include "plugins/IfFoundApp.h"
#include "plugins/SleepPickerApp.h"
#include "plugins/PomodoroApp.h"
#include "plugins/DailyQuoteApp.h"
#include "plugins/ReadingStatsApp.h"
#include "plugins/AchievementsApp.h"

// Learning
#include "plugins/DictionaryApp.h"

#if FEATURE_FLASHCARDS
#include "plugins/Flashcards.h"
#include "ble/BleFileTransfer.h"
#include "ble/BleBridge.h"
#endif
#endif  // FEATURE_PLUGINS

#if FEATURE_BLUETOOTH
#include "ble/BleHid.h"
#endif


#define SPI_FQ 40000000
// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define UART0_RXD 20  // Used for USB connection detection

#define SD_SPI_MISO 7

#define SERIAL_INIT_DELAY_MS 10
#define SERIAL_READY_TIMEOUT_MS 3000

// =============================================================================
// Boot loop detection - persists across soft resets (ESP.restart / panic)
// but resets on full power cycle, which is the desired behavior.
//
// History: the previous design tried to keep `rtcBootTimestamp = millis()`
// at last boot and compute `now - rtcBootTimestamp < 15000ms` to detect
// "rebooted within 15 seconds". That math could never work. `millis()`
// resets to 0 every boot, so on a soft reset the unsigned subtraction
// `(small) - (large_from_previous_boot)` wrapped to ~4.29e9 — never
// less than 15000. The else-branch fired every iteration and
// `rtcBootCount` was reset to 1 instead of incrementing. The threshold
// of 4 was unreachable; the guard documented in platformio.ini did
// nothing.
//
// Counter-only design: the RTC counter increments on every soft reset.
// On cold boot (POWERON/BROWNOUT) it zeros. After we run far enough
// into setup to be confident this boot is healthy, `bootLoopGuardClear()`
// zeros it from main-loop ground truth. If the counter hits
// BOOT_LOOP_THRESHOLD before clear runs, recovery fires.
// =============================================================================
RTC_DATA_ATTR static uint8_t rtcBootCount = 0;
static constexpr uint8_t BOOT_LOOP_THRESHOLD = 4;    // 4 rapid reboots = boot loop
static bool bootLoopRecovered = false;  // Set if recovery was triggered this boot

// =============================================================================
// Dual-boot dispatch — survives soft reset via RTC retention memory.
// =============================================================================
// Why RTC instead of settings.bin: on the emulator the virtual SD card has a quirk
// where files written before ESP.restart() show up in directory listings but
// their cluster chains are gone, so the post-restart boot can't actually
// open them. Settings.bin is no exception — pendingTransition does load,
// but the lastBookPath ROM file is not openable. RTC retention memory is
// chip-internal, simulator-friendly, and survives soft reset cleanly on
// both the emulator and real hardware. It does NOT survive a power cycle, which
// is exactly what we want — a full power cycle should land you in UI.
//
// Magic value 0xB007E0 ("BOOT EMO[ulator]") guards against uninitialised
// RTC RAM after the very first cold boot (RTC RAM contents are undefined
// until first written; a stray 1 in rtcEmuBootMode would loop us into
// the emulator).
// =============================================================================
// Non-static + namespaced so the picker (which lives in `sumi`) can write
// them from another translation unit. The picker forward-declares them
// `extern` inside the `sumi` namespace.
namespace sumi {
// `extern` gives the const external linkage. Without it, `const` at
// namespace scope is internal linkage in C++ and the picker's extern
// declaration won't link.
extern const uint32_t RTC_EMU_MAGIC = 0xB007E0;
RTC_DATA_ATTR uint32_t rtcEmuMagic = 0;
RTC_DATA_ATTR char rtcEmuRomPath[200] = {};
}  // namespace sumi
using sumi::rtcEmuMagic;
using sumi::rtcEmuRomPath;
using sumi::RTC_EMU_MAGIC;

/**
 * Heap-profiling checkpoint. Prints free + largest contiguous at a named
 * subsystem boundary. Grep for "[HP]" in serial logs to build a timeline
 * of where memory goes during boot and per-session.
 */
static void heapCheckpoint(const char* label) {
  Serial.printf("[HP] %s: free=%lu largest=%lu\n",
                label, (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());
}

/**
 * Check for boot loops and recover if detected.
 * Must be called very early in startup, before any complex init.
 *
 * Soft-reset paths (ESP.restart / panic / WDT) preserve `rtcBootCount`,
 * so each consecutive crash bumps it by 1. Cold-boot paths (POWERON,
 * BROWNOUT) zero it. After `bootLoopGuardClear()` runs from the main
 * loop a few seconds in, the counter is back to 0 — meaning a
 * subsequent crash counts as the FIRST rebount, not a continuation.
 * This is the correct semantics: "user pulled the power and is back
 * = fresh start; firmware crashed mid-boot four times = loop".
 */
static void bootLoopGuard() {
  esp_reset_reason_t reason = esp_reset_reason();

  // Cold boot: RTC memory may be uninitialised. Zero the counter.
  // Other reset reasons (SW, PANIC, INT_WDT, TASK_WDT, DEEPSLEEP, ...)
  // leave it untouched so it accumulates across the loop.
  if (reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT) {
    rtcBootCount = 0;
  }

  rtcBootCount++;

  Serial.printf("[BOOT] Boot count: %u (reason: %d)\n",
                static_cast<unsigned>(rtcBootCount), reason);

  if (rtcBootCount >= BOOT_LOOP_THRESHOLD) {
    Serial.println("[BOOT] *** BOOT LOOP DETECTED - forcing recovery ***");

    // Reset the counter so recovery doesn't fire repeatedly. The recovery
    // path proper (settings reset, cache wipe, fallback to safe state)
    // is owned by states downstream — we just signal them via
    // bootLoopRecovered.
    rtcBootCount = 0;
    bootLoopRecovered = true;

    Serial.println("[BOOT] Recovery complete - continuing normal boot");
  }
}

/**
 * Call after successful boot (few seconds in) to reset the rapid-boot
 * counter. Marks "this boot reached the main loop healthy" so the next
 * soft reset starts from count=1 not count=N.
 */
static void bootLoopGuardClear() {
  rtcBootCount = 0;
}

EInkDisplay einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);
GfxRenderer renderer(einkDisplay);

// Extern references for driver wrappers
EInkDisplay& display = einkDisplay;
MappedInputManager& mappedInput = mappedInputManager;

// Core system
namespace sumi {
Core core;
}

// State instances (pre-allocated, no heap per transition)
static sumi::StartupState startupState;
static sumi::HomeState homeState(renderer);
static sumi::FileListState fileListState(renderer);
static sumi::ReaderState readerState(renderer);
static sumi::SettingsState settingsState(renderer);
static sumi::SleepState sleepState(renderer);
static sumi::ErrorState errorState(renderer);
static sumi::StateMachine stateMachine;

// Reading statistics (global, persisted to SD)
sumi::ReadingStats readingStats;

// Achievements (global, persisted to SD)
namespace sumi { Achievements achievements; }

// Plugin system instances
#if FEATURE_PLUGINS
static sumi::PluginRenderer pluginRenderer(renderer);
static sumi::PluginListState pluginListState(renderer);
static sumi::PluginHostState pluginHostState(renderer);
#endif

RTC_DATA_ATTR uint16_t rtcPowerButtonDurationMs = 400;

// Fonts - XSmall (12pt)
EpdFont readerXSmallFont(&reader_xsmall_regular_2b);
EpdFont readerXSmallBoldFont(&reader_xsmall_bold_2b);
EpdFont readerXSmallItalicFont(&reader_xsmall_italic_2b);
EpdFontFamily readerXSmallFontFamily(&readerXSmallFont, &readerXSmallBoldFont, &readerXSmallItalicFont,
                                     &readerXSmallBoldFont);

// Fonts - Small (14pt, default)
EpdFont readerFont(&reader_2b);
EpdFont readerBoldFont(&reader_bold_2b);
EpdFont readerItalicFont(&reader_italic_2b);
EpdFontFamily readerFontFamily(&readerFont, &readerBoldFont, &readerItalicFont, &readerBoldFont);

// Fonts - Medium (16pt)
EpdFont readerMediumFont(&reader_medium_2b);
EpdFont readerMediumBoldFont(&reader_medium_bold_2b);
EpdFont readerMediumItalicFont(&reader_medium_italic_2b);
EpdFontFamily readerMediumFontFamily(&readerMediumFont, &readerMediumBoldFont, &readerMediumItalicFont,
                                     &readerMediumBoldFont);

// Fonts - Large (18pt)
EpdFont readerLargeFont(&reader_large_2b);
EpdFont readerLargeBoldFont(&reader_large_bold_2b);
EpdFont readerLargeItalicFont(&reader_large_italic_2b);
EpdFontFamily readerLargeFontFamily(&readerLargeFont, &readerLargeBoldFont, &readerLargeItalicFont,
                                    &readerLargeBoldFont);

EpdFont smallFont(&small14);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui12Font(&ui_12);
EpdFont uiBold12Font(&ui_bold_12);
EpdFontFamily uiFontFamily(&ui12Font, &uiBold12Font);

bool isUsbConnected() {
  return digitalRead(UART0_RXD) == HIGH;
}

struct WakeupInfo {
  esp_reset_reason_t resetReason;
  bool isPowerButton;
};

WakeupInfo getWakeupInfo() {
  const bool usbConnected = isUsbConnected();
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  // Without USB: power button triggers a full power-on reset (not GPIO wakeup)
  // With USB: power button wakes from deep sleep via GPIO
  const bool isPowerButton =
      (!usbConnected && wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON) ||
      (usbConnected && wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP);

  return {resetReason, isPowerButton};
}

// Verify long press on wake-up from deep sleep
void verifyWakeupLongPress(esp_reset_reason_t resetReason) {
  if (resetReason == ESP_RST_SW) {
    Serial.printf("[%lu] [   ] Skipping wakeup verification (software restart)\n", millis());
    return;
  }

  // Fast path for short press mode - skip verification entirely.
  // Uses settings directly (not RTC variable) so it works even after a full power cycle
  // where RTC memory is lost. Needed because inputManager.isPressed() may take up to
  // ~500ms to return the correct state after wake-up.
  if (sumi::core.settings.shortPwrBtn == sumi::Settings::PowerSleep) {
    Serial.printf("[%lu] [   ] Skipping wakeup verification (short press mode)\n", millis());
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for the configured duration
  const auto start = millis();
  bool abort = false;
  const uint16_t requiredPressDuration = sumi::core.settings.getPowerButtonDuration();

  inputManager.update();
  // Verify the user has actually pressed
  while (!inputManager.isPressed(InputManager::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    inputManager.update();
  }

  if (inputManager.isPressed(InputManager::BTN_POWER)) {
    do {
      delay(10);
      inputManager.update();
    } while (inputManager.isPressed(InputManager::BTN_POWER) && inputManager.getHeldTime() < requiredPressDuration);
    abort = inputManager.getHeldTime() < requiredPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    // Hold all GPIO pins at their current state during deep sleep to keep the X4's LDO enabled.
    // Without this, floating pins can cause increased power draw during sleep.
    gpio_deep_sleep_hold_en();
    esp_deep_sleep_start();
  }
}

void waitForPowerRelease() {
  inputManager.update();
  while (inputManager.isPressed(InputManager::BTN_POWER)) {
    delay(50);
    inputManager.update();
  }
}

void setupDisplayAndFonts() {
  // Detect hardware (X3 vs X4) before panel init. The detection probes I2C
  // for X3-only chips (BQ27220, DS3231, QMI8658) on first boot, then caches
  // the result in NVS flash. Subsequent boots read the cache without
  // probing. If X3 is detected, configure EInkDisplay to use the X3 panel
  // geometry (792x528) and init sequence BEFORE calling begin().
  const sumi::DeviceType deviceType = sumi::HardwareDetect::detect();
  if (deviceType == sumi::DeviceType::X3) {
    einkDisplay.setDisplayX3();
    Serial.printf("[%lu] [HW] Configured EInkDisplay for X3 panel (792x528)\n", millis());
  } else {
    Serial.printf("[%lu] [HW] Using X4 panel (800x480)\n", millis());
  }
  einkDisplay.begin();
  renderer.begin();
  Serial.printf("[%lu] [   ] Display initialized\n", millis());
  renderer.insertFont(READER_FONT_ID_XSMALL, readerXSmallFontFamily);
  renderer.insertFont(READER_FONT_ID, readerFontFamily);
  renderer.insertFont(READER_FONT_ID_MEDIUM, readerMediumFontFamily);
  renderer.insertFont(READER_FONT_ID_LARGE, readerLargeFontFamily);
  renderer.insertFont(UI_FONT_ID, uiFontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  Serial.printf("[%lu] [   ] Fonts setup\n", millis());
}

void applyThemeFonts() {
  Theme& theme = THEME_MANAGER.mutableCurrent();

  // Reset UI font to builtin first in case custom font loading fails
  theme.uiFontId = UI_FONT_ID;

  // Apply custom UI font if specified (small, always safe to load)
  if (theme.uiFontFamily[0] != '\0') {
    int customUiFontId = FONT_MANAGER.getFontId(theme.uiFontFamily, UI_FONT_ID);
    if (customUiFontId != UI_FONT_ID) {
      theme.uiFontId = customUiFontId;
      Serial.printf("[%lu] [FONT] UI font: %s (ID: %d)\n", millis(), theme.uiFontFamily, customUiFontId);
    }
  }

  // Only load the reader font that matches current font size setting
  // This saves ~500KB+ of RAM by not loading all three sizes
  const char* fontFamilyName = nullptr;
  int* targetFontId = nullptr;
  int builtinFontId = 0;

  switch (sumi::core.settings.fontSize) {
    case sumi::Settings::FontXSmall:
      fontFamilyName = theme.readerFontFamilyXSmall;
      targetFontId = &theme.readerFontIdXSmall;
      builtinFontId = READER_FONT_ID_XSMALL;
      break;
    case sumi::Settings::FontMedium:
      fontFamilyName = theme.readerFontFamilyMedium;
      targetFontId = &theme.readerFontIdMedium;
      builtinFontId = READER_FONT_ID_MEDIUM;
      break;
    case sumi::Settings::FontLarge:
      fontFamilyName = theme.readerFontFamilyLarge;
      targetFontId = &theme.readerFontIdLarge;
      builtinFontId = READER_FONT_ID_LARGE;
      break;
    default:  // FontSmall
      fontFamilyName = theme.readerFontFamilySmall;
      targetFontId = &theme.readerFontId;
      builtinFontId = READER_FONT_ID;
      break;
  }

  // Reset to builtin first in case custom font loading fails
  *targetFontId = builtinFontId;

  if (fontFamilyName && fontFamilyName[0] != '\0') {
    int customFontId = FONT_MANAGER.getFontId(fontFamilyName, builtinFontId);
    if (customFontId != builtinFontId) {
      *targetFontId = customFontId;
      Serial.printf("[%lu] [FONT] Reader font: %s (ID: %d)\n", millis(), fontFamilyName, customFontId);
    }
  }
}

void showErrorScreen(const char* message) {
  renderer.clearScreen(false);
  renderer.drawCenteredText(UI_FONT_ID, 100, message, true, BOLD);
  renderer.displayBuffer();
}

// Early initialization
// Returns false if critical initialization failed
bool earlyInit() {
  // Disable task watchdog — setup takes >5s (ADC reads + SD init + display refresh)
  esp_task_wdt_deinit();

  // Only start serial if USB connected.
  //
  // GPIO 20 (UART0_RXD) floats when nothing is connected. Without an
  // explicit pull, `digitalRead(UART0_RXD)` returns whatever ambient
  // electrical noise produces — leakage from neighbouring traces, the
  // last value latched by the IO mux during the previous use of the
  // pin, etc. Spurious "USB connected" booting Serial when no host is
  // present, or vice versa. INPUT_PULLDOWN guarantees a clean LOW
  // when nothing's actively driving the line; an active USB host
  // (mA-class drive) overpowers the chip's ~45 kΩ pulldown easily,
  // so detection still works when USB is genuinely connected.
  pinMode(UART0_RXD, INPUT_PULLDOWN);
  gpio_deep_sleep_hold_dis();  // Release GPIO hold from deep sleep to allow fresh readings
  if (isUsbConnected()) {
    Serial.begin(115200);
    delay(SERIAL_INIT_DELAY_MS);  // Allow USB CDC to initialize
    unsigned long start = millis();
    while (!Serial && (millis() - start) < SERIAL_READY_TIMEOUT_MS) {
      delay(SERIAL_INIT_DELAY_MS);
    }
  }

  // Boot loop detection - must be early, before anything that could crash
  bootLoopGuard();

  inputManager.begin();

  // Initialize SPI and SD card before wakeup verification so settings are available
  SPI.begin(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, EPD_CS);
  if (!SdMan.begin()) {
    Serial.printf("[%lu] [   ] SD card initialization failed\n", millis());
    setupDisplayAndFonts();
    showErrorScreen("SD card error");
    return false;
  }

  // Migrate data directory from .papyrix to .sumi (one-time)
  if (SdMan.exists("/.papyrix") && !SdMan.exists("/.sumi")) {
    Serial.println("[MIGRATE] Renaming /.papyrix -> /.sumi");
    SdMan.rename("/.papyrix", "/.sumi");
  }


  // Detect first boot (no .sumi folder yet) — used for welcome overlay on home screen
  sumi::core.settings.isFirstBoot = !SdMan.exists(SUMI_DIR);

  // Crash report to SD (CrossPoint #1145): log reset reason for post-mortem debugging.
  // Users without USB serial can check /.sumi/crash.log for crash info.
  {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
        reason == ESP_RST_WDT || bootLoopRecovered) {
      SdMan.mkdir(SUMI_DIR);
      // Pre-audit pass this used `openFileForWrite` (O_TRUNC) and
      // then called `seekEnd()`, which silently overwrote every previous
      // crash log line on every crash — the existing entries were
      // truncated to zero before seekEnd's no-op moved the cursor to
      // position 0. Switching to O_APPEND keeps the historic log so the
      // user (or a post-mortem) can see the boot-loop sequence instead
      // of just the latest reset.
      FsFile crashFile = SdMan.open(SUMI_DIR "/crash.log",
                                    O_RDWR | O_CREAT | O_APPEND);
      if (crashFile) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[%s] Reset reason: %d%s, boot count: %d, uptime: %lu ms\n",
                 SUMI_VERSION, reason,
                 bootLoopRecovered ? " (BOOT LOOP RECOVERED)" : "",
                 rtcBootCount, millis());
        crashFile.write(buf, strlen(buf));
        crashFile.close();
        Serial.printf("[BOOT] Crash logged to %s/crash.log\n", SUMI_DIR);
      }
    }
  }

  heapCheckpoint("earlyInit.preSettings");
  // Load settings before wakeup verification - without this, a full power cycle
  // (no USB) resets RTC memory and the short power button setting is ignored
  sumi::core.settings.loadFromFile();
  heapCheckpoint("earlyInit.postSettings");
  rtcPowerButtonDurationMs = sumi::core.settings.getPowerButtonDuration();

  // Apply saved UI language to the I18n singleton
  sumi::I18n::instance().setLanguage(static_cast<sumi::Language>(sumi::core.settings.language));
  heapCheckpoint("earlyInit.postI18n");

  // Apply saved timezone to SumiClock formatters. Audit #47 follow-up.
  // Old settings files (v17 or earlier) don't have this field — the
  // visitor leaves the default 0 in place, so SumiClock keeps its
  // pre-Batch-9 UTC display until the user opts in.
  sumi::SumiClock::setTimeZoneOffsetMinutes(sumi::core.settings.timeZoneOffsetMinutes);

  // If boot loop was detected, also clear any pending transitions in settings
  // to prevent cascading boot issues (e.g., READER mode pointing to a bad file)
  if (bootLoopRecovered) {
    sumi::core.settings.pendingTransition = 0;
    sumi::core.settings.transitionReturnTo = 0;
    sumi::core.settings.saveToFile();
    Serial.println("[BOOT] Cleared pending transitions after boot loop recovery");
  }

  const auto wakeup = getWakeupInfo();
  if (wakeup.isPowerButton) {
    verifyWakeupLongPress(wakeup.resetReason);
  }

  Serial.printf("[%lu] [   ] Starting SUMI version " SUMI_VERSION "\n", millis());
  heapCheckpoint("earlyInit.start");

  // Initialize battery ADC pin with proper attenuation for 0-3.3V range
  analogSetPinAttenuation(BAT_GPIO0, ADC_11db);

  // Initialize internal flash filesystem for font storage
  if (!LittleFS.begin(false)) {
    Serial.printf("[%lu] [FS] LittleFS mount failed, attempting format\n", millis());
    if (!LittleFS.format() || !LittleFS.begin(false)) {
      Serial.printf("[%lu] [FS] LittleFS recovery failed\n", millis());
      showErrorScreen("Internal storage error");
      return false;
    }
    Serial.printf("[%lu] [FS] LittleFS formatted and mounted\n", millis());
  } else {
    Serial.printf("[%lu] [FS] LittleFS mounted\n", millis());
  }

  heapCheckpoint("earlyInit.preArena");

  // Initialize memory arena for image processing and decompression buffers.
  // This must happen before any image/ZIP operations to prevent fragmentation.
  if (!sumi::MemoryArena::init()) {
    showErrorScreen("Memory init failed");
    return false;
  }
  heapCheckpoint("earlyInit.postArena");

  return true;
}

// Dual-boot EMULATOR-mode initialization. Skips initSystem() entirely so the
// SumiBoy emulator gets the full heap (~120 KB) without competing
// with fonts, themes, plugins, reading-stats, achievements, BLE, etc. This
// is what unlocks Tetris-and-bigger ROMs on real hardware where the picker's
// in-process construction was failing the 32 KB contig allocation under
// fragmented heap.
//
// Lifecycle:
//   setup() detects pendingTransition == 3 (BootMode::EMULATOR) and the
//   lastBookPath points at a real .gb file, then calls this instead of
//   initSystem().
//
// Crash safety:
//   1. detectBootMode (or the inline equivalent here) clears the pending
//      transition flag BEFORE we touch the emulator. If init crashes, the
//      next boot is plain UI mode.
//   2. The boot-loop guard fires after 4 rapid resets in 15 s, which
//      catches any case where the flag clear races a crash.
//
// Exit:
//   Long-press Back → save transition (1 = UI mode) → ESP.restart(). The
//   next boot lands in UI mode with no emulator state.
void initEmulator(const char* romPath) {
  Serial.printf("[%lu] [BOOT-EMU] Entering EMULATOR mode for %s\n", millis(), romPath);
  Serial.printf("[%lu] [BOOT-EMU] Heap free=%lu largest=%lu\n",
                millis(),
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());

  // Bring up the display chain. earlyInit() has already initialised SD and
  // the renderer device, but the regular initSystem() owns the GfxRenderer
  // begin() call which we still need before drawing anything.
  setupDisplayAndFonts();

  // Quick "Loading..." splash so the user doesn't see a black screen for
  // the ~10-60 s warmup. uiFontId is the built-in font baked into the
  // renderer; no font loading required.
  renderer.clearScreen(true);
  renderer.drawCenteredText(0, renderer.getScreenHeight() / 2 - 20, "SumiBoy", true, REGULAR);
  renderer.drawCenteredText(0, renderer.getScreenHeight() / 2 + 20, "Loading...", true, REGULAR);
  renderer.displayBuffer();

  // Construct the emulator and run its allocator chain on the freshly-empty
  // heap. With no UI subsystems live, all 120 KB+ is available, so the 32 KB
  // contig ROM allocation lands cleanly.
  Serial.println("[BOOT-EMU] constructing SumiBoyEmulator");
  Serial.flush();
  auto* emu = new (std::nothrow) sumi::SumiBoyEmulator(pluginRenderer, romPath);
  if (!emu) {
    Serial.println("[BOOT-EMU] SumiBoyEmulator alloc FAILED — rebooting to UI");
    sumi::saveTransition(sumi::BootMode::UI);
    delay(500);
    ESP.restart();
    return;
  }
  Serial.printf("[BOOT-EMU] constructed, screen=%dx%d, calling init()\n",
                renderer.getScreenWidth(), renderer.getScreenHeight());
  Serial.flush();

  emu->init(renderer.getScreenWidth(), renderer.getScreenHeight());
  if (!emu->isReady()) {
    Serial.println("[BOOT-EMU] Emulator init failed — rebooting to UI");
    delete emu;
    sumi::saveTransition(sumi::BootMode::UI);
    delay(500);
    ESP.restart();
    return;
  }
  Serial.printf("[%lu] [BOOT-EMU] Ready, free=%lu\n",
                millis(), (unsigned long)ESP.getFreeHeap());

  // Run the emulator loop directly — no SUMI state machine, no plugin host.
  // Long-press Back → save state → reboot to UI.
  inputManager.begin();
  unsigned long lastBackPressMs = 0;
  bool backHeld = false;
  while (true) {
    esp_task_wdt_reset();
    inputManager.update();

    // Translate raw button state to PluginButton dispatch. SumiBoy expects
    // press/release events, not a polled bitmap, so check transitions.
    static uint8_t lastState = 0;
    const uint8_t state = inputManager.getState();
    const uint8_t pressed = state & ~lastState;
    const uint8_t released = lastState & ~state;
    lastState = state;

    auto dispatch = [&](uint8_t mask, sumi::PluginButton btn) {
      if (pressed & mask)  emu->handleInput(btn);
      if (released & mask) emu->handleRelease(btn);
    };
    // InputManager state bits:
    //   0 Back, 1 Confirm, 2 Left, 3 Right, 4 Up, 5 Down, 6 Power
    dispatch(1 << 1, sumi::PluginButton::Center);
    dispatch(1 << 2, sumi::PluginButton::Left);
    dispatch(1 << 3, sumi::PluginButton::Right);
    dispatch(1 << 4, sumi::PluginButton::Up);
    dispatch(1 << 5, sumi::PluginButton::Down);
    dispatch(1 << 6, sumi::PluginButton::Power);

    // Back is reserved for exit. Track held duration manually so users
    // don't accidentally bail mid-game with a tap.
    constexpr unsigned long kExitHoldMs = 1200;
    if (state & (1 << 0)) {
      if (!backHeld) {
        backHeld = true;
        lastBackPressMs = millis();
      } else if (millis() - lastBackPressMs >= kExitHoldMs) {
        Serial.println("[BOOT-EMU] Back held — exiting to UI mode");
        // Show a quick exit splash so the user sees the transition.
        renderer.clearScreen(true);
        renderer.drawCenteredText(0, renderer.getScreenHeight() / 2,
                                  "Exiting SumiBoy...", true, REGULAR);
        renderer.displayBuffer();

        delete emu;  // saves SRAM + state via cleanup()

        // User explicitly exited the emulator — clear lastBookPath so
        // the next boot doesn't auto-resume the ROM via "Last Document"
        // startup. This still writes settings.bin (lastBookPath is a
        // persistent setting), but only on user-driven exit, not the
        // boot-time hot path.
        sumi::core.settings.lastBookPath[0] = '\0';
        sumi::core.settings.saveToFile();

        sumi::saveTransition(sumi::BootMode::UI);
        delay(500);
        ESP.restart();
        return;
      }
    } else {
      backHeld = false;
    }

    if (emu->update()) {
      emu->draw();
    }

    delay(1);  // yield so background tasks (Serial flush, Wi-Fi if any) breathe
  }
}

// Unified system initialization — all states registered, no dual-boot split.
// Determines initial state from settings (StartupLastDocument → Reader, else Home).
// State transitions happen in-process via StateTransition::to() — no ESP.restart().
void initSystem() {
  Serial.printf("[%lu] [BOOT] Initializing system\n", millis());
  Serial.printf("[%lu] [BOOT] Free heap: %lu, Max block: %lu\n", millis(), ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());

  // Determine initial state before font loading (XTC content can skip custom fonts)
  sumi::StateId initialState = sumi::StateId::Home;
  bool startInReader = false;

  // Check "Last Document" startup behavior for cold-boot reader start
  if (sumi::core.settings.startupBehavior == sumi::Settings::StartupLastDocument &&
      sumi::core.settings.lastBookPath[0] != '\0' &&
      SdMan.exists(sumi::core.settings.lastBookPath)) {
    Serial.printf("[%lu] [BOOT] 'Last Document' startup: %s\n", millis(), sumi::core.settings.lastBookPath);
    startInReader = true;
    initialState = sumi::StateId::Reader;
  }

  // Crash resilience: if the previous session crashed while opening a book,
  // override startup to Home so we don't retry the same crash in a loop.
  if (startInReader && SdMan.exists(SUMI_DIR "/reader_guard.bin")) {
    FsFile guard;
    if (SdMan.openFileForRead("BOOT", SUMI_DIR "/reader_guard.bin", guard)) {
      uint8_t attempts = 0;
      int n = guard.read(&attempts, 1);
      guard.close();
      if (n == 1 && attempts > 0) {
        Serial.println("[BOOT] Reader crash guard: previous session crashed during book open, going Home");
        startInReader = false;
        initialState = sumi::StateId::Home;
        // Load the stale count so ReaderState::enter() can see it if user
        // manually opens the same book later.
        sumi::core.settings.readerLoadAttempts = attempts;
      }
    }
  }

  // Second-chance safety net: if the boot-loop guard tripped (4 rapid resets
  // within 15s), force Home even if reader_guard.bin cleanup already ran.
  // This catches crashes that happen AFTER the reader was considered
  // "successful" — e.g. the background page cache task aborting on a
  // std::bad_alloc for a huge book (War and Peace saw this: cover renders,
  // guard clears, cache task crashes, reboot, repeat).
  if (startInReader && bootLoopRecovered) {
    Serial.println("[BOOT] Boot-loop recovery — overriding Reader startup to Home");
    startInReader = false;
    initialState = sumi::StateId::Home;
    // Also write a guard so ReaderState::enter knows this book is suspect
    // if the user opens it manually later.
    SdMan.mkdir(SUMI_DIR);
    FsFile guard;
    if (SdMan.openFileForWrite("BOOT", SUMI_DIR "/reader_guard.bin", guard)) {
      const uint8_t sentinel = 1;
      guard.write(&sentinel, 1);
      SdMan.syncAndClose(guard);
    }
    sumi::core.settings.readerLoadAttempts = 1;
  }

  // Detect content type for font optimization (XTC doesn't need custom fonts)
  bool needsCustomFonts = true;
  if (startInReader) {
    sumi::ContentType contentType = sumi::detectContentType(sumi::core.settings.lastBookPath);
    needsCustomFonts = (contentType != sumi::ContentType::Xtc);
  }

  // Initialize theme and font managers
  heapCheckpoint("initSystem.preFontManager");
  FONT_MANAGER.init(renderer);
  heapCheckpoint("initSystem.postFontManager");
  THEME_MANAGER.loadTheme(sumi::core.settings.themeName);
  THEME_MANAGER.createDefaultThemeFiles();
  heapCheckpoint("initSystem.postTheme");
  Serial.printf("[%lu] [   ] Theme loaded: %s\n", millis(), THEME_MANAGER.currentThemeName());

  setupDisplayAndFonts();
  if (needsCustomFonts) {
    applyThemeFonts();
  } else {
    Serial.printf("[%lu] [BOOT] Skipping custom fonts for XTC content\n", millis());
  }

  // Preload an external CJK font at boot so flashcards, reader, and any
  // other text path all have access to it. Previously the external font
  // was only loaded by getReaderFontId() when entering the Reader state,
  // which left plugins like Flashcards without a CJK fallback (user
  // feedback: "Whether it be with flash cards or a converted epub, I
  // get ???? whenever it tries to render a CJK character").
  //
  // Lookup order:
  //  1) If the user's readerFont setting is a .bin filename, load that.
  //  2) Otherwise, scan /config/fonts/ for the first .bin file and load
  //     it. This is the "just drop a CJK font on the SD card and it
  //     works" path. Common case: user picks a Latin .epdfont for their
  //     reader and places a NotoSansJP_xx.bin alongside for CJK
  //     fallback.
  auto preloadExternalFont = [](const char* name) -> bool {
    if (!name || !*name) return false;
    if (FONT_MANAGER.loadExternalFont(name)) {
      Serial.printf("[%lu] [BOOT] Preloaded external font: %s\n", millis(), name);
      return true;
    }
    Serial.printf("[%lu] [BOOT] External font preload failed: %s\n", millis(), name);
    return false;
  };
  bool externalFontLoaded = false;
  if (sumi::core.settings.readerFont[0] &&
      FontManager::isBinFont(sumi::core.settings.readerFont)) {
    externalFontLoaded = preloadExternalFont(sumi::core.settings.readerFont);
  }
  if (!externalFontLoaded) {
    // Second chance: scan /config/fonts/ for any .bin file and load the
    // first one we find. Users who set a .epdfont as their reader font
    // still get CJK fallback if they drop any *.bin into /config/fonts/.
    auto fonts = FONT_MANAGER.listAvailableFonts();
    for (const auto& fname : fonts) {
      if (FontManager::isBinFont(fname.c_str())) {
        externalFontLoaded = preloadExternalFont(fname.c_str());
        if (externalFontLoaded) break;
      }
    }
  }


  // Show boot splash when starting at Home
  if (!startInReader) {
    ui::BootView bootView;
    bootView.setLogo(SumiLogo, 128, 128);
    bootView.setVersion(SUMI_VERSION);
    bootView.setStatus("BOOTING");
    ui::render(renderer, THEME, bootView);
  }

  // Register ALL states (unified — transitions happen in-process)
  stateMachine.registerState(&startupState);
  stateMachine.registerState(&homeState);
  stateMachine.registerState(&fileListState);
  stateMachine.registerState(&readerState);
  stateMachine.registerState(&settingsState);
  stateMachine.registerState(&sleepState);
  stateMachine.registerState(&errorState);

#if FEATURE_PLUGINS
  // Register plugin states
  pluginListState.setHostState(&pluginHostState);
  fileListState.setHostState(&pluginHostState);
  stateMachine.registerState(&pluginListState);
  stateMachine.registerState(&pluginHostState);
  heapCheckpoint("initSystem.preRegisterPlugins");

  // Register available plugins
  // Plugin factories use std::nothrow because PluginHostState::enter()
  // already null-checks the factory result and falls back to Home with
  // a "factory returned null" log. With raw `new` and -fno-exceptions,
  // a heap-exhausted plugin instantiation would abort the device — boot
  // loop guard recovers, but the user just sees a reset when they
  // tapped "Open" on a plugin. nothrow lets the existing fallback fire.
#if FEATURE_GAMES
  sumi::PluginListState::registerPlugin("Chess", "Games", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::ChessGame(pluginRenderer);
  }, CHESS_SAVE_PATH);
  sumi::PluginListState::registerPlugin("Sudoku", "Games", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::SudokuGame(pluginRenderer);
  }, SUDOKU_SAVE_PATH);
  sumi::PluginListState::registerPlugin("Minesweeper", "Games", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::MinesweeperGame(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Checkers", "Games", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::CheckersGame(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Solitaire", "Games", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::SolitaireGame(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("SumiBoy", "Games", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::SumiBoyRomPicker(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("2048", "Games", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::Game2048App(pluginRenderer);
  }, GAME2048_SAVE_PATH);
#endif  // FEATURE_GAMES

  sumi::PluginListState::registerPlugin("Notes", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::NotesApp(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Todo List", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::TodoApp(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Tools", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::ToolSuiteApp(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Pomodoro", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::PomodoroApp(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Images", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::ImagesApp(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Maps", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::MapsApp(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("If Found", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::IfFoundApp(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Daily Quote", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::DailyQuoteApp(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Sleep Screens", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::SleepPickerApp(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Reading Stats", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::ReadingStatsApp(pluginRenderer);
  });
  sumi::PluginListState::registerPlugin("Achievements", "Productivity", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::AchievementsApp(pluginRenderer);
  });

#if FEATURE_FLASHCARDS
  sumi::PluginListState::registerPlugin("Flashcards", "Learning", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::FlashcardsApp(pluginRenderer);
  });
#endif

  // Dictionary: standalone StarDict lookup with on-screen keyboard and BLE
  // keyboard input. Uses the same lib/Dictionary as the in-reader overlay,
  // so the active-dictionary setting is shared between the two entry points.
  sumi::PluginListState::registerPlugin("Dictionary", "Learning", []() -> sumi::PluginInterface* {
    return new (std::nothrow) sumi::DictionaryApp(pluginRenderer);
  });

  // Scan SD card for user-created Lua plugins
  sumi::PluginListState::scanLuaPlugins(pluginRenderer);

  Serial.printf("[BOOT] Registered %d plugins (%d built-in + %d Lua)\n",
                sumi::PluginListState::pluginCount,
                sumi::PluginListState::pluginCount - sumi::PluginListState::luaPluginCount_,
                sumi::PluginListState::luaPluginCount_);
  heapCheckpoint("initSystem.postRegisterPlugins");
#endif  // FEATURE_PLUGINS

  // Initialize core
  auto result = sumi::core.init();
  if (!result.ok()) {
    Serial.printf("[%lu] [CORE] Init failed: %s\n", millis(), sumi::errorToString(result.err));
    showErrorScreen("Core init failed");
    return;
  }

  heapCheckpoint("initSystem.preClock");

  // Initialize clock: restore from RTC or NVS flash
  sumi::SumiClock::init();
  heapCheckpoint("initSystem.postClock");

  // Load reading statistics and achievements from SD
  readingStats.load();
  heapCheckpoint("initSystem.postReadingStats");
  sumi::achievements.load();
  heapCheckpoint("initSystem.postAchievements");


  Serial.printf("[%lu] [CORE] State machine starting (initial: %s)\n", millis(),
                startInReader ? "Reader" : "Home");
  mappedInputManager.setSettings(&sumi::core.settings);
  ui::setFrontButtonLayout(sumi::core.settings.frontButtonLayout);

  // Enable periodic half-refresh in non-reader states to clear e-ink ghosting.
  // Reader manages its own counter and disables this on enter.
  if (!startInReader) {
    renderer.setPeriodicRefreshInterval(sumi::core.settings.getPagesPerRefreshValue());
  }

  // Set up reader path if starting in reader. utf8SafeCopy so a CJK
  // filename that happens to be near the 256-byte path buffer limit
  // doesn't land mid-codepoint, which would produce an 'Open failed'
  // on boot even though the file itself is fine.
  if (startInReader) {
    utf8SafeCopy(sumi::core.buf.path, sumi::core.settings.lastBookPath, sizeof(sumi::core.buf.path));
    Serial.printf("[%lu] [BOOT] Opening book: %s\n", millis(), sumi::core.buf.path);
  }

  stateMachine.init(sumi::core, initialState);

  // Force initial render
  Serial.printf("[%lu] [CORE] Forcing initial render\n", millis());
  stateMachine.update(sumi::core);

  Serial.printf("[%lu] [BOOT] After init - Free heap: %lu, Max block: %lu\n", millis(), ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());
}

void setup() {

  // Early initialization
  if (!earlyInit()) {
    return;  // Critical failure
  }

  // Dual-boot decision: pendingTransition == 3 means "boot directly into
  // SumiBoy emulator" (BootMode::EMULATOR). The picker writes this flag
  // when launching a ROM, then ESP.restarts. The full UI stack never
  // initialises — emulator gets the entire heap to itself.
  //
  // Clear the flag BEFORE running the emulator so a crash mid-init falls
  // back to UI mode on the next boot instead of looping. lastBookPath
  // stays set across the restart so the emulator loop can read it; we
  // don't clear it here, only on user-driven exit (long-press Back).
  // Dual-boot detection. On real HW, SdMan.exists is the right gate
  // (it stops a stale lastBookPath from a previous SD card from booting
  // into nothing). The boot-loop guard is the backstop in case the SD
  // state is inconsistent across a soft reset.
  // Two parallel signals (Batch 3b — audit #41):
  //   1. RTC retention memory (rtcEmuMagic + rtcEmuRomPath) — survives
  //      soft reset cleanly. Cleared on power cycle. Primary path;
  //      doesn't touch the SD card.
  //   2. /.sumi/transition.bin — dedicated transition-state file
  //      (replaces the v0.6.0 settings.pendingTransition channel).
  char sdRomPath[200] = {};
  const bool sdHasTransition = sumi::peekEmulatorTransition(sdRomPath, sizeof(sdRomPath));

  Serial.printf("[BOOT-CHECK] rtcMagic=0x%08lx rtcRom='%s' transitionRom='%s'\n",
                (unsigned long)rtcEmuMagic,
                rtcEmuRomPath,
                sdRomPath);

  const bool rtcTrigger = (rtcEmuMagic == RTC_EMU_MAGIC) && rtcEmuRomPath[0] != '\0';
  const bool sdTrigger = sdHasTransition && SdMan.exists(sdRomPath);

  if (rtcTrigger || sdTrigger) {
    char romPath[200];
    if (rtcTrigger) {
      Serial.printf("[BOOT] EMULATOR mode trigger (RTC): %s\n", rtcEmuRomPath);
      utf8SafeCopy(romPath, rtcEmuRomPath, sizeof(romPath));
    } else {
      Serial.printf("[BOOT] EMULATOR mode trigger (SD): %s\n", sdRomPath);
      utf8SafeCopy(romPath, sdRomPath, sizeof(romPath));
    }
    // Clear BOTH signals before running the emulator. If init crashes,
    // the next boot lands in UI mode regardless of which path tripped.
    // clearTransition() drops transition.bin (no settings.bin write —
    // audit #41 was specifically about boot-time settings.bin hammering).
    rtcEmuMagic = 0;
    rtcEmuRomPath[0] = '\0';
    sumi::clearTransition();
    initEmulator(romPath);
    // initEmulator never returns under normal flow — it ESP.restarts on
    // exit. If it falls through here, something is wrong; bail to UI.
    return;
  }

  initSystem();

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();

  // Re-arm the task watchdog now that setup is done. Generous 60 s timeout:
  // large-EPUB open + background cache build is legitimately long-running
  // (10 MB Les Misérables can take 20-30 s), and we don't want the WDT
  // firing in the middle of a healthy parse. A hang longer than 60 s means
  // main loop is genuinely stuck (mutex / SD / runaway plugin) — resetting
  // is strictly better than a permanently dead device. Boot-loop guard +
  // reader-crash-guard catch the repeated-crash case.
  //
  // Arduino-ESP32's bundled IDF still ships the legacy
  // `esp_task_wdt_init(timeout_seconds, panic)` signature, not the newer
  // struct-config variant. Using the legacy API here for source compat
  // with the platform @ 6.12.0 toolchain.
  esp_task_wdt_init(60, true);        // 60 s timeout, panic on fire
  esp_task_wdt_add(nullptr);          // register the main (loop) task
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;
  static bool bootGuardCleared = false;

  // Feed the 60 s WDT set up in setup(). If the main loop takes longer
  // than that between iterations the device resets — paired with the
  // reader_guard.bin crash counter so a book that triggers the reset
  // falls back to Home on the next boot.
  esp_task_wdt_reset();

  // After 10 seconds of stable operation, clear the boot loop counter
  if (!bootGuardCleared && millis() > 10000) {
    bootLoopGuardClear();
    bootGuardCleared = true;
  }

  inputManager.update();

  // Apply sunlight fading fix setting to renderer (like CrossPoint's fadingFix)
  renderer.setFadingFix(sumi::core.settings.sunlightFadingFix != 0);

  // Apply text darkness setting to renderer (controls AA glyph intensity)
  renderer.setTextDarkness(sumi::core.settings.textDarkness);

  // Screenshot: press Back + Up simultaneously to save the framebuffer as a
  // BMP to /screenshots/ on the SD card.
  //
  // Up and Down live on the SAME physical toggle switch on the Xteink X4
  // (the right-side toggle), so the original Up+Down combo inherited from
  // CrossPoint #759 was impossible to trigger on real hardware. User
  // feedback: "I wanted to take screenshots of the issue, but couldn't
  // get that working either with any combinations of the up and down
  // side buttons..." Back lives on the bottom-left toggle, Up on the
  // right-side toggle — they're on different toggles so the combo is
  // reachable two-handed. No conflicts with existing long-press handlers.
  if (inputManager.isPressed(InputManager::BTN_BACK) && inputManager.isPressed(InputManager::BTN_UP)) {
    static unsigned long lastScreenshotMs = 0;
    if (millis() - lastScreenshotMs > 3000) {  // debounce 3s
      lastScreenshotMs = millis();
      SdMan.mkdir("/screenshots");
      char path[48];
      snprintf(path, sizeof(path), "/screenshots/%lu.bmp", millis());

      // Write 1-bit BMP rotated 90deg CCW from physical (landscape)
      // to portrait. Read the panel's actual dimensions at runtime so
      // this works for both X4 (800x480 -> 480x800) and X3 (792x528
      // -> 528x792); previously hardcoded 480/800 produced a broken
      // BMP on X3.
      const uint8_t* fb = einkDisplay.getFrameBuffer();
      const int physW = einkDisplay.getDisplayWidth();   // landscape W (800 X4 / 792 X3)
      const int physH = einkDisplay.getDisplayHeight();  // landscape H (480 X4 / 528 X3)
      const int W = physH;  // portrait width comes from physical height
      const int H = physW;  // portrait height comes from physical width
      const int rowBytes = (W + 31) / 32 * 4;  // BMP row stride (4-byte aligned)
      const int imageSize = rowBytes * H;
      const int fileSize = 62 + imageSize;  // 14 (file hdr) + 40 (info hdr) + 8 (palette) + image
      const int physRowBytes = physW / 8;   // bytes per row of physical framebuffer

      FsFile f;
      if (SdMan.openFileForWrite("SCR", path, f)) {
        // BMP file header (14 bytes)
        uint8_t hdr[62] = {};
        hdr[0] = 'B'; hdr[1] = 'M';
        memcpy(hdr + 2, &fileSize, 4);
        uint32_t offset = 62; memcpy(hdr + 10, &offset, 4);
        // DIB header (40 bytes)
        uint32_t dibSize = 40; memcpy(hdr + 14, &dibSize, 4);
        int32_t w = W; memcpy(hdr + 18, &w, 4);
        int32_t h = H; memcpy(hdr + 22, &h, 4);  // positive = bottom-up
        uint16_t planes = 1; memcpy(hdr + 26, &planes, 2);
        uint16_t bpp = 1; memcpy(hdr + 28, &bpp, 2);
        memcpy(hdr + 34, &imageSize, 4);
        // Palette: black and white (8 bytes)
        hdr[54] = 0; hdr[55] = 0; hdr[56] = 0; hdr[57] = 0;       // black
        hdr[58] = 0xFF; hdr[59] = 0xFF; hdr[60] = 0xFF; hdr[61] = 0;  // white
        f.write(hdr, 62);

        // Write rows bottom-up, rotating 90deg CCW from physical to portrait.
        // Portrait W/H derived from runtime panel dims (see above) so the
        // buffer below auto-resizes for both X4 (rowBytes=60) and X3 (66).
        // Max rowBytes on any supported panel is 792/8=99 bytes + padding = 100.
        uint8_t row[100];
        for (int outY = H - 1; outY >= 0; outY--) {
          memset(row, 0, sizeof(row));
          for (int outX = 0; outX < W; outX++) {
            // Map portrait (outX, outY) → physical (inX, inY)
            int inX = outY;               // portrait Y maps to physical X
            int inY = W - 1 - outX;       // portrait X maps to inverted physical Y
            int srcByte = inY * physRowBytes + (inX / 8);
            int srcBit = 7 - (inX % 8);
            int pixel = (fb[srcByte] >> srcBit) & 1;
            if (pixel) row[outX / 8] |= (0x80 >> (outX % 8));  // BMP MSB-first
          }
          f.write(row, rowBytes);
        }
        SdMan.syncAndClose(f);
        Serial.printf("[%lu] [SCR] Screenshot saved: %s\n", millis(), path);

        // Brief on-screen confirmation banner. User reported Back+Up
        // appearing to do nothing because there was no visual cue —
        // the file was just silently written to /screenshots/. Flash
        // a partial-refresh banner at the top of the panel so the
        // user knows the gesture landed. Partial refresh avoids a
        // full-screen flash that would interrupt reading.
        const int bannerH = 36;
        renderer.fillRect(0, 0, renderer.getScreenWidth(), bannerH, false);
        renderer.drawRect(0, 0, renderer.getScreenWidth(), bannerH, true);
        renderer.drawCenteredText(UI_FONT_ID, bannerH / 2 + 4,
                                   "Screenshot saved", true, BOLD);
        renderer.displayWindow(0, 0, renderer.getScreenWidth(), bannerH, false);
      }
    }
  }

  if (Serial && millis() - lastMemPrint >= 10000) {
    Serial.printf("[%lu] [MEM] Free: %d bytes, Total: %d bytes, Min Free: %d bytes\n", millis(), ESP.getFreeHeap(),
                  ESP.getHeapSize(), ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  // Poll input and push events to queue
  sumi::core.input.poll();

#if FEATURE_BLUETOOTH && FEATURE_PLUGINS
  // Drain the plugin bridge queues unconditionally — outbound notifications
  // go out and inbound messages are dispatched to whichever plugin set the
  // handler. Runs every loop iteration so the bridge stays responsive even
  // when the foregrounded plugin doesn't implement update().
  ble_bridge::process();
#endif

#if FEATURE_BLUETOOTH
  // Check BLE inactivity timeout — disconnect when idle too long. The
  // older "reclaim memory" comment + arena reinit was a v1-arena
  // artifact: under v2 the arena lives in .bss and is never released,
  // so there's nothing to reinit. NimBLE's own deinit reclaims its own
  // pools.
  if (ble::isReady() && ble::checkInactivityTimeout()) {
    Serial.println("[BLE] Timed out, deinitialising stack");
    ble_transfer::deinit();
    ble::deinit();
  }
#endif

  // Auto-sleep after inactivity
  // Skip if BLE file transfer is active — sleeping during transfer crashes with
  // a FreeRTOS assert (xQueueGenericSend) as the settings state exits while
  // the BLE stack is holding mutexes in its write callbacks.
  const auto autoSleepTimeout = sumi::core.settings.getAutoSleepTimeoutMs();
  if (autoSleepTimeout > 0 && sumi::core.input.idleTimeMs() >= autoSleepTimeout) {
#if FEATURE_PLUGINS && FEATURE_BLUETOOTH
    // A plugin (e.g. a doorbell) can hold the device awake for up to an hour
    // per call via bridge.keep_awake(seconds). Renewed calls extend the
    // window; expiry auto-clears inside ble_bridge::process().
    if (ble_bridge::keepAwakeActive()) {
      sumi::core.input.markActivity();
    } else
#endif
    if (ble_transfer::isTransferring() || ble_transfer::isConnected()) {
      // Don't sleep — BLE is active. Actually reset the idle timer so
      // that when BLE disconnects we don't immediately fall into Sleep
      // on the next loop iteration (the user's hands may still be off
      // the device while a 10-minute transfer completes, and the old
      // comment "the next input event will reset it naturally" was
      // wrong — no input event fires on BLE disconnect alone).
      sumi::core.input.markActivity();
    } else {
      Serial.printf("[%lu] [SLP] Auto-sleep after %lu ms idle\n", millis(), autoSleepTimeout);
      stateMachine.init(sumi::core, sumi::StateId::Sleep);
      return;
    }
  }

  // Power button sleep check: track held time that excludes long rendering gaps
  // where button state changes could have been missed by inputManager
  {
    static unsigned long powerHeldSinceMs = 0;
    static unsigned long prevPowerCheckMs = 0;
    const unsigned long loopGap = loopStartTime - prevPowerCheckMs;
    prevPowerCheckMs = loopStartTime;

    if (inputManager.isPressed(InputManager::BTN_POWER)) {
      if (powerHeldSinceMs == 0 || loopGap > 100) {
        powerHeldSinceMs = loopStartTime;
      }
      if (loopStartTime - powerHeldSinceMs > sumi::core.settings.getPowerButtonDuration()) {
        stateMachine.init(sumi::core, sumi::StateId::Sleep);
        return;
      }
    } else {
      powerHeldSinceMs = 0;
    }
  }

  // Update state machine (handles transitions and rendering)
  const unsigned long activityStartTime = millis();
  stateMachine.update(sumi::core);
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      Serial.printf("[%lu] [LOOP] New max loop duration: %lu ms (activity: %lu ms)\n", millis(), maxLoopDuration,
                    activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // Increase delay after idle to save power (~4x less CPU load)
  // Idea: https://github.com/crosspoint-reader/crosspoint-reader/commit/0991782 by @ngxson (https://github.com/ngxson)
  static constexpr unsigned long kIdlePowerSavingMs = 3000;
  if (sumi::core.input.idleTimeMs() >= kIdlePowerSavingMs) {
    delay(50);
  } else {
    delay(10);
  }
}

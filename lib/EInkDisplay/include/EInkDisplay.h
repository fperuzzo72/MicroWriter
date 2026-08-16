#pragma once
#include <Arduino.h>
#include <SPI.h>

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

class EInkDisplay {
 public:
  // Constructor with pin configuration
  EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);

  // Destructor
  ~EInkDisplay();

  // Refresh modes (guarded to avoid redefinition in test builds)
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  // Set X3 panel geometry and mode. MUST be called before begin().
  // When not called, the driver defaults to X4 (800x480).
  void setDisplayX3();

  // Initialize the display hardware and driver
  void begin();

  // Display dimensions — X4 (default) compile-time constants.
  // Legacy callers still reference DISPLAY_WIDTH/HEIGHT/BUFFER_SIZE directly.
  // New code should prefer the runtime getters (getDisplayWidth etc.) so it
  // works for both X4 and X3 panels.
  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
  static constexpr uint16_t X3_DISPLAY_WIDTH = 792;
  static constexpr uint16_t X3_DISPLAY_HEIGHT = 528;
  static constexpr uint16_t X3_DISPLAY_WIDTH_BYTES = X3_DISPLAY_WIDTH / 8;
  static constexpr uint32_t X3_BUFFER_SIZE = X3_DISPLAY_WIDTH_BYTES * X3_DISPLAY_HEIGHT;
  // Framebuffer is sized for the larger of the two panels so one binary works
  // on both. max(800*480, 792*528)/8 = max(48000, 52272) = 52272 bytes.
  static constexpr uint32_t MAX_BUFFER_SIZE = 52272;

  // Runtime dimension getters — prefer these over the constexpr above.
  uint16_t getDisplayWidth() const { return displayWidth; }
  uint16_t getDisplayHeight() const { return displayHeight; }
  uint16_t getDisplayWidthBytes() const { return displayWidthBytes; }
  uint32_t getBufferSize() const { return bufferSize; }
  bool isX3Mode() const { return _x3Mode; }

  // Frame buffer operations (waitForRefresh guards against RED RAM race)
  void clearScreen(uint8_t color = 0xFF);
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false);

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  void swapBuffers();
#endif
  void setFramebuffer(const uint8_t* bwBuffer);

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);
#endif

  // turnOffScreen: Power down display after refresh. Used for sunlight fading fix
  // on SSD1677 displays without resin protection (XTEINK X4).
  void displayBuffer(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);
  // EXPERIMENTAL: Windowed update - display only a rectangular region
  void displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool turnOffScreen = false);
  void displayGrayBuffer(bool turnOffScreen = false);

  void refreshDisplay(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);

  // Hint the X3 policy to run a one-shot full resync on the next update.
  // `settlePasses` controls how many forced conditioning passes to run
  // before the normal update resumes. No-op on X4.
  // Ported from Crosspoint's open-x4-sdk X3 support.
  void requestResync(uint8_t settlePasses = 0);

  // debug function
  void grayscaleRevert();

  // LUT control
  void setCustomLUT(bool enabled, const unsigned char* lutData = nullptr);

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const { return frameBuffer; }

  // Async refresh: wait for any in-progress background refresh to complete.
  // Called automatically at the start of displayBuffer/displayWindow/displayGrayBuffer.
  void waitForRefresh();

  // Check if async refresh is in progress (non-blocking).
  // Useful for emulators/animations that want to skip rendering when the
  // display is busy rather than blocking on waitForRefresh().
  bool isRefreshing() const { return refreshPending_; }

  // Save the current framebuffer to a PBM file (desktop/test builds only)
  void saveFrameBufferAsPBM(const char* filename);

 private:
  // Pin configuration
  int8_t _sclk, _mosi, _cs, _dc, _rst, _busy;

  // Runtime display geometry (defaults to X4, flipped by setDisplayX3()).
  // Naming matches Crosspoint's open-x4-sdk so X3 code can be dropped in.
  uint16_t displayWidth = DISPLAY_WIDTH;
  uint16_t displayHeight = DISPLAY_HEIGHT;
  uint16_t displayWidthBytes = DISPLAY_WIDTH_BYTES;
  uint32_t bufferSize = BUFFER_SIZE;
  bool _x3Mode = false;

  // X3-specific state tracking (ported from Crosspoint). Unused on X4.
  bool _x3RedRamSynced = false;
  struct X3GrayState {
    bool lastBaseWasPartial = false;
    bool lsbValid = false;
  };
  X3GrayState _x3GrayState;
  uint8_t _x3InitialFullSyncsRemaining = 0;
  bool _x3ForceFullSyncNext = false;
  uint8_t _x3ForcedConditionPassesNext = 0;

  // Internal geometry setter used by setDisplayX3().
  void setDisplayDimensions(uint16_t width, uint16_t height);

  // Frame buffer sized for the LARGER of X4/X3 panels so one binary works on
  // both. Only the first `bufferSize_` bytes are used — the rest is unused
  // padding on X4 (4272 extra bytes, but keeps the code simple).
  uint8_t frameBuffer0[MAX_BUFFER_SIZE];
  uint8_t* frameBuffer;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  uint8_t frameBuffer1[MAX_BUFFER_SIZE];
  uint8_t* frameBufferActive;
#endif

  // SPI settings
  SPISettings spiSettings;

  // State
  bool isScreenOn;
  bool customLutActive;
  bool inGrayscaleMode;
  bool drawGrayscale;

  // Low-level display control
  void resetDisplay();
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  void sendData(const uint8_t* data, uint16_t length);
  void waitWhileBusy(const char* comment = nullptr);
  void initDisplayController();

  // Low-level display operations
  void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size);

#ifdef ARDUINO
  // Async refresh task — runs refreshDisplay + RED RAM sync in background
  // so the main loop can process input while the e-ink waveform runs.
  struct AsyncRefreshJob {
    RefreshMode mode = FAST_REFRESH;
    bool turnOffScreen = false;
    bool syncRedRam = false;  // Single-buffer: sync RED RAM after refresh
  };

  static void refreshTaskFunc(void* param);
  void startRefreshTask();

  TaskHandle_t refreshTaskHandle_ = nullptr;
  SemaphoreHandle_t refreshDoneSemaphore_ = nullptr;
  volatile bool refreshPending_ = false;
  AsyncRefreshJob pendingJob_;
#endif
};

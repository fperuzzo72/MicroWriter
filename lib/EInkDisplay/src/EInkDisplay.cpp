#include "EInkDisplay.h"

#include <cstring>
#include <fstream>
#include <vector>

// SSD1677 command definitions
// Initialization and reset
#define CMD_SOFT_RESET 0x12             // Soft reset
#define CMD_BOOSTER_SOFT_START 0x0C     // Booster soft-start control
#define CMD_DRIVER_OUTPUT_CONTROL 0x01  // Driver output control
#define CMD_BORDER_WAVEFORM 0x3C        // Border waveform control
#define CMD_TEMP_SENSOR_CONTROL 0x18    // Temperature sensor control

// RAM and buffer management
#define CMD_DATA_ENTRY_MODE 0x11     // Data entry mode
#define CMD_SET_RAM_X_RANGE 0x44     // Set RAM X address range
#define CMD_SET_RAM_Y_RANGE 0x45     // Set RAM Y address range
#define CMD_SET_RAM_X_COUNTER 0x4E   // Set RAM X address counter
#define CMD_SET_RAM_Y_COUNTER 0x4F   // Set RAM Y address counter
#define CMD_WRITE_RAM_BW 0x24        // Write to BW RAM (current frame)
#define CMD_WRITE_RAM_RED 0x26       // Write to RED RAM (used for fast refresh)
#define CMD_AUTO_WRITE_BW_RAM 0x46   // Auto write BW RAM
#define CMD_AUTO_WRITE_RED_RAM 0x47  // Auto write RED RAM

// Display update and refresh
#define CMD_DISPLAY_UPDATE_CTRL1 0x21  // Display update control 1
#define CMD_DISPLAY_UPDATE_CTRL2 0x22  // Display update control 2
#define CMD_MASTER_ACTIVATION 0x20     // Master activation
#define CTRL1_NORMAL 0x00              // Normal mode - compare RED vs BW for partial
#define CTRL1_BYPASS_RED 0x40          // Bypass RED RAM (treat as 0) - for full refresh

// LUT and voltage settings
#define CMD_WRITE_LUT 0x32       // Write LUT
#define CMD_GATE_VOLTAGE 0x03    // Gate voltage
#define CMD_SOURCE_VOLTAGE 0x04  // Source voltage
#define CMD_WRITE_VCOM 0x2C      // Write VCOM
#define CMD_WRITE_TEMP 0x1A      // Write temperature

// Power management
#define CMD_DEEP_SLEEP 0x10  // Deep sleep

// Custom LUT for fast refresh
const unsigned char lut_grayscale[] PROGMEM = {
    // 00 black/white
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 01 light gray
    0x54, 0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 10 gray
    0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 11 dark gray
    0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G2: A=0 B=0 C=0 D=0 RP=0 (4 frames)
    0x00, 0x00, 0x00, 0x00, 0x00,  // G3: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G4: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G5: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G6: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G7: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G8: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17, 0x41, 0xA8, 0x32, 0x30,

    // Reserved
    0x00, 0x00};

const unsigned char lut_grayscale_revert[] PROGMEM = {
    // 00 black/white
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 10 gray
    0x54, 0x54, 0x54, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 01 light gray
    0xA8, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 11 dark gray
    0xFC, 0xFC, 0xFC, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x01,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x01,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G2: A=0 B=0 C=0 D=0 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G3: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G4: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G5: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G6: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G7: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G8: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17, 0x41, 0xA8, 0x32, 0x30,

    // Reserved
    0x00, 0x00};

// ============================================================================
// X3 panel support (ported from Crosspoint master's open-x4-sdk)
//
// X3 uses a different display controller than X4's SSD1677. The full LUT set
// below is the reverse-engineered X3 waveform tuned by the community. All
// unused on X4 — PROGMEM keeps them in flash, not RAM.
// ============================================================================

// X3 full refresh LUTs
static const uint8_t lut_x3_vcom_full[] PROGMEM = {
    0x00, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_ww_full[] PROGMEM = {
    0x20, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_bw_full[] PROGMEM = {
    0xAA, 0x06, 0x02, 0x06, 0x06, 0x01, 0x80, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_wb_full[] PROGMEM = {
    0x55, 0x06, 0x02, 0x06, 0x06, 0x01, 0x40, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_bb_full[] PROGMEM = {
    0x10, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// X3 stock image-write LUTs (used for first-frame full sync)
static const uint8_t lut_x3_vcom_img[] PROGMEM = {
    0x00, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x00, 0x0C, 0x02, 0x07, 0x02, 0x01, 0x00, 0x01,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_ww_img[] PROGMEM = {
    0xA8, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x44, 0x0C, 0x02, 0x07, 0x02, 0x01, 0x04, 0x01,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_bw_img[] PROGMEM = {
    0x80, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x62, 0x0C, 0x02, 0x07, 0x02, 0x01, 0x00, 0x01,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_wb_img[] PROGMEM = {
    0x88, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x60, 0x0C, 0x02, 0x07, 0x02, 0x01, 0x00, 0x01,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_bb_img[] PROGMEM = {
    0x00, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x4A, 0x0C, 0x02, 0x07, 0x02, 0x01, 0x88, 0x01,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// X3 fast refresh LUTs (partial updates)
static const uint8_t lut_x3_vcom_fast[] PROGMEM = {
    0x00, 0x18, 0x18, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_ww_fast[] PROGMEM = {
    0x60, 0x18, 0x18, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_bw_fast[] PROGMEM = {
    0x20, 0x18, 0x18, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_wb_fast[] PROGMEM = {
    0x10, 0x18, 0x18, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_x3_bb_fast[] PROGMEM = {
    0x90, 0x18, 0x18, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void EInkDisplay::setDisplayDimensions(uint16_t width, uint16_t height) {
  displayWidth = width;
  displayHeight = height;
  displayWidthBytes = width / 8;
  bufferSize = static_cast<uint32_t>(displayWidthBytes) * height;
  _x3Mode = false;
}

void EInkDisplay::setDisplayX3() {
  setDisplayDimensions(X3_DISPLAY_WIDTH, X3_DISPLAY_HEIGHT);
  _x3Mode = true;
}

void EInkDisplay::requestResync(uint8_t settlePasses) {
  _x3ForceFullSyncNext = _x3Mode;
  _x3ForcedConditionPassesNext = _x3Mode ? settlePasses : 0;
}

EInkDisplay::EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy)
    : _sclk(sclk),
      _mosi(mosi),
      _cs(cs),
      _dc(dc),
      _rst(rst),
      _busy(busy),
      frameBuffer(nullptr),
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
      frameBufferActive(nullptr),
#endif
      isScreenOn(false),
      customLutActive(false),
      inGrayscaleMode(false),
      drawGrayscale(false) {
  if (Serial) Serial.printf("[%lu] EInkDisplay: Constructor called\n", millis());
  if (Serial)
    Serial.printf("[%lu]   SCLK=%d, MOSI=%d, CS=%d, DC=%d, RST=%d, BUSY=%d\n", millis(), sclk, mosi, cs, dc, rst, busy);
}

EInkDisplay::~EInkDisplay() {
#ifdef ARDUINO
  // Wait for any in-flight refresh to finish before tearing down the
  // task. Without this, vTaskDelete can fire while the refresh task is
  // mid-SPI transaction — CS held LOW, data partially clocked, the
  // panel waiting for the rest of a command. The next SPI consumer
  // (SD card) sees a controller stuck mid-transaction. Audit #32.
  //
  // In production EInkDisplay is a global singleton; the destructor
  // never fires (there's no `exit` on ESP32). This is defence in depth
  // for any future code path that does construct + destruct an
  // EInkDisplay (test harnesses, host-build mocks, screen-off-on-power
  // routines). Mirrors the discipline already in `deepSleep()` above.
  if (refreshTaskHandle_) {
    waitForRefresh();
    vTaskDelete(refreshTaskHandle_);
    refreshTaskHandle_ = nullptr;
  }
  if (refreshDoneSemaphore_) {
    vSemaphoreDelete(refreshDoneSemaphore_);
    refreshDoneSemaphore_ = nullptr;
  }
#endif
}

#ifdef ARDUINO
void EInkDisplay::startRefreshTask() {
  refreshDoneSemaphore_ = xSemaphoreCreateBinary();
  xSemaphoreGive(refreshDoneSemaphore_);  // Start in "done" state

  xTaskCreate(refreshTaskFunc, "eink_refresh", 4096, this, 1, &refreshTaskHandle_);
  if (Serial) Serial.printf("[%lu]   Async refresh task created\n", millis());
}

void EInkDisplay::refreshTaskFunc(void* param) {
  auto* self = static_cast<EInkDisplay*>(param);
  while (true) {
    // Wait for notification from displayBuffer
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Execute the refresh (blocks on waitWhileBusy — this is the slow part)
    self->refreshDisplay(self->pendingJob_.mode, self->pendingJob_.turnOffScreen);

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
    // Sync RED RAM with current frame for next fast refresh
    if (self->pendingJob_.syncRedRam) {
      self->setRamArea(0, 0, self->displayWidth, self->displayHeight);
      self->writeRamBuffer(CMD_WRITE_RAM_RED, self->frameBuffer, self->bufferSize);
    }
#endif

    self->refreshPending_ = false;
    xSemaphoreGive(self->refreshDoneSemaphore_);
  }
}

void EInkDisplay::waitForRefresh() {
  if (!refreshPending_) return;
  unsigned long start = millis();
  xSemaphoreTake(refreshDoneSemaphore_, portMAX_DELAY);
  xSemaphoreGive(refreshDoneSemaphore_);  // Restore for next check
  if (Serial) Serial.printf("[%lu]   waitForRefresh: waited %lu ms\n", millis(), millis() - start);
}
#else
void EInkDisplay::waitForRefresh() {}
#endif

void EInkDisplay::begin() {
  if (Serial) Serial.printf("[%lu] EInkDisplay: begin() called\n", millis());

  // CRITICAL: Reset isScreenOn flag to ensure display is properly initialized
  // This is especially important after deep sleep wake-up where the display
  // controller needs to be treated as a fresh initialization
  isScreenOn = false;

  frameBuffer = frameBuffer0;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  frameBufferActive = frameBuffer1;
#endif

  // Initialize to white (use runtime bufferSize so X3 inits the correct
  // number of bytes — X4's bufferSize is 48000, X3's is 52272).
  memset(frameBuffer0, 0xFF, bufferSize);
  // Reset X3 state (no-op on X4 but keeps state deterministic across begin()).
  _x3RedRamSynced = false;
  _x3InitialFullSyncsRemaining = _x3Mode ? 2 : 0;
  _x3ForceFullSyncNext = false;
  _x3ForcedConditionPassesNext = 0;
  _x3GrayState = {};
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  if (Serial) Serial.printf("[%lu]   Static frame buffer (%lu bytes, mode=%s)\n",
                             millis(), bufferSize, _x3Mode ? "X3" : "X4");
#else
  memset(frameBuffer1, 0xFF, bufferSize);
  if (Serial) Serial.printf("[%lu]   Static frame buffers (2 x %lu bytes, mode=%s)\n",
                             millis(), bufferSize, _x3Mode ? "X3" : "X4");
#endif

  if (Serial) Serial.printf("[%lu]   Initializing e-ink display driver...\n", millis());

  // Initialize SPI with custom pins. X3 panel needs a slower SPI clock (10MHz)
  // vs X4 (40MHz) — the X3 display controller can't sustain the higher rate.
  SPI.begin(_sclk, -1, _mosi, _cs);
  const uint32_t spiHz = _x3Mode ? 10000000 : 40000000;
  spiSettings = SPISettings(spiHz, MSBFIRST, SPI_MODE0);  // MODE0 is standard for SSD1677
  if (Serial) Serial.printf("[%lu]   SPI initialized at %lu Hz, Mode 0\n", millis(), spiHz);

  // Setup GPIO pins
  pinMode(_cs, OUTPUT);
  pinMode(_dc, OUTPUT);
  pinMode(_rst, OUTPUT);
#ifdef SUMI_WOKWI
  // Wokwi-specific: GPIO 6 is shared between EPD_BUSY and the Power button
  // on the virtual device because there aren't enough free GPIOs on the
  // ESP32-C3 DevKitM-1. Without pull-up, the "floating" BUSY pin reads LOW
  // whenever the simulator isn't driving it, and InputManager interprets
  // that as a Power-button long-press — triggering sleep mid-test after
  // ~10 seconds. INPUT_PULLUP keeps the pin sane and costs nothing on real
  // hardware (guarded by the Wokwi flag).
  pinMode(_busy, INPUT_PULLUP);
#else
  pinMode(_busy, INPUT);
#endif

  digitalWrite(_cs, HIGH);
  digitalWrite(_dc, HIGH);

  if (Serial) Serial.printf("[%lu]   GPIO pins configured\n", millis());

  // Reset display
  resetDisplay();

  // Initialize display controller
  initDisplayController();

#ifdef ARDUINO
  // Create async refresh task (safe here — FreeRTOS is fully running by begin())
  if (!refreshTaskHandle_) {
    startRefreshTask();
  }
#endif

  if (Serial) Serial.printf("[%lu]   E-ink display driver initialized\n", millis());
}

// ============================================================================
// Low-level display control methods
// ============================================================================

void EInkDisplay::resetDisplay() {
  if (Serial) Serial.printf("[%lu]   Resetting display...\n", millis());
  digitalWrite(_rst, HIGH);
  delay(20);
  digitalWrite(_rst, LOW);
  delay(2);
  digitalWrite(_rst, HIGH);
  delay(20);
  if (Serial) Serial.printf("[%lu]   Display reset complete\n", millis());
  if (_x3Mode) {
    // X3 panel needs an extra settle delay after reset.
    delay(50);
  }
}

void EInkDisplay::sendCommand(uint8_t command) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, LOW);  // Command mode
  digitalWrite(_cs, LOW);  // Select chip
  SPI.transfer(command);
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendData(uint8_t data) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);  // Data mode
  digitalWrite(_cs, LOW);   // Select chip
  SPI.transfer(data);
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendData(const uint8_t* data, uint16_t length) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);       // Data mode
  digitalWrite(_cs, LOW);        // Select chip
  SPI.writeBytes(data, length);  // Transfer all bytes
  digitalWrite(_cs, HIGH);       // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::waitWhileBusy(const char* comment) {
#ifdef SUMI_WOKWI
  // No physical SSD1677 in the emulator — BUSY pin is not real. Pretend the
  // display is always ready immediately.
  (void)comment;
  return;
#else
  unsigned long start = millis();
  if (!_x3Mode) {
    // X4: BUSY is HIGH while busy, LOW when ready.
    while (digitalRead(_busy) == HIGH) {
      delay(1);
      if (millis() - start > 10000) {
        if (Serial) Serial.printf("[%lu]   Timeout waiting for busy%s\n", millis(), comment ? comment : "");
        break;
      }
    }
  } else {
    // X3: BUSY goes HIGH briefly first, then LOW-while-busy, then HIGH-when-ready.
    // We wait for the HIGH→LOW transition first (with short timeout), then the
    // LOW→HIGH transition (long timeout). Ported from Crosspoint.
    bool sawLow = false;
    while (digitalRead(_busy) == HIGH) {
      delay(1);
      if (millis() - start > 1000) break;
    }
    if (digitalRead(_busy) == LOW) {
      sawLow = true;
      while (digitalRead(_busy) == LOW) {
        delay(1);
        if (millis() - start > 10000) break;
      }
    }
    if (!sawLow) {
      // Panel transitioned faster than our poll loop could catch — that's fine.
      return;
    }
  }
  if (comment) {
    if (Serial) Serial.printf("[%lu]   Wait complete: %s (%lu ms)\n", millis(), comment, millis() - start);
  }
#endif
}

void EInkDisplay::initDisplayController() {
  // --------------------------------------------------------------------------
  // X3 panel init (ported from Crosspoint master's open-x4-sdk).
  // X3 uses a different display controller than the SSD1677 — different
  // command set, different LUT writes, different gate driver config.
  // --------------------------------------------------------------------------
  if (_x3Mode) {
    if (Serial) Serial.printf("[%lu]   Initializing X3 display controller...\n", millis());
    sendCommand(0x00);
    sendData(0x3F);
    sendData(0x08);
    sendCommand(0x61);
    sendData(0x03);
    sendData(0x18);
    sendData(0x02);
    sendData(0x58);
    sendCommand(0x65);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendCommand(0x03);
    sendData(0x1D);
    sendCommand(0x01);
    sendData(0x07);
    sendData(0x17);
    sendData(0x3F);
    sendData(0x3F);
    sendData(0x17);
    sendCommand(0x82);
    sendData(0x1D);
    sendCommand(0x06);
    sendData(0x25);
    sendData(0x25);
    sendData(0x3C);
    sendData(0x37);
    sendCommand(0x30);
    sendData(0x09);
    sendCommand(0xE1);
    sendData(0x02);
    sendCommand(0x20);
    sendData(lut_x3_vcom_full, 42);
    sendCommand(0x21);
    sendData(lut_x3_ww_full, 42);
    sendCommand(0x22);
    sendData(lut_x3_bw_full, 42);
    sendCommand(0x23);
    sendData(lut_x3_wb_full, 42);
    sendCommand(0x24);
    sendData(lut_x3_bb_full, 42);
    isScreenOn = false;
    if (Serial) Serial.printf("[%lu]   X3 controller initialized\n", millis());
    return;
  }

  // --------------------------------------------------------------------------
  // X4 SSD1677 init (original SUMI path)
  // --------------------------------------------------------------------------
  if (Serial) Serial.printf("[%lu]   Initializing SSD1677 controller...\n", millis());

  const uint8_t TEMP_SENSOR_INTERNAL = 0x80;

  // Soft reset
  sendCommand(CMD_SOFT_RESET);
  waitWhileBusy(" CMD_SOFT_RESET");

  // Temperature sensor control (internal)
  sendCommand(CMD_TEMP_SENSOR_CONTROL);
  sendData(TEMP_SENSOR_INTERNAL);

  // Booster soft-start control (GDEQ0426T82 specific values)
  sendCommand(CMD_BOOSTER_SOFT_START);
  sendData(0xAE);
  sendData(0xC7);
  sendData(0xC3);
  sendData(0xC0);
  sendData(0x40);

  // Driver output control: set display height and scan direction.
  // Uses runtime displayHeight so both X4 (480) and X3 (528) work correctly,
  // though X3 takes the early-return branch above and never reaches here.
  sendCommand(CMD_DRIVER_OUTPUT_CONTROL);
  sendData((displayHeight - 1) % 256);  // gates A0..A7 (low byte)
  sendData((displayHeight - 1) / 256);  // gates A8..A9 (high byte)
  sendData(0x02);                       // SM=1 (interlaced), TB=0

  // Border waveform control
  sendCommand(CMD_BORDER_WAVEFORM);
  sendData(0x01);

  // Set up full screen RAM area (runtime dims)
  setRamArea(0, 0, displayWidth, displayHeight);

  if (Serial) Serial.printf("[%lu]   Clearing RAM buffers...\n", millis());
  sendCommand(CMD_AUTO_WRITE_BW_RAM);  // Auto write BW RAM
  sendData(0xF7);
  waitWhileBusy(" CMD_AUTO_WRITE_BW_RAM");

  sendCommand(CMD_AUTO_WRITE_RED_RAM);  // Auto write RED RAM
  sendData(0xF7);                       // Fill with white pattern
  waitWhileBusy(" CMD_AUTO_WRITE_RED_RAM");

  if (Serial) Serial.printf("[%lu]   SSD1677 controller initialized\n", millis());
}

void EInkDisplay::setRamArea(const uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  constexpr uint8_t DATA_ENTRY_X_INC_Y_DEC = 0x01;

  // Reverse Y coordinate (gates are reversed on this display).
  // Uses runtime displayHeight so X3 (528) and X4 (480) both work.
  y = displayHeight - y - h;

  // Set data entry mode (X increment, Y decrement for reversed gates)
  sendCommand(CMD_DATA_ENTRY_MODE);
  sendData(DATA_ENTRY_X_INC_Y_DEC);

  // Set RAM X address range (start, end) - X is in PIXELS
  sendCommand(CMD_SET_RAM_X_RANGE);
  sendData(x % 256);            // start low byte
  sendData(x / 256);            // start high byte
  sendData((x + w - 1) % 256);  // end low byte
  sendData((x + w - 1) / 256);  // end high byte

  // Set RAM Y address range (start, end) - Y is in PIXELS
  sendCommand(CMD_SET_RAM_Y_RANGE);
  sendData((y + h - 1) % 256);  // start low byte
  sendData((y + h - 1) / 256);  // start high byte
  sendData(y % 256);            // end low byte
  sendData(y / 256);            // end high byte

  // Set RAM X address counter - X is in PIXELS
  sendCommand(CMD_SET_RAM_X_COUNTER);
  sendData(x % 256);  // low byte
  sendData(x / 256);  // high byte

  // Set RAM Y address counter - Y is in PIXELS
  sendCommand(CMD_SET_RAM_Y_COUNTER);
  sendData((y + h - 1) % 256);  // low byte
  sendData((y + h - 1) / 256);  // high byte
}

void EInkDisplay::clearScreen(const uint8_t color) {
  // Wait for any async refresh to finish syncing RED RAM before modifying
  // the framebuffer. Without this, a rapid button press can trigger a new
  // render that overwrites the framebuffer while the background task is
  // still reading it for the RED RAM sync, causing e-ink ghosting.
  waitForRefresh();
  memset(frameBuffer, color, bufferSize);
}

void EInkDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                            const uint16_t h, const bool fromProgmem) {
  waitForRefresh();
  if (!frameBuffer) {
    if (Serial) Serial.printf("[%lu]   ERROR: Frame buffer not allocated!\n", millis());
    return;
  }

  // Calculate bytes per line for the image
  const uint16_t imageWidthBytes = w / 8;

  // Copy image data to frame buffer
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= displayHeight) break;

    const uint16_t destOffset = destY * displayWidthBytes + (x / 8);
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= displayWidthBytes) break;

      if (fromProgmem) {
        frameBuffer[destOffset + col] = pgm_read_byte(&imageData[srcOffset + col]);
      } else {
        frameBuffer[destOffset + col] = imageData[srcOffset + col];
      }
    }
  }

  if (Serial) Serial.printf("[%lu]   Image drawn to frame buffer\n", millis());
}

void EInkDisplay::writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size) {
  const char* bufferName = (ramBuffer == CMD_WRITE_RAM_BW) ? "BW" : "RED";
  const unsigned long startTime = millis();
  if (Serial) Serial.printf("[%lu]   Writing frame buffer to %s RAM (%lu bytes)...\n", startTime, bufferName, size);

  sendCommand(ramBuffer);
  sendData(data, size);

  const unsigned long duration = millis() - startTime;
  if (Serial) Serial.printf("[%lu]   %s RAM write complete (%lu ms)\n", millis(), bufferName, duration);
}

void EInkDisplay::setFramebuffer(const uint8_t* bwBuffer) {
  waitForRefresh();
  memcpy(frameBuffer, bwBuffer, bufferSize);
}

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
void EInkDisplay::swapBuffers() {
  uint8_t* temp = frameBuffer;
  frameBuffer = frameBufferActive;
  frameBufferActive = temp;
}
#endif

void EInkDisplay::grayscaleRevert() {
  if (!inGrayscaleMode) {
    return;
  }
  waitForRefresh();  // Ensure previous async refresh is done

  inGrayscaleMode = false;

  // Load the revert LUT
  setCustomLUT(true, lut_grayscale_revert);
  refreshDisplay(FAST_REFRESH);
  setCustomLUT(false);
}

void EInkDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  waitForRefresh();
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_BW, lsbBuffer, bufferSize);
}

void EInkDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  waitForRefresh();
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_RED, msbBuffer, bufferSize);
}

void EInkDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  waitForRefresh();
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_BW, lsbBuffer, bufferSize);
  writeRamBuffer(CMD_WRITE_RAM_RED, msbBuffer, bufferSize);
}

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
/**
 * In single buffer mode, this should be called with the previously written BW buffer
 * to reconstruct both BW and RED RAM for proper differential fast refreshes following
 * a grayscale display.
 *
 * After grayscale rendering: BW RAM has stale LSB data, RED RAM has stale MSB data.
 * Both must be synced back to the BW content to prevent the next displayBuffer()
 * from triggering grayscaleRevert() against mismatched RAM contents (causes washout).
 */
void EInkDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  waitForRefresh();
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_BW, bwBuffer, bufferSize);
  writeRamBuffer(CMD_WRITE_RAM_RED, bwBuffer, bufferSize);
  inGrayscaleMode = false;
}
#endif

#ifdef SUMI_WOKWI
// Dump the current framebuffer to serial as a hex blob framed with
// [FB-BEGIN size] ... hex ... [FB-END]. Python-side tools/wokwi/fb_to_png.py
// parses these out of wokwi-full.log and writes PNG files. Rate-limited to
// once every DUMP_INTERVAL_MS to avoid flooding the log.
static void dumpFramebufferToSerial(const uint8_t* buf, uint32_t size) {
  static uint32_t lastDumpMs = 0;
  static uint32_t dumpCounter = 0;
  const uint32_t now = millis();
  // Always dump at boot; afterwards at most every 250 ms
  if (lastDumpMs != 0 && (now - lastDumpMs) < 250) return;
  lastDumpMs = now;

  if (!Serial) return;
  Serial.printf("[FB-BEGIN %u size %u]\n", dumpCounter++, size);
  // Hex-encode; newlines every 64 bytes to keep lines manageable.
  char line[129];
  static const char hexChars[] = "0123456789abcdef";
  for (uint32_t i = 0; i < size; i++) {
    uint8_t b = buf[i];
    uint32_t pos = (i & 63) * 2;
    line[pos    ] = hexChars[b >> 4];
    line[pos + 1] = hexChars[b & 0x0f];
    if ((i & 63) == 63 || i == size - 1) {
      line[pos + 2] = '\0';
      Serial.println(line);
    }
  }
  Serial.println("[FB-END]");
}
#endif

void EInkDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) {
#ifdef SUMI_WOKWI
  dumpFramebufferToSerial(frameBuffer, bufferSize);
#endif
  if (!isScreenOn && mode == FAST_REFRESH) {
    // Force half refresh if screen is off - FAST_REFRESH requires valid
    // previous frame data in RED RAM which may be stale after power-off
    mode = HALF_REFRESH;
  }

  // Wait for any previous async refresh before touching SPI/display
  waitForRefresh();

  // If currently in grayscale mode, revert first to black/white
  if (inGrayscaleMode) {
    inGrayscaleMode = false;
    grayscaleRevert();
  }

  // --------------------------------------------------------------------------
  // X3 panel refresh path (ported from Crosspoint master's open-x4-sdk).
  //
  // X3 uses a completely different display controller and refresh command
  // sequence than X4's SSD1677. The X3 controller stores the previous frame
  // in its own RAM (CMD 0x10) and computes differential updates against it,
  // so we only send new frame data via CMD 0x13. LUTs are swapped per
  // refresh type: img LUTs for full sync, full LUTs for fast diff.
  //
  // Runs synchronously here — bypasses SUMI's async refresh task entirely
  // since the X3 sequence is inherently sequential (CTRL state depends on
  // each prior step). Returns early to skip the X4 SSD1677 path below.
  // --------------------------------------------------------------------------
  if (_x3Mode) {
    const bool fastMode = (mode != FULL_REFRESH);
    uint8_t row[128];  // Max X3 width byte count: 792/8 = 99; 128 is safe.

    auto sendCommandDataX3 = [&](uint8_t cmd, const uint8_t* data, uint16_t len) {
      SPI.beginTransaction(spiSettings);
      digitalWrite(_cs, LOW);
      digitalWrite(_dc, LOW);
      SPI.transfer(cmd);
      if (len > 0 && data != nullptr) {
        digitalWrite(_dc, HIGH);
        SPI.writeBytes(data, len);
      }
      digitalWrite(_cs, HIGH);
      SPI.endTransaction();
    };
    auto sendCommandDataByteX3 = [&](uint8_t cmd, uint8_t d0, uint8_t d1) {
      const uint8_t d[2] = {d0, d1};
      sendCommandDataX3(cmd, d, 2);
    };
    // X3 panel data is scanned bottom-up — mirror rows before sending.
    auto sendMirroredPlane = [&](const uint8_t* plane, bool invertBits) {
      for (uint16_t y = 0; y < displayHeight; y++) {
        const uint16_t srcY = static_cast<uint16_t>(displayHeight - 1 - y);
        const uint8_t* src = plane + static_cast<uint32_t>(srcY) * displayWidthBytes;
        for (uint16_t x = 0; x < displayWidthBytes; x++) {
          row[x] = invertBits ? static_cast<uint8_t>(~src[x]) : src[x];
        }
        sendData(row, displayWidthBytes);
      }
    };

    const bool forcedFullSync = _x3ForceFullSyncNext;
    const bool doFullSync = !fastMode || !_x3RedRamSynced ||
                            _x3InitialFullSyncsRemaining > 0 || forcedFullSync;

    if (Serial) {
      Serial.printf("[%lu]   X3_%s\n", millis(), doFullSync ? "FULL" : "FAST");
    }
    _x3GrayState.lastBaseWasPartial = !doFullSync;

    if (doFullSync) {
      // Full sync: write img LUTs, send inverted frame to both RAMs.
      sendCommandDataX3(0x20, lut_x3_vcom_img, 42);
      sendCommandDataX3(0x21, lut_x3_ww_img, 42);
      sendCommandDataX3(0x22, lut_x3_bw_img, 42);
      sendCommandDataX3(0x23, lut_x3_wb_img, 42);
      sendCommandDataX3(0x24, lut_x3_bb_img, 42);

      sendCommand(0x13);
      sendMirroredPlane(frameBuffer, true);
      sendCommand(0x10);
      sendMirroredPlane(frameBuffer, true);

      sendCommandDataByteX3(0x50, 0xA9, 0x07);
    } else {
      // Fast differential: full LUTs, only write new frame to 0x13.
      // Controller diffs it against 0x10 (previous frame).
      sendCommandDataX3(0x20, lut_x3_vcom_full, 42);
      sendCommandDataX3(0x21, lut_x3_ww_full, 42);
      sendCommandDataX3(0x22, lut_x3_bw_full, 42);
      sendCommandDataX3(0x23, lut_x3_wb_full, 42);
      sendCommandDataX3(0x24, lut_x3_bb_full, 42);

      sendCommand(0x13);
      sendMirroredPlane(frameBuffer, false);

      sendCommandDataByteX3(0x50, 0x29, 0x07);
    }

    if (!isScreenOn || doFullSync) {
      sendCommand(0x04);  // Power on
      waitWhileBusy(" X3_CMD04");
      isScreenOn = true;
    }

    if (Serial) Serial.printf("[%lu]   X3_TRIGGER=0x12\n", millis());
    sendCommand(0x12);
    waitWhileBusy(" X3_CMD12");

    if (turnOffScreen) {
      sendCommand(0x02);
      waitWhileBusy(" X3_CMD02_POWEROFF");
      isScreenOn = false;
    }

    if (!fastMode) delay(200);

    // Light post-condition pass after first full sync improves early-page
    // quality on X3 without the old 6-pass overhead.
    uint8_t postConditionPasses = 0;
    if (doFullSync) {
      if (forcedFullSync) postConditionPasses = _x3ForcedConditionPassesNext;
      else if (_x3InitialFullSyncsRemaining == 1) postConditionPasses = 1;
    }

    if (postConditionPasses > 0) {
      const uint16_t xStart = 0;
      const uint16_t xEnd = static_cast<uint16_t>(displayWidth - 1);
      const uint16_t yStart = 0;
      const uint16_t yEnd = static_cast<uint16_t>(displayHeight - 1);
      const uint8_t w[9] = {
          static_cast<uint8_t>(xStart >> 8), static_cast<uint8_t>(xStart & 0xFF),
          static_cast<uint8_t>(xEnd >> 8),   static_cast<uint8_t>(xEnd & 0xFF),
          static_cast<uint8_t>(yStart >> 8), static_cast<uint8_t>(yStart & 0xFF),
          static_cast<uint8_t>(yEnd >> 8),   static_cast<uint8_t>(yEnd & 0xFF), 0x01};

      sendCommandDataX3(0x20, lut_x3_vcom_full, 42);
      sendCommandDataX3(0x21, lut_x3_ww_full, 42);
      sendCommandDataX3(0x22, lut_x3_bw_full, 42);
      sendCommandDataX3(0x23, lut_x3_wb_full, 42);
      sendCommandDataX3(0x24, lut_x3_bb_full, 42);
      sendCommandDataByteX3(0x50, 0x29, 0x07);

      for (uint8_t i = 0; i < postConditionPasses; i++) {
        sendCommand(0x91);
        sendCommandDataX3(0x90, w, 9);
        sendCommand(0x13);
        sendMirroredPlane(frameBuffer, false);
        sendCommand(0x92);
        if (!isScreenOn) {
          sendCommand(0x04);
          waitWhileBusy(" X3_CMD04(cond)");
          isScreenOn = true;
        }
        sendCommand(0x12);
        waitWhileBusy(" X3_CMD12(cond)");
      }
    }

    // Sync RED RAM (0x10) with non-inverted current frame for next fast diff.
    sendCommand(0x10);
    sendMirroredPlane(frameBuffer, false);
    _x3RedRamSynced = true;

    if (doFullSync && _x3InitialFullSyncsRemaining > 0) {
      _x3InitialFullSyncsRemaining--;
    }
    _x3ForceFullSyncNext = false;
    _x3ForcedConditionPassesNext = 0;
    return;  // Skip X4 path below
  }

  // --------------------------------------------------------------------------
  // X4 SSD1677 refresh path (original SUMI code, with async task dispatch)
  // --------------------------------------------------------------------------
  // Set up full screen RAM area
  setRamArea(0, 0, displayWidth, displayHeight);

  if (mode != FAST_REFRESH) {
    // For full/half refresh, write to both buffers before refresh
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, bufferSize);
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, bufferSize);
  } else {
    // For fast refresh: write BW (new frame). RED already has previous frame.
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, bufferSize);
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBufferActive, bufferSize);
#endif
  }

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  swapBuffers();
#endif

#ifdef ARDUINO
  // Dispatch refresh + RED sync to background task. The display controller
  // has the frame data in its own RAM. The e-ink waveform (80-3000ms) runs
  // in the background while the main loop processes input.
  //
  // For single-buffer mode, the async task also syncs RED RAM after refresh.
  // This uses the framebuffer contents at that point — safe because states
  // don't modify the framebuffer until the next user-triggered render, and
  // waitForRefresh() blocks at the start of the next displayBuffer() call.
  xSemaphoreTake(refreshDoneSemaphore_, portMAX_DELAY);
  pendingJob_.mode = mode;
  pendingJob_.turnOffScreen = turnOffScreen;
  pendingJob_.syncRedRam = true;
  refreshPending_ = true;
  xTaskNotifyGive(refreshTaskHandle_);

#else
  // Desktop/test builds: synchronous refresh
  refreshDisplay(mode, turnOffScreen);

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, bufferSize);
#endif
#endif
}

// EXPERIMENTAL: Windowed update support
// Displays only a rectangular region of the frame buffer, preserving the rest of the screen.
// Requirements: x and w must be byte-aligned (multiples of 8 pixels)
void EInkDisplay::displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool turnOffScreen) {
  waitForRefresh();  // Ensure previous async refresh is done
  if (Serial) Serial.printf("[%lu]   Displaying window at (%d,%d) size (%dx%d)\n", millis(), x, y, w, h);

  // Validate bounds
  if (x + w > displayWidth || y + h > displayHeight) {
    if (Serial) Serial.printf("[%lu]   ERROR: Window bounds exceed display dimensions!\n", millis());
    return;
  }

  // Validate byte alignment
  if (x % 8 != 0 || w % 8 != 0) {
    if (Serial) Serial.printf("[%lu]   ERROR: Window x and width must be byte-aligned (multiples of 8)!\n", millis());
    return;
  }

  if (!frameBuffer) {
    if (Serial) Serial.printf("[%lu]   ERROR: Frame buffer not allocated!\n", millis());
    return;
  }

  // displayWindow is not supported while the rest of the screen has grayscale content, revert it
  if (inGrayscaleMode) {
    inGrayscaleMode = false;
    grayscaleRevert();
  }

  // Calculate window buffer size
  const uint16_t windowWidthBytes = w / 8;
  const uint32_t windowBufferSize = windowWidthBytes * h;

  if (Serial)
    Serial.printf("[%lu]   Window buffer size: %lu bytes (%d x %d pixels)\n", millis(), windowBufferSize, w, h);

  // Allocate temporary buffer on stack
  std::vector<uint8_t> windowBuffer(windowBufferSize);

  // Extract window region from frame buffer
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t srcY = y + row;
    const uint16_t srcOffset = srcY * displayWidthBytes + (x / 8);
    const uint16_t dstOffset = row * windowWidthBytes;
    memcpy(&windowBuffer[dstOffset], &frameBuffer[srcOffset], windowWidthBytes);
  }

  // Configure RAM area for window
  setRamArea(x, y, w, h);

  // Write to BW RAM (current frame)
  writeRamBuffer(CMD_WRITE_RAM_BW, windowBuffer.data(), windowBufferSize);

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  // Dual buffer: Extract window from frameBufferActive (previous frame)
  std::vector<uint8_t> previousWindowBuffer(windowBufferSize);
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t srcY = y + row;
    const uint16_t srcOffset = srcY * displayWidthBytes + (x / 8);
    const uint16_t dstOffset = row * windowWidthBytes;
    memcpy(&previousWindowBuffer[dstOffset], &frameBufferActive[srcOffset], windowWidthBytes);
  }
  writeRamBuffer(CMD_WRITE_RAM_RED, previousWindowBuffer.data(), windowBufferSize);
#endif

  // Perform fast refresh
  refreshDisplay(FAST_REFRESH, turnOffScreen);

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  // Post-refresh: Sync RED RAM with current window (for next fast refresh)
  setRamArea(x, y, w, h);
  writeRamBuffer(CMD_WRITE_RAM_RED, windowBuffer.data(), windowBufferSize);
#endif

  if (Serial) Serial.printf("[%lu]   Window display complete\n", millis());
}

void EInkDisplay::displayGrayBuffer(const bool turnOffScreen) {
  waitForRefresh();  // Ensure previous async refresh is done
  drawGrayscale = false;
  inGrayscaleMode = true;

  // activate the custom LUT for grayscale rendering and refresh
  setCustomLUT(true, lut_grayscale);
  refreshDisplay(FAST_REFRESH, turnOffScreen);
  setCustomLUT(false);
}

void EInkDisplay::refreshDisplay(const RefreshMode mode, const bool turnOffScreen) {
  // Configure Display Update Control 1
  sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
  sendData((mode == FAST_REFRESH) ? CTRL1_NORMAL : CTRL1_BYPASS_RED);  // Configure buffer comparison mode

  // best guess at display mode bits:
  // bit | hex | name                    | effect
  // ----+-----+--------------------------+-------------------------------------------
  // 7   | 80  | CLOCK_ON                | Start internal oscillator
  // 6   | 40  | ANALOG_ON               | Enable analog power rails (VGH/VGL drivers)
  // 5   | 20  | TEMP_LOAD               | Load temperature (internal or I2C)
  // 4   | 10  | LUT_LOAD                | Load waveform LUT
  // 3   | 08  | MODE_SELECT             | Mode 1/2
  // 2   | 04  | DISPLAY_START           | Run display
  // 1   | 02  | ANALOG_OFF_PHASE        | Shutdown step 1 (undocumented)
  // 0   | 01  | CLOCK_OFF               | Disable internal oscillator

  // Select appropriate display mode based on refresh type
  uint8_t displayMode = 0x00;

  // Enable counter and analog if not already on
  if (!isScreenOn) {
    isScreenOn = true;
    displayMode |= 0xC0;  // Set CLOCK_ON and ANALOG_ON bits
  }

  // Turn off screen if requested
  if (turnOffScreen) {
    isScreenOn = false;
    displayMode |= 0x03;  // Set ANALOG_OFF_PHASE and CLOCK_OFF bits
  }

  if (mode == FULL_REFRESH) {
    displayMode |= 0x34;
  } else if (mode == HALF_REFRESH) {
    // Write high temp to the register for a faster refresh
    sendCommand(CMD_WRITE_TEMP);
    sendData(0x5A);
    displayMode |= 0xD4;
  } else {  // FAST_REFRESH
    displayMode |= customLutActive ? 0x0C : 0x1C;
  }

  // Power on and refresh display
  const char* refreshType = (mode == FULL_REFRESH) ? "full" : (mode == HALF_REFRESH) ? "half" : "fast";
  if (Serial) Serial.printf("[%lu]   Powering on display 0x%02X (%s refresh)...\n", millis(), displayMode, refreshType);
  sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
  sendData(displayMode);

  sendCommand(CMD_MASTER_ACTIVATION);

  // Wait for display to finish updating
  if (Serial) Serial.printf("[%lu]   Waiting for display refresh...\n", millis());
  waitWhileBusy(refreshType);
}

void EInkDisplay::setCustomLUT(const bool enabled, const unsigned char* lutData) {
  if (enabled) {
    if (Serial) Serial.printf("[%lu]   Loading custom LUT...\n", millis());

    // Load custom LUT (first 105 bytes: VS + TP/RP + frame rate)
    sendCommand(CMD_WRITE_LUT);
    for (uint16_t i = 0; i < 105; i++) {
      sendData(pgm_read_byte(&lutData[i]));
    }

    // Set voltage values from bytes 105-109
    sendCommand(CMD_GATE_VOLTAGE);  // VGH
    sendData(pgm_read_byte(&lutData[105]));

    sendCommand(CMD_SOURCE_VOLTAGE);         // VSH1, VSH2, VSL
    sendData(pgm_read_byte(&lutData[106]));  // VSH1
    sendData(pgm_read_byte(&lutData[107]));  // VSH2
    sendData(pgm_read_byte(&lutData[108]));  // VSL

    sendCommand(CMD_WRITE_VCOM);  // VCOM
    sendData(pgm_read_byte(&lutData[109]));

    customLutActive = true;
    if (Serial) Serial.printf("[%lu]   Custom LUT loaded\n", millis());
  } else {
    customLutActive = false;
    if (Serial) Serial.printf("[%lu]   Custom LUT disabled\n", millis());
  }
}

void EInkDisplay::deepSleep() {
  waitForRefresh();  // Ensure refresh completes before sleep
  if (Serial) Serial.printf("[%lu]   Preparing display for deep sleep...\n", millis());

  // First, power down the display properly
  // This shuts down the analog power rails and clock
  if (isScreenOn) {
    sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
    sendData(CTRL1_BYPASS_RED);  // Normal mode

    sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
    sendData(0x03);  // Set ANALOG_OFF_PHASE (bit 1) and CLOCK_OFF (bit 0)

    sendCommand(CMD_MASTER_ACTIVATION);

    // Wait for the power-down sequence to complete
    waitWhileBusy(" display power-down");

    isScreenOn = false;
  }

  // Now enter deep sleep mode
  if (Serial) Serial.printf("[%lu]   Entering deep sleep mode...\n", millis());
  sendCommand(CMD_DEEP_SLEEP);
  sendData(0x01);  // Enter deep sleep
}

void EInkDisplay::saveFrameBufferAsPBM(const char* filename) {
#ifndef ARDUINO
  const uint8_t* buffer = getFrameBuffer();

  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    if (Serial) Serial.printf("Failed to open %s for writing\n", filename);
    return;
  }

  // Rotate the image 90 degrees counterclockwise when saving
  // Original buffer: 800x480 (landscape)
  // Output image: 480x800 (portrait)
  const int DISPLAY_WIDTH_LOCAL = displayWidth;    // 800
  const int DISPLAY_HEIGHT_LOCAL = displayHeight;  // 480
  const int DISPLAY_WIDTH_BYTES_LOCAL = DISPLAY_WIDTH_LOCAL / 8;

  file << "P4\n";  // Binary PBM
  file << DISPLAY_HEIGHT_LOCAL << " " << DISPLAY_WIDTH_LOCAL << "\n";

  // Create rotated buffer
  std::vector<uint8_t> rotatedBuffer((DISPLAY_HEIGHT_LOCAL / 8) * DISPLAY_WIDTH_LOCAL, 0);

  for (int outY = 0; outY < DISPLAY_WIDTH_LOCAL; outY++) {
    for (int outX = 0; outX < DISPLAY_HEIGHT_LOCAL; outX++) {
      int inX = outY;
      int inY = DISPLAY_HEIGHT_LOCAL - 1 - outX;

      int inByteIndex = inY * DISPLAY_WIDTH_BYTES_LOCAL + (inX / 8);
      int inBitPosition = 7 - (inX % 8);
      bool isWhite = (buffer[inByteIndex] >> inBitPosition) & 1;

      int outByteIndex = outY * (DISPLAY_HEIGHT_LOCAL / 8) + (outX / 8);
      int outBitPosition = 7 - (outX % 8);
      if (!isWhite) {  // Invert: e-ink white=1 -> PBM black=1
        rotatedBuffer[outByteIndex] |= (1 << outBitPosition);
      }
    }
  }

  file.write(reinterpret_cast<const char*>(rotatedBuffer.data()), rotatedBuffer.size());
  file.close();
  if (Serial) Serial.printf("Saved framebuffer to %s\n", filename);
#else
  (void)filename;
  if (Serial) Serial.println("saveFrameBufferAsPBM is not supported on Arduino builds.");
#endif
}

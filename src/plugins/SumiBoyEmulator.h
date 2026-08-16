#pragma once
/**
 * @file SumiBoyEmulator.h
 * @brief Game Boy / Game Boy Color emulator plugin for SUMI.
 *
 * This is a thin plugin-host adapter around the X4_emulator GBEmulator
 * core. The actual CPU/PPU/MBC/CGB/RTC/bank-cache/patch code lives in
 * `plugins/gb/gb_emulator.h` (~1700 lines ported wholesale from the
 * X4 standalone retro suite). This wrapper handles:
 *
 *   - PluginInterface lifecycle (init / cleanup / draw / handleInput /
 *     handleRelease / update)
 *   - Button mapping from the device's 7-button layout onto the GB's
 *     D-pad + A/B/Select/Start, with the double-tap-Center → Select
 *     gesture SUMI already shipped
 *   - Framebuffer bridging — the core produces 160×144 shade values
 *     (0-3); the wrapper applies a Bayer 4×4 dither and scales 3× into
 *     SUMI's 480×800 portrait framebuffer
 *   - E-ink refresh scheduling (dirty-frame hashing, adaptive frame
 *     skip, FAST vs FULL refresh)
 *   - Save state + SRAM routed through SdMan (SUMI's SD abstraction)
 *
 * Previous version of this file implemented the whole SM83+MBC3 core
 * inline (~1600 lines). That version only supported ROM-only + MBC3
 * cartridges, no CGB, no RTC, no big-ROM bank cache, no Pokemon Red
 * e-ink patches, and no cheats. The ported core adds all of those.
 */

#include <Arduino.h>

#include "../config.h"

#if FEATURE_PLUGINS && FEATURE_GAMES

#include <SdFat.h>

#include "PluginInterface.h"
#include "PluginRenderer.h"
#include "gb/gb_emulator.h"

namespace sumi {

class SumiBoyEmulator : public PluginInterface {
 public:
  explicit SumiBoyEmulator(PluginRenderer& renderer, const char* romPath);
  ~SumiBoyEmulator() override;

  // --- PluginInterface ---
  void init(int screenW, int screenH) override;
  void cleanup() override;
  void draw() override;
  bool handleInput(PluginButton btn) override;
  bool handleRelease(PluginButton btn) override;
  bool update() override;
  PluginRunMode runMode() const override { return PluginRunMode::WithUpdate; }
  bool wantsLandscape() const override { return false; }
  bool handlesOwnRefresh() const override { return true; }
  const char* name() const override { return "SumiBoy"; }

  // True after init() succeeded end-to-end. Picker checks this to
  // distinguish "emulator running" from "emulator construction failed,
  // buffers freed" so it can show an error instead of hanging on the
  // Loading screen.
  bool isReady() const { return ready_; }

 private:
  PluginRenderer& disp_;
  GBEmulator* emu_ = nullptr;      // The ported GB core. Heap-allocated.
  uint8_t* romData_ = nullptr;     // RAM-resident ROM (for small ROMs).
  uint8_t* vram1_ = nullptr;       // CGB VRAM bank 1 (8 KB, heap, if CGB).
  uint8_t* wramExtra_ = nullptr;   // CGB WRAM banks 2-7 (24 KB, heap, if CGB).
  uint8_t* bankCacheBuf_ = nullptr;  // Streaming bank cache buffer (N × 16 KB).
  int* bankCacheMap_ = nullptr;      // Streaming bank cache LRU map.

  // --- Paths (filled by constructor) ---
  char romPath_[80] = {};
  char romName_[32] = {};
  char sramPath_[80] = {};
  char statePath_[80] = {};

  // --- Plugin-side button state (held across update ticks) ---
  // D-pad bits: Right=0, Left=1, Up=2, Down=3
  // Button bits: A=0, B=1, Select=2, Start=3
  uint8_t pendingDpad_ = 0x0F;
  uint8_t pendingButtons_ = 0x0F;

  // Double-tap detection for Center → Select.
  unsigned long lastCenterMs_ = 0;
  static constexpr unsigned long DOUBLE_TAP_MS = 400;

  // --- Display + frame scheduling ---
  int screenW_ = 0, screenH_ = 0;
  uint32_t lastFrameHash_ = 0;
  int consecutiveSkips_ = 0;
  bool firstDraw_ = true;
  // True only after init() completes end-to-end (all buffers allocated,
  // ROM loaded, warmup finished). cleanup() skips saveState/saveSRAM when
  // false to avoid dereferencing partially-allocated emulator state —
  // that was the Load-Access-Fault crash path after emu_->init() alloc
  // failure.
  bool ready_ = false;

  // Dither LUT: packed 8-bit column pattern for each (shade, bayer-col).
  uint8_t shade_byte_[4][4] = {};

  // Auto-save counters.
  int saveCounter_ = 0;
  int sramCounter_ = 0;
  bool buttonPressedSinceSave_ = false;

  // Clashes with #define GB_W / GB_H in plugins/gb/gb_emulator.h, so use
  // underscored wrapper-local names instead.
  static constexpr int WRAP_GB_W = 160;
  static constexpr int WRAP_GB_H = 144;
  static constexpr int DISP_GAME_W = WRAP_GB_W * 3;  // 480
  static constexpr int DISP_GAME_H = WRAP_GB_H * 3;  // 432
  static constexpr int FRAMES_PER_REFRESH = 10;
  static constexpr int FRAMES_PER_REFRESH_IDLE = 4;
  static constexpr int AUTOSAVE_INTERVAL = 300;
  static constexpr int SRAM_SAVE_INTERVAL = 100;

  // --- Helpers ---
  bool loadROM();                       // Reads header, decides RAM-vs-cache, opens romFile_
  void renderToDisplay();               // 160x144 shade -> 480x432 Bayer-dithered
  void drawControls();                  // Reused from prior version — draws GB control legend
  void initDitherLUT();
  bool saveSRAM();
  bool loadSRAM();
  bool saveState();
  bool loadState();
};

}  // namespace sumi

#endif  // FEATURE_PLUGINS && FEATURE_GAMES

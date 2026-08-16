#include "SumiBoyEmulator.h"

#include <Utf8.h>

#include "../config.h"

#if FEATURE_PLUGINS && FEATURE_GAMES

#include <Arduino.h>
#include <SDCardManager.h>
#include <EInkDisplay.h>
#include <GfxRenderer.h>
#include <esp_task_wdt.h>

#include "../core/MemoryArena.h"
#include "../ThemeManager.h"

namespace sumi {

// 4x4 ordered Bayer matrix for dithering.
static const uint8_t bayer4x4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5}};

// Game Boy shade (0-3) → display intensity (0..16). Higher = darker ink.
static const uint8_t shadeIntensity[4] = {0, 5, 11, 16};

// Bank cache sizing for big ROMs. The X4 suite uses 8 slots; SUMI has a
// tighter heap budget (the core plugin host keeps a lot more state around)
// so we start with 4 × 16 KB = 64 KB. Pokemon Red + Crystal both fit in
// 4 slots with good locality thanks to per-bank usage patterns.
// 1 slot × 16 KB + bank0 (16 KB) = 32 KB of ROM data resident. On
// ESP32-C3 the 77 KB largest-contiguous block needs to hold the ROM
// data AND leave enough room for the 47 KB emu internal buffers
// (framebuf 23 + vram 8 + wram 8 + sram 8). 32 + 47 = 79 KB, which
// squeezes in once fragments are counted. 2 slots pushed streaming to
// 48 KB and emu_->init() started failing. Single-slot cache means a
// bank switch touches SD on every miss — slower but functional.
static constexpr int BANK_CACHE_SLOTS = 1;

// =========================================================================
// Construction / Destruction
// =========================================================================

SumiBoyEmulator::SumiBoyEmulator(PluginRenderer& renderer, const char* romPath)
    : disp_(renderer) {
  utf8SafeCopy(romPath_, romPath, sizeof(romPath_));

  // Extract ROM name (filename without extension) for save paths.
  const char* slash = strrchr(romPath, '/');
  const char* base = slash ? slash + 1 : romPath;
  const char* dot = strrchr(base, '.');
  const size_t baseLen = dot ? (size_t)(dot - base) : strlen(base);
  char scratch[128];
  const size_t cap = baseLen < sizeof(scratch) - 1 ? baseLen : sizeof(scratch) - 1;
  memcpy(scratch, base, cap);
  scratch[cap] = '\0';
  utf8SafeCopy(romName_, scratch, sizeof(romName_));

  snprintf(sramPath_, sizeof(sramPath_), "/games/saves/%s.sav", romName_);
  snprintf(statePath_, sizeof(statePath_), "/games/saves/%s.state", romName_);
}

SumiBoyEmulator::~SumiBoyEmulator() { cleanup(); }

// =========================================================================
// Dither LUT
// =========================================================================

void SumiBoyEmulator::initDitherLUT() {
  for (int shade = 0; shade < 4; shade++) {
    for (int bc = 0; bc < 4; bc++) {
      uint8_t val = 0xFF;
      for (int b = 0; b < 8; b++) {
        if (shadeIntensity[shade] > bayer4x4[b & 3][bc]) {
          val &= ~(0x80 >> b);
        }
      }
      shade_byte_[shade][bc] = val;
    }
  }
}

// =========================================================================
// ROM load + GBEmulator instantiation
// =========================================================================

bool SumiBoyEmulator::loadROM() {
  FsFile f;
  if (!SdMan.openFileForRead("GB", romPath_, f)) {
    Serial.printf("[GB] Failed to open ROM: %s\n", romPath_);
    return false;
  }

  const uint32_t romSize = f.fileSize();
  if (romSize == 0 || romSize > 4u * 1024 * 1024) {
    Serial.printf("[GB] Implausible ROM size %u, rejecting\n", romSize);
    f.close();
    return false;
  }

  const int bankCount = static_cast<int>(romSize / 0x4000);
  Serial.printf("[GB] ROM %s: %u bytes (%d banks)\n", romPath_, romSize, bankCount);

  // Try to load the full ROM into RAM first (simple + fast). Actual
  // headroom after `new GBEmulator()` is ~55 KB with the 8 KB SRAM build
  // — verified on-device: `[GB] init(): core allocated, heap: free=30256`
  // was the failure mode with the old 32 KB SRAM struct. 32 KB here means
  // 32 KB ROM-only carts (Tetris, Dr. Mario, most 32-KB games) load whole;
  // anything bigger streams 16 KB bank 0 + 2×16 KB cache slots.
  constexpr uint32_t ROM_RAM_THRESHOLD = 32 * 1024;

  // Try RAM mode first for small ROMs. On a fragmented heap (common on
  // where the arena is static and heap sits around 30 KB largest
  // contig) the 32 KB contiguous alloc can fail even when total free
  // heap is plenty. In that case drop through to streaming mode: one
  // 16 KB bank0 + 16 KB cache slot = 32 KB split across two smaller
  // allocations. Same total heap use, much easier on fragmentation.
  bool useStreaming = (romSize > ROM_RAM_THRESHOLD);
  if (!useStreaming) {
    romData_ = (uint8_t*)malloc(romSize);
    if (!romData_) {
      Serial.printf("[GB] 32KB contig RAM alloc failed (heap=%lu/%lu), "
                    "falling back to streaming mode\n",
                    (unsigned long)ESP.getMaxAllocHeap(),
                    (unsigned long)ESP.getFreeHeap());
      useStreaming = true;
    }
  }

  if (!useStreaming) {
    const int r = f.read(romData_, romSize);
    f.close();
    if (r != static_cast<int>(romSize)) {
      Serial.printf("[GB] Short read %d/%u\n", r, romSize);
      free(romData_); romData_ = nullptr;
      return false;
    }
    emu_->romData_ = romData_;
    emu_->romSize_ = romSize;
    emu_->romBankCount_ = bankCount;
    emu_->romInRam_ = true;
    // romBank0_ points into the full ROM — keeps MBC code using a single
    // path instead of branching on romInRam_ for bank-0 reads.
    emu_->romBank0_ = romData_;
  } else {
    // Streaming: keep bank 0 resident, cache N banks on demand.
    emu_->romBank0_ = (uint8_t*)malloc(0x4000);
    if (!emu_->romBank0_) {
      Serial.println("[GB] malloc for bank0 failed");
      f.close();
      return false;
    }
    f.seek(0);
    const int r = f.read(emu_->romBank0_, 0x4000);
    if (r != 0x4000) {
      Serial.printf("[GB] Bank 0 short read %d\n", r);
      free(emu_->romBank0_); emu_->romBank0_ = nullptr;
      f.close();
      return false;
    }

    bankCacheBuf_ = (uint8_t*)malloc(BANK_CACHE_SLOTS * 0x4000);
    bankCacheMap_ = (int*)malloc(BANK_CACHE_SLOTS * sizeof(int));
    if (!bankCacheBuf_ || !bankCacheMap_) {
      Serial.println("[GB] Bank cache alloc failed");
      if (bankCacheBuf_) { free(bankCacheBuf_); bankCacheBuf_ = nullptr; }
      if (bankCacheMap_) { free(bankCacheMap_); bankCacheMap_ = nullptr; }
      free(emu_->romBank0_); emu_->romBank0_ = nullptr;
      f.close();
      return false;
    }
    for (int i = 0; i < BANK_CACHE_SLOTS; i++) bankCacheMap_[i] = -1;

    // The bank cache reads through `romFile_`. FsFile is non-copyable so
    // reopen the path into the emulator's member instead of assigning.
    f.close();
    if (!SdMan.openFileForRead("GB", romPath_, emu_->romFile_)) {
      Serial.println("[GB] Reopen for bank cache failed");
      free(emu_->romBank0_); emu_->romBank0_ = nullptr;
      free(bankCacheBuf_); bankCacheBuf_ = nullptr;
      free(bankCacheMap_); bankCacheMap_ = nullptr;
      return false;
    }
    emu_->romSize_ = romSize;
    emu_->romBankCount_ = bankCount;
    emu_->romInRam_ = false;
    emu_->bankCache_ = bankCacheBuf_;
    emu_->cacheMap_ = bankCacheMap_;
    emu_->cacheSlots_ = BANK_CACHE_SLOTS;
    emu_->cacheNextSlot_ = 0;

    Serial.printf("[GB] Streaming mode: %d slots × 16 KB = %d KB\n",
                  BANK_CACHE_SLOTS, BANK_CACHE_SLOTS * 16);
  }

  // Let the core inspect the cart type from bank 0 (CGB, MBC family, etc).
  // In streaming mode `emu_->romData_` is never set — pass nullptr
  // explicitly so the call site reads truthfully (audit #19). The core
  // already special-cases nullptr; the previous version just relied on
  // whatever default GBEmulator left in romData_.
  uint8_t* const romDataArg = useStreaming ? nullptr : emu_->romData_;
  if (!emu_->loadRom(romPath_, romDataArg, romSize)) {
    Serial.println("[GB] emu_->loadRom() rejected ROM");
    return false;
  }

  // CGB detection: header byte 0x143 == 0x80 or 0xC0 → CGB-capable.
  // Allocate VRAM1 + WRAM banks 2-7 (32 KB heap) before calling enableCgbMode.
  const uint8_t cgbFlag = emu_->romBank0_[0x143];
  if (cgbFlag == 0x80 || cgbFlag == 0xC0) {
    vram1_ = (uint8_t*)malloc(0x2000);       // 8 KB VRAM bank 1
    wramExtra_ = (uint8_t*)malloc(0x6000);   // 24 KB WRAM banks 2-7
    if (!vram1_ || !wramExtra_) {
      Serial.println("[GB] CGB buffer alloc failed — falling back to DMG mode");
      if (vram1_) { free(vram1_); vram1_ = nullptr; }
      if (wramExtra_) { free(wramExtra_); wramExtra_ = nullptr; }
    } else {
      emu_->enableCgbMode(vram1_, wramExtra_);
    }
  }

  // Pokemon Red detection works off the header in bank 0. Use the
  // checksum-based detector (resilient to retail vs romhack variants).
  // When matched, patchBank(bank0, 0) applies the simple single-byte
  // patches in bank 0 and the pokemonRedPatch_ flag makes freshly-cached
  // banks get patched on load too.
  if (emu_->romBank0_) {
    if (pokered::isPokemonInBank0(emu_->romBank0_, 0x4000)) {
      Serial.println("[GB] Pokemon Red/Blue detected — enabling e-ink patches");
      pokered::patchBank(emu_->romBank0_, 0);
      emu_->pokemonRedPatch_ = true;
    } else if (gbpatches::isPatchedGame(emu_->romBank0_, 0x4000)) {
      Serial.println("[GB] Per-game e-ink patch framework: match found");
      gbpatches::applyToBank(emu_->romBank0_, 0, emu_->romBank0_, 0x4000);
      emu_->gbPatchEnabled_ = true;
    }
  }

  return true;
}

// =========================================================================
// Plugin lifecycle
// =========================================================================

void SumiBoyEmulator::init(int screenW, int screenH) {
  Serial.println("[GB] init(): entry");
  screenW_ = screenW;
  screenH_ = screenH;
  initDitherLUT();

  Serial.printf("[GB] Heap before: free=%lu, largest=%lu\n",
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());

  // Opportunistic defrag before big allocations. Clears caches that are
  // fine to rebuild on demand but currently eating contiguous heap:
  //  - ThemeManager cache (theme colour + font-family strings)
  //  - GfxRenderer text-width cache (one hash per glyph measured)
  //  - all EpdFont GlyphCaches (16-slot recent-glyph lookup per font)
  // Typical recovery: 1-3 KB, but critically from the already-fragmented
  // region, so it tends to open up one more large contiguous slot.
  THEME_MANAGER.clearCache();
  // renderer_ isn't reachable here without plumbing — caller of init()
  // owns it. Caches inside GfxRenderer will clear naturally as text gets
  // re-rendered, and aren't as big as the above anyway.
  Serial.printf("[GB] After theme cache clear: free=%lu, largest=%lu\n",
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());

  // (v1 used to release MemoryArena here to free ~80 KB. v2 keeps the
  //  arena permanently in .bss, so the GB emulator works from the
  //  ~120 KB of heap that remains after boot. Allocation-order
  //  discipline below is still load-bearing.)

  // Allocation order is load-bearing on ESP32-C3. Any single 32 KB+
  // malloc MUST come from the largest-contig block, so the framebuffer
  // (the biggest live allocation) goes first while the heap is least
  // fragmented.
  //
  // Order:
  //  1. Tiny emu struct (< 1 KB): drops into a small fragment.
  //  2. loadROM() carves the ROM/bank-cache out of the 77 KB block
  //     FIRST, while it's still intact:
  //        - RAM mode : 32 KB  -> 45 KB contig left
  //        - streaming: 16 + 16 = 32 KB (1 cache slot) -> 45 KB left
  //  3. emu_->init() allocates 4 buffers (framebuf 23 + vram/wram/sram
  //     8 each = 47 KB). These can slot across the 45 KB contig
  //     remainder + the 26 KB of small fragments (TLSF best-fit).
  //
  // Reversed order tried emu_init first: the 4 mallocs fragmented the
  // 77 KB block down below 32 KB contig, and even 32 KB ROMs (Tetris)
  // stopped loading. ROM-first works consistently.
  Serial.println("[GB] init(): allocating emulator core...");
  emu_ = new (std::nothrow) GBEmulator();
  if (!emu_) {
    Serial.println("[GB] new GBEmulator failed — insufficient heap");
    return;
  }
  Serial.printf("[GB] init(): core allocated, heap: free=%lu\n",
                (unsigned long)ESP.getFreeHeap());

  Serial.println("[GB] init(): loadROM() start");
  if (!loadROM()) {
    Serial.println("[GB] ROM load failed");
    cleanup();
    return;
  }
  Serial.printf("[GB] init(): loadROM() done, heap: free=%lu\n",
                (unsigned long)ESP.getFreeHeap());

  if (!emu_->init()) {
    Serial.println("[GB] emu_->init() allocation failed");
    cleanup();
    return;
  }
  Serial.printf("[GB] init(): emu_->init() done, heap: free=%lu\n",
                (unsigned long)ESP.getFreeHeap());

  if (!loadState()) {
    loadSRAM();
    const int warmup = emu_->getWarmupFrames();
    Serial.printf("[GB] Running %d warmup frames (%d-chunk for WDT)...\n", warmup, 50);
    const uint32_t t0 = millis();
    // Run warmup in 50-frame chunks and feed the task watchdog between
    // them. One 600-frame runFrames() on cold cart can take ~10 s of
    // SM83 + PPU work, which is well under the 60 s WDT budget — but if
    // somebody hands us a homebrew ROM that takes minutes to reach the
    // title screen, the WDT would fire. Chunking keeps us safe for any
    // warmup length the core reports.
    constexpr int kChunk = 50;
    int remaining = warmup;
    while (remaining > 0) {
      const int step = remaining < kChunk ? remaining : kChunk;
      emu_->runFrames(step);
      remaining -= step;
      esp_task_wdt_reset();
    }
    Serial.printf("[GB] Warmup done in %lu ms\n", (unsigned long)(millis() - t0));
  } else {
    Serial.println("[GB] init(): save state restored — skipped warmup");
  }

  firstDraw_ = true;
  ready_ = true;   // full init success — cleanup() now allowed to save
  Serial.printf("[GB] Ready, heap: free=%lu\n", (unsigned long)ESP.getFreeHeap());
}

void SumiBoyEmulator::cleanup() {
  if (emu_) {
    // Write-back only if init() fully succeeded — otherwise the emulator
    // state is partially constructed and saveState() can dereference a
    // null framebuf/vram/etc. and crash with Load Access Fault.
    if (ready_) {
      saveState();
      saveSRAM();
    }

    // Close the ROM file handle (if streaming mode was active).
    if (emu_->romFile_) emu_->romFile_.close();

    delete emu_;
    emu_ = nullptr;
  }
  ready_ = false;

  if (romData_)      { free(romData_);      romData_ = nullptr; }
  if (vram1_)        { free(vram1_);        vram1_ = nullptr; }
  if (wramExtra_)    { free(wramExtra_);    wramExtra_ = nullptr; }
  if (bankCacheBuf_) { free(bankCacheBuf_); bankCacheBuf_ = nullptr; }
  if (bankCacheMap_) { free(bankCacheMap_); bankCacheMap_ = nullptr; }

  // (v1 used to reclaim MemoryArena here. v2 never released it.)

  Serial.printf("[GB] Cleanup done, heap: free=%lu\n",
                (unsigned long)ESP.getFreeHeap());
}

// =========================================================================
// Rendering (160x144 shades -> 480x432 dithered 1-bit, portrait rotated)
// =========================================================================

// Unpack one 2bpp packed shade from the GB framebuf. 4 pixels per byte,
// LSB is leftmost pixel (matches renderLine's pack order).
static inline uint8_t gbShadeAt(const uint8_t* fb, int gx, int gy) {
  const int byteIdx = gy * (GB_W / 4) + (gx >> 2);
  return (fb[byteIdx] >> ((gx & 3) << 1)) & 0x3;
}

void SumiBoyEmulator::renderToDisplay() {
  if (!emu_) return;
  const uint8_t* framebuffer = emu_->getFramebuffer();

  // 3× scale: 160×144 → 480×432. Portrait coordinate mapping (logical →
  // physical): physX = logicalY, physY = 479 - logicalX. Physical buffer
  // is 800 cols × 480 rows, 100 bytes/row, MSB-first. E-ink: 0=black,
  // 1=white.
  uint8_t* buf = disp_.gfx().getFrameBuffer();
  if (!buf) {
    // Fallback: route every pixel through drawPixel() (slow, but the
    // direct-buffer path is the hot one; this branch only fires when
    // the renderer's framebuffer isn't directly accessible).
    GfxRenderer& gfx = disp_.gfx();
    for (int gy = 0; gy < GB_H; gy++) {
      for (int gx = 0; gx < GB_W; gx++) {
        uint8_t shade = gbShadeAt(framebuffer, gx, gy);
        uint8_t intensity = shadeIntensity[shade];
        int baseX = gx * 3, baseY = gy * 3;
        for (int dy = 0; dy < 3; dy++) {
          int ey = baseY + dy;
          for (int dx = 0; dx < 3; dx++) {
            int ex = baseX + dx;
            gfx.drawPixel(ex, ey, intensity > bayer4x4[ey & 3][ex & 3]);
          }
        }
      }
    }
    return;
  }

  static constexpr int BUF_STRIDE = 100;  // 800/8

  for (int gy = 0; gy < GB_H; gy++) {
    for (int gx = 0; gx < GB_W; gx++) {
      uint8_t shade = gbShadeAt(framebuffer, gx, gy);
      uint8_t intensity = shadeIntensity[shade];

      int logBaseX = gx * 3;
      int logBaseY = gy * 3;

      for (int dy = 0; dy < 3; dy++) {
        int rotX = logBaseY + dy;
        int bayerRow = (logBaseY + dy) & 3;

        for (int dx = 0; dx < 3; dx++) {
          int rotY = 479 - (logBaseX + dx);
          int bayerCol = (logBaseX + dx) & 3;

          int byteIdx = rotY * BUF_STRIDE + (rotX >> 3);
          uint8_t bitMask = 0x80 >> (rotX & 7);

          if (intensity > bayer4x4[bayerRow][bayerCol]) {
            buf[byteIdx] &= ~bitMask;  // black
          } else {
            buf[byteIdx] |= bitMask;   // white
          }
        }
      }
    }
  }
}

void SumiBoyEmulator::drawControls() {
  // Programmatic controls overlay below the game area. Copied verbatim
  // from the pre-port SumiBoyEmulator — same button legend.
  GfxRenderer& gfx = disp_.gfx();
  int font = disp_.fontId();
  int topY = DISP_GAME_H;
  int W = screenW_;

  gfx.drawLine(0, topY,     W - 1, topY,     true);
  gfx.drawLine(0, topY + 2, W - 1, topY + 2, true);

  int dCX = 105, dCY = topY + 120;
  int arm = 40, thick = 3;

  gfx.fillRect(dCX - arm/2,           dCY - arm - arm/2, arm,           arm * 3,       true);
  gfx.fillRect(dCX - arm - arm/2,     dCY - arm/2,       arm * 3,       arm,           true);

  int inset = thick;
  gfx.fillRect(dCX - arm/2 + inset,       dCY - arm - arm/2 + inset, arm - inset*2,     arm * 3 - inset*2, false);
  gfx.fillRect(dCX - arm - arm/2 + inset, dCY - arm/2 + inset,       arm * 3 - inset*2, arm - inset*2,     false);

  gfx.drawLine(dCX - arm/2 + inset, dCY - arm/2 + inset,
               dCX + arm/2 - inset, dCY - arm/2 + inset, true);
  gfx.drawLine(dCX - arm/2 + inset, dCY + arm/2 - inset,
               dCX + arm/2 - inset, dCY + arm/2 - inset, true);
  gfx.drawLine(dCX - arm/2 + inset, dCY - arm/2 + inset,
               dCX - arm/2 + inset, dCY + arm/2 - inset, true);
  gfx.drawLine(dCX + arm/2 - inset, dCY - arm/2 + inset,
               dCX + arm/2 - inset, dCY + arm/2 - inset, true);

  for (int i = 0; i < 10; i++)
    gfx.drawLine(dCX - i, dCY - arm - 8 + i, dCX + i, dCY - arm - 8 + i, true);
  for (int i = 0; i < 10; i++)
    gfx.drawLine(dCX - i, dCY + arm + 8 - i, dCX + i, dCY + arm + 8 - i, true);
  for (int i = 0; i < 10; i++)
    gfx.drawLine(dCX - arm - 8 + i, dCY - i, dCX - arm - 8 + i, dCY + i, true);
  for (int i = 0; i < 10; i++)
    gfx.drawLine(dCX + arm + 8 - i, dCY - i, dCX + arm + 8 - i, dCY + i, true);

  int btnR = 28;
  int bCX = 310, bCY = dCY - 5;
  disp_.drawCircle(bCX, bCY, btnR,     true);
  disp_.drawCircle(bCX, bCY, btnR - 1, true);
  disp_.drawCircle(bCX, bCY, btnR - 2, true);
  gfx.drawText(font, bCX - 6, bCY - 12, "B", true);

  int aCX = 390, aCY = dCY - 45;
  disp_.fillCircle(aCX, aCY, btnR, true);
  gfx.drawText(font, aCX - 6, aCY - 12, "A", false);

  gfx.drawText(font, bCX - 20, bCY + btnR + 8, "Back", true);
  gfx.drawText(font, aCX - 10, aCY + btnR + 8, "OK",   true);

  int sy = dCY + 70, sx = 295, sw = 150, sh = 30;
  gfx.fillRect(sx + sh/2, sy, sw - sh, sh, true);
  disp_.fillCircle(sx + sh/2,      sy + sh/2, sh/2, true);
  disp_.fillCircle(sx + sw - sh/2, sy + sh/2, sh/2, true);
  gfx.drawText(font, sx + sw/2 - 28, sy + 4,      "START",     false);
  gfx.drawText(font, sx + sw/2 - 40, sy + sh + 8, "Power btn", true);

  int refY = topY + 260;
  gfx.drawLine(20, refY - 8, W - 20, refY - 8, true);
  int col1 = 25, col2 = 250;
  gfx.drawText(font, col1, refY,      "OK     = A button",    true);
  gfx.drawText(font, col1, refY + 26, "Back   = B button",    true);
  gfx.drawText(font, col1, refY + 52, "Power  = Start",       true);
  gfx.drawText(font, col2, refY,      "2x OK    = Select",    true);
  gfx.drawText(font, col2, refY + 26, "Hold Back  = Exit",    true);
  gfx.drawText(font, col2, refY + 52, "Hold Power = Sleep",   true);
}

// =========================================================================
// Plugin draw / input
// =========================================================================

void SumiBoyEmulator::draw() {
  if (!emu_) return;

  disp_.gfx().clearScreen(0xFF);
  renderToDisplay();
  drawControls();

  if (firstDraw_) {
    disp_.gfx().displayBuffer(EInkDisplay::FULL_REFRESH);
    firstDraw_ = false;
  } else {
    disp_.gfx().displayBuffer(EInkDisplay::FAST_REFRESH);
  }
}

bool SumiBoyEmulator::handleInput(PluginButton btn) {
  if (!emu_) return false;

  switch (btn) {
    case PluginButton::Right: pendingDpad_    &= ~0x01; return true;
    case PluginButton::Left:  pendingDpad_    &= ~0x02; return true;
    case PluginButton::Up:    pendingDpad_    &= ~0x04; return true;
    case PluginButton::Down:  pendingDpad_    &= ~0x08; return true;

    case PluginButton::Center: {
      // Double-tap Center → Select, single tap → A.
      unsigned long now = millis();
      if (now - lastCenterMs_ < DOUBLE_TAP_MS) {
        pendingButtons_ &= ~0x04;  // Select
        lastCenterMs_ = 0;
      } else {
        pendingButtons_ &= ~0x01;  // A
        lastCenterMs_ = now;
      }
      return true;
    }

    case PluginButton::Back:
      pendingButtons_ &= ~0x02;    // B
      return true;

    case PluginButton::Power:
      pendingButtons_ &= ~0x08;    // Start
      return true;

    default:
      return false;
  }
}

bool SumiBoyEmulator::handleRelease(PluginButton btn) {
  if (!emu_) return false;

  switch (btn) {
    case PluginButton::Right:  pendingDpad_    |= 0x01; return true;
    case PluginButton::Left:   pendingDpad_    |= 0x02; return true;
    case PluginButton::Up:     pendingDpad_    |= 0x04; return true;
    case PluginButton::Down:   pendingDpad_    |= 0x08; return true;
    case PluginButton::Center: pendingButtons_ |= 0x05; return true;  // A + Select
    case PluginButton::Back:   pendingButtons_ |= 0x02; return true;  // B
    case PluginButton::Power:  pendingButtons_ |= 0x08; return true;  // Start
    default: return false;
  }
}

// Translate the two per-device pending bitmaps into the Emulator's
// INPUT_* bitmask (Emulator::setInput contract).
static uint8_t packInput(uint8_t dpad, uint8_t buttons) {
  uint8_t out = 0;
  if (!(dpad    & 0x01)) out |= INPUT_RIGHT;
  if (!(dpad    & 0x02)) out |= INPUT_LEFT;
  if (!(dpad    & 0x04)) out |= INPUT_UP;
  if (!(dpad    & 0x08)) out |= INPUT_DOWN;
  if (!(buttons & 0x01)) out |= INPUT_A;
  if (!(buttons & 0x02)) out |= INPUT_B;
  if (!(buttons & 0x04)) out |= INPUT_SELECT;
  if (!(buttons & 0x08)) out |= INPUT_START;
  return out;
}

bool SumiBoyEmulator::update() {
  if (!emu_) return false;

  const int framesToRun = (consecutiveSkips_ >= 5) ? FRAMES_PER_REFRESH_IDLE : FRAMES_PER_REFRESH;

  emu_->setInput(packInput(pendingDpad_, pendingButtons_));
  emu_->runFrames(framesToRun);

  if (pendingDpad_ != 0x0F || pendingButtons_ != 0x0F) {
    buttonPressedSinceSave_ = true;
  }

  // Auto-save timing — matches the old cadence exactly.
  if (++saveCounter_ >= AUTOSAVE_INTERVAL) {
    if (buttonPressedSinceSave_) {
      saveState();
      buttonPressedSinceSave_ = false;
    }
    saveCounter_ = 0;
  }
  if (++sramCounter_ >= SRAM_SAVE_INTERVAL) {
    if (buttonPressedSinceSave_) {
      saveSRAM();
    }
    sramCounter_ = 0;
  }

  // Dirty-frame detection via FNV-1a. Skips e-ink refresh when the game
  // output hasn't changed (common in dialog boxes, pause menus).
  // framebuf is 2bpp packed (GB_W*GB_H/4 bytes); hash 4 bytes at a time.
  uint32_t hash = 2166136261u;
  const uint8_t* fb = emu_->getFramebuffer();
  constexpr int FB_BYTES = (GB_W * GB_H) / 4;
  for (int i = 0; i < FB_BYTES; i += 4) {
    hash ^= *(uint32_t*)(fb + i);
    hash *= 16777619u;
  }

  if (hash == lastFrameHash_) {
    consecutiveSkips_++;
    return false;
  }

  consecutiveSkips_ = 0;
  if (disp_.gfx().isRefreshing()) {
    // Don't stack refreshes — drop this frame.
    return false;
  }

  lastFrameHash_ = hash;
  if (firstDraw_) {
    disp_.gfx().clearScreen(0xFF);
    renderToDisplay();
    drawControls();
    disp_.gfx().displayBuffer(EInkDisplay::FULL_REFRESH);
    firstDraw_ = false;
  } else {
    renderToDisplay();
    disp_.gfx().displayBuffer(EInkDisplay::FAST_REFRESH);
  }
  return true;
}

// =========================================================================
// Save / Load — delegates to GBEmulator::saveState / loadState / saveSRAM /
// loadSRAM. Those methods take an SdFat& which we source from SdMan.raw().
// =========================================================================

bool SumiBoyEmulator::saveState() {
  if (!emu_) return false;
  SdMan.ensureDirectoryExists("/games");
  SdMan.ensureDirectoryExists("/games/saves");
  const bool ok = emu_->saveState(SdMan.raw(), statePath_);
  if (ok) Serial.printf("[GB] State saved: %s\n", statePath_);
  return ok;
}

bool SumiBoyEmulator::loadState() {
  if (!emu_) return false;
  const bool ok = emu_->loadState(SdMan.raw(), statePath_);
  if (ok) Serial.printf("[GB] State loaded: %s\n", statePath_);
  return ok;
}

bool SumiBoyEmulator::saveSRAM() {
  if (!emu_) return false;
  SdMan.ensureDirectoryExists("/games");
  SdMan.ensureDirectoryExists("/games/saves");
  const bool ok = emu_->saveSRAM(SdMan.raw(), sramPath_);
  if (ok) Serial.printf("[GB] SRAM saved: %s\n", sramPath_);
  return ok;
}

bool SumiBoyEmulator::loadSRAM() {
  if (!emu_) return false;
  const bool ok = emu_->loadSRAM(SdMan.raw(), sramPath_);
  if (ok) Serial.printf("[GB] SRAM loaded: %s\n", sramPath_);
  return ok;
}

}  // namespace sumi

#endif  // FEATURE_PLUGINS && FEATURE_GAMES

#pragma once

#include <EInkDisplay.h>
#include <EpdFontFamily.h>
#include <ThaiCluster.h>

#include <array>
#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

#include "Bitmap.h"

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

// Forward declaration for external CJK font support
class ExternalFont;
// Forward declaration for streaming font support
class StreamingEpdFont;

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

  // Logical screen orientation from the perspective of callers
  enum Orientation {
    Portrait,                  // 480x800 logical coordinates (current default)
    LandscapeClockwise,        // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    PortraitInverted,          // 480x800 logical coordinates, inverted
    LandscapeCounterClockwise  // 800x480 logical coordinates, native panel orientation
  };

 private:
  // 8KB chunks to allow for non-contiguous memory allocation. Sized for
  // MAX_BUFFER_SIZE (52272 bytes for X3) so one set of chunks works for both
  // panels. 7 chunks × 8000 = 56000 bytes; X3 uses 52272, X4 uses 48000, the
  // remainder is wasted but the chunk allocation pattern stays simple.
  static constexpr size_t BW_BUFFER_CHUNK_SIZE = 8000;
  static constexpr size_t BW_BUFFER_NUM_CHUNKS = 7;
  static_assert(BW_BUFFER_CHUNK_SIZE * BW_BUFFER_NUM_CHUNKS >= EInkDisplay::MAX_BUFFER_SIZE,
                "BW buffer chunks too small for MAX_BUFFER_SIZE");

  EInkDisplay& einkDisplay;
  RenderMode renderMode;
  Orientation orientation;
  uint8_t* frameBuffer = nullptr;
  uint8_t* bwBufferChunks[BW_BUFFER_NUM_CHUNKS] = {nullptr};
  std::map<int, EpdFontFamily> fontMap;
  // Streaming fonts: [fontId] -> array of [REGULAR, BOLD, ITALIC] (BOLD_ITALIC uses BOLD)
  // Mutable: getStreamingFont may trigger lazy loading of bold/italic variants via resolver
  mutable std::map<int, std::array<StreamingEpdFont*, 3>> _streamingFonts;
  ExternalFont* _externalFont = nullptr;

  // Lazy font style resolver: called when a streaming font variant (bold/italic) is
  // requested but not yet loaded. The callback should load the variant and call
  // setStreamingFont() + updateFontFamily() to register it.
  using FontStyleResolver = void (*)(void* ctx, int fontId, int styleIdx);
  mutable FontStyleResolver _fontStyleResolver = nullptr;
  mutable void* _fontStyleResolverCtx = nullptr;

  // Pre-allocated row buffers for bitmap rendering (reduces heap fragmentation).
  // Sized for the LARGER of X4 (800) and X3 (792) panel widths; X3 uses the
  // buffer but leaves 8 pixels unused per row (negligible).
  // outputRow = 800/4 = 200 bytes, rowBytes = 800*4 = 3200 bytes (32-bit max).
  static constexpr size_t BITMAP_MAX_WIDTH =
      (EInkDisplay::DISPLAY_WIDTH > EInkDisplay::X3_DISPLAY_WIDTH) ? EInkDisplay::DISPLAY_WIDTH
                                                                  : EInkDisplay::X3_DISPLAY_WIDTH;
  static constexpr size_t BITMAP_OUTPUT_ROW_SIZE = (BITMAP_MAX_WIDTH + 3) / 4;
  static constexpr size_t BITMAP_ROW_BYTES_SIZE = BITMAP_MAX_WIDTH * 4;
  uint8_t* bitmapOutputRow_ = nullptr;
  uint8_t* bitmapRowBytes_ = nullptr;
  bool bitmapRowsOwnMemory_ = false;
  void allocateBitmapRowBuffers();
  void freeBitmapRowBuffers();

  // Periodic refresh counter — auto-promotes FAST_REFRESH to HALF_REFRESH
  // every N fast refreshes to clear accumulated e-ink ghosting.
  // Mutable: display policy state, not renderer content state.
  mutable int periodicRefreshInterval_ = 0;  // 0 = disabled (Reader manages its own)
  mutable int fastRefreshCount_ = 0;

  // Sunlight fading fix — power off display after each refresh
  bool fadingFix_ = false;

  // Text darkness: controls AA intensity for 2-bit font glyphs
  // 0=Normal (true 4-level), 1=Dark, 2=Extra Dark, 3=Maximum (1-bit, no AA)
  uint8_t textDarkness_ = 0;

  // Word width cache for performance optimization during EPUB section creation.
  // Key: FNV-1a hash of (fontId, text, style). Value: measured width in pixels.
  // Limited to MAX_WIDTH_CACHE_SIZE entries to prevent heap fragmentation.
  //
  // Value type was int16_t pre-Batch-7. A long Arabic ligature or a CJK
  // string with extra-wide AA spacing can measure past 32767 pixels —
  // the int16_t cast wrapped negative and the cache returned a phantom
  // negative width that subsequent layout used as an offset, producing
  // overlapping or off-screen text. int32_t fits any plausible
  // pixel-width on this hardware (panel max ~800 px, cache holds whole
  // rendered runs that can be many panels wide). 4-byte values vs 2-byte
  // cost ~512 B at MAX_WIDTH_CACHE_SIZE=256; that's a worthy trade.
  // Audit #34.
  static constexpr size_t MAX_WIDTH_CACHE_SIZE = 256;
  mutable std::unordered_map<uint64_t, int32_t> wordWidthCache;
  // FIFO insertion order, used for eviction. When the map hits
  // MAX_WIDTH_CACHE_SIZE the oldest entry (front of deque) is dropped
  // — instead of the previous `cache.clear()` which trashed all 256
  // entries on every overflow. EPUB layout regularly measures hundreds
  // of unique words per page; the bulk-clear meant every page paid the
  // full measurement cost. FIFO eviction keeps the most recent N
  // entries warm and the typical hit rate climbs from ~0% to ~30-50%.
  // Audit #42.
  //
  // We use FIFO rather than true LRU: a hit doesn't promote the entry
  // to the back. The access pattern (a single layout pass walks ~50
  // words once, then moves on) makes LRU promotion ~free in benefit
  // for a measurable cost (deque-find on every hit). Pure FIFO is
  // both cheaper and observationally similar.
  mutable std::deque<uint64_t> wordWidthOrder;

  uint64_t makeWidthCacheKey(int fontId, const char* text, EpdFontFamily::Style style) const {
    // FNV-1a hash
    uint64_t hash = 14695981039346656037ULL;
    hash ^= static_cast<uint64_t>(fontId);
    hash *= 1099511628211ULL;
    hash ^= static_cast<uint64_t>(style);
    hash *= 1099511628211ULL;
    if (text) {
      while (*text) {
        hash ^= static_cast<uint8_t>(*text++);
        hash *= 1099511628211ULL;
      }
    }
    return hash;
  }

  void renderChar(const EpdFontFamily& fontFamily, uint32_t cp, int* x, const int* y, bool pixelState,
                  EpdFontFamily::Style style, int fontId) const;
  void renderThaiCluster(const EpdFontFamily& fontFamily, const ThaiShaper::ThaiCluster& cluster, int* x, int y,
                         bool pixelState, EpdFontFamily::Style style, int fontId) const;
  void renderExternalGlyph(uint32_t cp, int* x, int y, bool pixelState) const;
  int getExternalGlyphWidth(uint32_t cp) const;
  void freeBwBufferChunks();

  // Concurrency check helper. Logs (rate-limited) when a renderer method
  // is entered from a task that is not the cacheTask while the cacheTask
  // is registered as active. See `s_cacheTaskHandle_` above. No-op on
  // host builds.
  static void warnIfNonOwner(const char* methodName);

 public:
  explicit GfxRenderer(EInkDisplay& einkDisplay)
      : einkDisplay(einkDisplay), renderMode(BW), orientation(Portrait) {
    allocateBitmapRowBuffers();
  }
  ~GfxRenderer() { freeBitmapRowBuffers(); }

  static constexpr int VIEWABLE_MARGIN_TOP = 9;
  static constexpr int VIEWABLE_MARGIN_RIGHT = 3;
  static constexpr int VIEWABLE_MARGIN_BOTTOM = 3;
  static constexpr int VIEWABLE_MARGIN_LEFT = 3;

  // ── Single-task ownership tracking (CONCURRENCY.md C1) ────────────
  //
  // The renderer is owned by exactly one task at a time. By convention
  // (verified at the audit-plan stage and now enforced here), main task
  // calls `stopBackgroundCaching()` before invoking any renderer method
  // that mutates shared state (wordWidthCache, framebuffer, etc.). The
  // cacheTask is allowed to invoke renderer methods while it's running
  // — it owns the renderer during that window.
  //
  // ReaderState publishes the cacheTask's TaskHandle into
  // `s_cacheTaskHandle_` between `startBackgroundCaching()` and the
  // matching `stopBackgroundCaching()`. The check at the entry of every
  // mutating public method warns when a NON-cacheTask caller invokes
  // a renderer method while the field is non-null — that's main task
  // forgetting to stop the bg first, the bug pattern from audits #6/#7.
  //
  // Set to `nullptr` when no bg task is active; reads/writes are
  // single-aligned-pointer and safe across the single-core ESP32-C3
  // pipeline (CONCURRENCY.md C6). On host test builds the field is
  // unused (no FreeRTOS tasks).
#ifdef ARDUINO
  static TaskHandle_t s_cacheTaskHandle_;
#endif

  // Setup
  void begin();
  void insertFont(int fontId, EpdFontFamily font);
  void removeFont(int fontId);
  void clearWidthCache() {
    std::unordered_map<uint64_t, int32_t>().swap(wordWidthCache);
    std::deque<uint64_t>().swap(wordWidthOrder);
  }
  void setExternalFont(ExternalFont* font) { _externalFont = font; }
  ExternalFont* getExternalFont() const { return _externalFont; }

  void setFontStyleResolver(FontStyleResolver resolver, void* ctx) {
    _fontStyleResolver = resolver;
    _fontStyleResolverCtx = ctx;
  }
  void updateFontFamily(int fontId, EpdFontFamily::Style style, const EpdFont* font) {
    auto it = fontMap.find(fontId);
    if (it != fontMap.end()) {
      it->second.setFont(style, font);
    }
  }

  void setStreamingFont(int fontId, EpdFontFamily::Style style, StreamingEpdFont* font) {
    int idx = (style == EpdFontFamily::BOLD_ITALIC) ? EpdFontFamily::BOLD : style;
    // std::map::operator[] value-initializes new entries, so array elements are nullptr by default
    _streamingFonts[fontId][idx] = font;
  }
  void setStreamingFont(int fontId, StreamingEpdFont* font) { _streamingFonts[fontId][EpdFontFamily::REGULAR] = font; }
  void removeStreamingFont(int fontId) { _streamingFonts.erase(fontId); }
  // NOTE: May trigger lazy font loading (SD I/O + allocation) on first access to bold/italic.
  // Thread safety: caller must have exclusive renderer access (ownership model).
  StreamingEpdFont* getStreamingFont(int fontId, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    auto it = _streamingFonts.find(fontId);
    if (it == _streamingFonts.end()) return nullptr;
    int idx = (style == EpdFontFamily::BOLD_ITALIC) ? EpdFontFamily::BOLD : style;
    StreamingEpdFont* sf = it->second[idx];
    if (!sf && idx != EpdFontFamily::REGULAR && _fontStyleResolver) {
      _fontStyleResolver(_fontStyleResolverCtx, fontId, idx);
      sf = it->second[idx];
    }
    return sf ? sf : it->second[EpdFontFamily::REGULAR];
  }

  // Orientation control (affects logical width/height and coordinate transforms)
  void setOrientation(const Orientation o) { orientation = o; }
  Orientation getOrientation() const { return orientation; }

  // Screen ops
  int getScreenWidth() const;
  int getScreenHeight() const;
  void displayBuffer(EInkDisplay::RefreshMode refreshMode = EInkDisplay::FAST_REFRESH,
                     bool turnOffScreen = false) const;
  // EXPERIMENTAL: Windowed update - display only a rectangular region
  void displayWindow(int x, int y, int width, int height, bool turnOffScreen = false) const;
  void invertScreen() const;
  void clearScreen(uint8_t color = 0xFF) const;

  // Check if an async display refresh is in progress (non-blocking).
  // Emulators/animations can skip rendering when the display is busy.
  bool isRefreshing() const { return einkDisplay.isRefreshing(); }

  // Periodic refresh: auto-promote FAST_REFRESH to HALF_REFRESH every N fast
  // refreshes to clear accumulated e-ink ghosting. Set to 0 to disable.
  void setPeriodicRefreshInterval(int interval) { periodicRefreshInterval_ = interval; }
  void resetPeriodicRefreshCounter() { fastRefreshCount_ = 0; }

  // Sunlight fading fix: power off display after each refresh to prevent
  // e-ink fading in direct sunlight. Applied automatically to all displayBuffer
  // calls unless explicitly overridden by passing turnOffScreen=true/false.
  void setFadingFix(bool enabled) { fadingFix_ = enabled; }
  void setTextDarkness(uint8_t level) { textDarkness_ = level; }
  uint8_t getTextDarkness() const { return textDarkness_; }
  void clearArea(int x, int y, int width, int height, uint8_t color = 0xFF) const;

  // Drawing
  void drawPixel(int x, int y, bool state = true) const;
  void drawLine(int x1, int y1, int x2, int y2, bool state = true) const;
  void drawRect(int x, int y, int width, int height, bool state = true) const;
  void fillRect(int x, int y, int width, int height, bool state = true) const;
  void drawImage(const uint8_t bitmap[], int x, int y, int width, int height) const;
  void drawBitmap(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight) const;

  // Text
  int getTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawCenteredText(int fontId, int y, const char* text, bool black = true,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawText(int fontId, int x, int y, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getSpaceWidth(int fontId) const;
  int getFontAscenderSize(int fontId) const;
  int getLineHeight(int fontId) const;
  std::string truncatedText(const int fontId, const char* text, const int maxWidth,
                            const EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  // Breaks a single word into chunks that fit within maxWidth, adding "-" where needed
  std::vector<std::string> breakWordWithHyphenation(int fontId, const char* word, int maxWidth,
                                                    EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  std::vector<std::string> wrapTextWithHyphenation(int fontId, const char* text, int maxWidth, int maxLines,
                                                   EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  bool fontSupportsGrayscale(int fontId) const;

  // Thai text rendering
  int getThaiTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawThaiText(int fontId, int x, int y, const char* text, bool black = true,
                    EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

  // Arabic text rendering
  int getArabicTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawArabicText(int fontId, int x, int y, const char* text, bool black = true,
                      EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

  // UI Components
  void drawButtonHints(int fontId, const char* btn1, const char* btn2, const char* btn3, const char* btn4,
                       bool black = true) const;

  // Grayscale functions
  void setRenderMode(const RenderMode mode) { this->renderMode = mode; }
  RenderMode getRenderMode() const { return renderMode; }
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  void displayGrayBuffer(bool turnOffScreen = false) const;
  bool storeBwBuffer();  // Returns true if buffer was stored successfully
  void restoreBwBuffer();
  void cleanupGrayscaleWithFrameBuffer() const;

  // Low level functions
  uint8_t* getFrameBuffer() const;
  size_t getBufferSize() const;
  void grayscaleRevert() const;
  void getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const;
};

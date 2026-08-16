#pragma once

#include <BackgroundTask.h>
#include <BookOverrides.h>
#include <Bookmarks.h>

#include <cstdint>
#include <memory>

#include "../content/ReaderNavigation.h"
#include "../core/Types.h"
#include "../rendering/XtcPageRenderer.h"
#include "../ui/views/DictionaryViews.h"
#include "../ui/views/HomeView.h"
#include "../ui/views/ReaderViews.h"
#include "../ui/views/SettingsViews.h"
#include "State.h"

class ContentParser;
class GfxRenderer;
class PageCache;
class Page;
struct RenderConfig;

namespace sumi {

// Forward declarations
class Core;
struct Event;

// ReaderState - unified reader for all content types
// Uses ContentHandle to abstract Epub/Xtc/Txt/Markdown differences
// Uses PageCache for all formats with partial caching support
// Delegates to: XtcPageRenderer (binary rendering), ProgressManager (persistence),
//               ReaderNavigation (page traversal)
class ReaderState : public State {
 public:
  explicit ReaderState(GfxRenderer& renderer);
  ~ReaderState() override;

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::Reader; }

  // Set content path before entering state
  void setContentPath(const char* path);

  // Reading position
  uint32_t currentPage() const { return currentPage_; }
  void setCurrentPage(uint32_t page) { currentPage_ = page; }

 private:
  GfxRenderer& renderer_;
  XtcPageRenderer xtcRenderer_;
  char contentPath_[256];
  uint32_t currentPage_;
  bool needsRender_;
  bool contentLoaded_;
  bool loadFailed_ = false;  // Track if content loading failed (for error state transition)

  // Reading position (maps to ReaderNavigation::Position)
  int currentSpineIndex_;
  int currentSectionPage_;

  // Last successfully rendered position (for accurate progress saving)
  int lastRenderedSpineIndex_ = 0;
  int lastRenderedSectionPage_ = 0;

  // Whether book has a valid cover image
  bool hasCover_ = false;

  // Landscape scroll mode (for comics/scanned docs)
  bool landscapeScroll_ = false;  // Active landscape scroll mode
  int scrollY_ = 0;               // Current vertical scroll offset within page
  int pageContentHeight_ = 0;     // Total height of current page content

  // First text content spine index (from EPUB guide, 0 if not specified)
  int textStartIndex_ = 0;

  // Unified page cache for all content types
  // Ownership model: main thread owns pageCache_/parser_ when !cacheTask_.isRunning()
  //                  background task owns pageCache_/parser_ when cacheTask_.isRunning()
  // Navigation ALWAYS stops task first, then accesses cache/parser
  std::unique_ptr<PageCache> pageCache_;

  // Persistent parser for incremental (hot) extends — kept alive between extend calls
  // so the parser can resume from where it left off instead of re-parsing from byte 0
  std::unique_ptr<ContentParser> parser_;
  int parserSpineIndex_ = -1;
  uint8_t pagesUntilFullRefresh_;
  bool pageHasLargeImage_ = false;  // Set during render for refresh mode selection
  uint32_t autoTurnLastMs_ = 0;    // Auto page turn timer (0 = reset on next check)

  // Background caching (uses BackgroundTask for proper lifecycle management)
  BackgroundTask cacheTask_;
  Core* coreForCacheTask_ = nullptr;
  bool thumbnailDone_ = false;
  void startBackgroundCaching(Core& core);
  void stopBackgroundCaching();

  // Navigation helpers (delegates to ReaderNavigation)
  void navigateNext(Core& core);
  void navigatePrev(Core& core);
  void applyNavResult(const ReaderNavigation::NavResult& result, Core& core);

  // Rendering
  void renderCurrentPage(Core& core);
  void renderCachedPage(Core& core);
  void renderXtcPage(Core& core);
  void renderComicPage(Core& core);
  bool renderCoverPage(Core& core);

  // Helpers
  void renderPageContents(Core& core, Page& page, int marginTop, int marginRight, int marginBottom, int marginLeft);
  void renderStatusBar(Core& core, int marginRight, int marginBottom, int marginLeft);

  // Cache management
  bool ensurePageCached(Core& core, uint16_t pageNum);
  void loadCacheFromDisk(Core& core);
  void createOrExtendCache(Core& core);

  void createOrExtendCacheImpl(ContentParser& parser, const std::string& cachePath, const RenderConfig& config);
  void backgroundCacheImpl(ContentParser& parser, const std::string& cachePath, const RenderConfig& config);

  // Display helpers
  void displayWithRefresh(Core& core);

  // Viewport calculation
  struct Viewport {
    int marginTop;
    int marginRight;
    int marginBottom;
    int marginLeft;
    int width;
    int height;
  };
  Viewport getReaderViewport() const;

  // Get first content spine index (skips cover document when appropriate)
  static int calcFirstContentSpine(bool hasCover, int textStartIndex, size_t spineCount);

  // Anchor-to-page persistence for intra-spine TOC navigation
  static void saveAnchorMap(const ContentParser& parser, const std::string& cachePath);
  static int loadAnchorPage(const std::string& cachePath, const std::string& anchor);

  // Per-book reader setting overrides
  sumi::BookOverrides bookOverrides_;

  // Snapshot of global defaults taken before per-book overrides are applied.
  // Used by applyInReaderSettings to determine which fields are overridden.
  uint8_t globalFontSize_ = 0;
  uint8_t globalLineSpacing_ = 0;
  uint8_t globalHyphenation_ = 0;
  uint8_t globalShowImages_ = 0;
  uint8_t globalTextDarkness_ = 0;

  // Source state (where reader was opened from)
  StateId sourceState_ = StateId::Home;

  // TOC overlay mode
  bool tocMode_ = false;
  ui::ChapterListView tocView_;

  void enterTocMode(Core& core);
  void exitTocMode();
  void handleTocInput(Core& core, const Event& e);
  void renderTocOverlay(Core& core);
  int tocVisibleCount() const;
  void populateTocView(Core& core);
  int findCurrentTocEntry(Core& core);
  void jumpToTocEntry(Core& core, int tocIndex);

  // In-reader settings overlay (long-press Select)
  bool settingsMode_ = false;
  bool centerLongPressFired_ = false;  // Suppress short-press TOC on release after long-press settings
  uint32_t enterTime_ = 0;  // millis() at enter() — suppress stale long-press from previous state
  ui::InReaderSettingsView settingsView_;

  void enterSettingsMode(Core& core);
  void exitSettingsMode(Core& core);
  void handleSettingsInput(Core& core, const Event& e);
  void renderSettingsOverlay(Core& core);
  void loadInReaderSettings(Core& core);
  void applyInReaderSettings(Core& core);
#if FEATURE_BLUETOOTH
  void handleBleAction(Core& core);
#endif

  // Exit reader back to UI (Home or FileList)
  void exitToUI(Core& core);
  StateId exitTarget_ = StateId::Reader;  // Set by exitToUI to trigger transition

  // ── Dictionary lookup history viewer ────────────────────────
  ui::DictHistoryView historyView_;
  bool historyMode_ = false;

  void handleHistoryInput(Core& core, const Event& e);

  // ── Dictionary overlay modes ────────────────────────────────
  // The in-reader dictionary is a three-stage overlay: word-select
  // (pick a word on the current page), definition (show the entry),
  // and suggestions ("did you mean?" fuzzy list). Only one is active
  // at a time. Launched from Settings → "Look up Word".
  enum class DictStage : uint8_t { None, WordSelect, Definition, Suggestions };
  DictStage dictStage_ = DictStage::None;
  ui::DictWordSelectView dictWordSelectView_;
  ui::DictDefinitionView dictDefinitionView_;
  ui::DictSuggestionsView dictSuggestionsView_;

  // Page kept alive for the duration of the word-select overlay. We don't
  // want to re-parse the cache underneath us while the user is navigating
  // word cells, so we snapshot the rendered page here on entry.
  std::unique_ptr<Page> dictSelectedPage_;

  void enterDictWordSelect(Core& core);
  void exitDictOverlay(Core& core);
  void handleDictInput(Core& core, const Event& e);
  void renderDictOverlay(Core& core);

  // Triggered from the word-select view on Confirm. Runs Dictionary::lookup,
  // with a stem-variant fallback and a findSimilar fallback; transitions
  // into Definition or Suggestions, or shows "Not found" and stays in
  // WordSelect. The word argument is already cleaned.
  void performDictLookup(Core& core, const std::string& cleaned);

  // ── Bookmark list overlay ──────────────────────────────────
  bool bookmarkListMode_ = false;
  ui::BookmarkListView bookmarkView_;

  void handleBookmarkListInput(Core& core, const Event& e);
  void enterBookmarkList(Core& core);

  // ── Global bookmark index overlay ─────────────────────────
  bool globalBookmarkMode_ = false;
  ui::GlobalBookmarkListView globalBookmarkView_;

  void handleGlobalBookmarkInput(Core& core, const Event& e);
  void enterGlobalBookmarkList(Core& core);
};

}  // namespace sumi

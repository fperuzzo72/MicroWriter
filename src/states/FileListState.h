#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../config.h"
#include "../content/EpubProvider.h"
#include "../ui/views/SettingsViews.h"
#include "State.h"

class GfxRenderer;

namespace sumi {

#if FEATURE_PLUGINS
class PluginHostState;
#endif

// FileListState - browse and select files
// Uses dynamic vector for unlimited file support with pagination
class FileListState : public State {
  enum class Screen : uint8_t {
    Browse,
    FileAction,       // 3-option Index/Delete/Cancel popup on Right-press
    ConfirmDelete,
    ConvertInfo,      // Friendly "needs conversion" message
    IndexConfirm,     // Tradeoff explainer with Start/Cancel
    Indexing,         // Live progress (chapter N of M)
    IndexDone,        // "Indexed N chapters" — any key dismisses
  };

 public:
  explicit FileListState(GfxRenderer& renderer);
  ~FileListState() override;

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::FileList; }

  // Get selected file path after state exits
  const char* selectedPath() const { return selectedPath_; }

  // Set initial directory before entering
  void setDirectory(const char* dir);

#if FEATURE_PLUGINS
  // Set plugin host for launching apps (e.g., Flashcards) from file browser
  void setHostState(PluginHostState* host) { pluginHost_ = host; }
#endif

 private:
  GfxRenderer& renderer_;
  char currentDir_[256];
  char selectedPath_[256];

  // File entries - dynamic vector for unlimited files
  struct FileEntry {
    std::string name;
    bool isDir;
    int16_t progressPercent;  // -1 = never opened, 0-100 = reading progress.
                              // Widened from int8_t in Batch 7 alongside
                              // LibraryIndex::Entry::progressPercent so a
                              // stale entry where currentPage > totalPages
                              // can't manufacture a negative phantom (audit
                              // #35). LibraryIndex clamps to [0, 100], so
                              // valid values still fit; the wider type
                              // documents the sentinel.
    bool unsupported;        // true = known format but needs conversion via sumi.page
    uint8_t contentHint;     // ContentHint from LibraryIndex (0 = generic/unknown)
  };
  std::vector<FileEntry> files_;

  size_t selectedIndex_;
  bool needsRender_;
  bool hasSelection_;
  bool goHome_;       // Return to Home state
  bool firstRender_;  // Use HALF_REFRESH on first render to clear ghosting
  Screen currentScreen_;
  ui::ConfirmDialogView confirmView_;
  ui::FileActionMenuView actionView_;

  // On-device indexer state. Valid only while currentScreen_ is in
  // {IndexConfirm, Indexing, IndexDone}. The indexer pre-builds the
  // page cache for every spine of the selected EPUB so subsequent
  // reads (especially with BLE on, where heap is too tight for the
  // cold-extend path) load instantly.
  std::unique_ptr<EpubProvider> indexProvider_;
  char indexBookPath_[256] = {};
  char indexSavedFont_[32] = {};
  uint16_t indexCurrentSpine_ = 0;
  uint16_t indexTotalSpines_ = 0;
  uint32_t indexStartMs_ = 0;
  bool indexFontWasLoaded_ = false;
  bool indexBleWasInitd_ = false;
  uint8_t indexResult_ = 0;  // 0 = in-progress, 1 = success, 2 = cancelled, 3 = error

#if FEATURE_PLUGINS
  PluginHostState* pluginHost_ = nullptr;
  bool launchPlugin_ = false;
#endif

  bool pendingOpen_ = false;  // Set by openSelected to trigger Reader transition

  void loadFiles(Core& core);
  void promptFileAction(Core& core);
  void promptDelete(Core& core);
  void navigateUp(Core& core);
  void navigateDown(Core& core);
  void openSelected(Core& core);
  void goBack(Core& core);
  void showConvertMessage(Core& core, const char* filename);

  // On-device indexer entry points.
  void enterIndexConfirm(Core& core, const char* filename);
  void renderIndexConfirm(Core& core);
  bool startIndexing(Core& core);              // false on early-fail (not an EPUB / open failed)
  void indexOneSpineStep(Core& core);          // builds cache for current spine, advances counter
  void renderIndexProgress(Core& core);
  void renderIndexDone(Core& core);
  void finishIndexing(Core& core);             // releases provider, restores font/BLE state

  // Pagination helpers
  int getPageItems() const;
  int getTotalPages() const;
  int getCurrentPage() const;
  int getPageStartIndex() const;

  bool isHidden(const char* name) const;
  bool isSupportedFile(const char* name) const;
  bool isImageFile(const char* name) const;
  bool isFlashcardFile(const char* name) const;
  bool isConvertibleFile(const char* name) const;
  bool isAtRoot() const { return strcmp(currentDir_, "/") == 0; }
};

}  // namespace sumi

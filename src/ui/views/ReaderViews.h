#pragma once

#include <GfxRenderer.h>
#include <Theme.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../Elements.h"

namespace ui {

// ============================================================================
// ReaderStatusView - Status bar for reader screens
// ============================================================================

struct ReaderStatusView {
  int16_t currentPage = 1;
  int16_t totalPages = 1;
  // Widened from int8_t in Batch 7. The setPage math (`current * 100 /
  // total`) can momentarily exceed 100 right after a chapter boundary
  // crossing where currentPage briefly leads totalPages from a stale
  // cache view; the int8_t cast wrapped negative and the status bar
  // flickered "-56%". Wider type + clamp keeps the displayed percent
  // sane during the brief window. Audit #35.
  int16_t progressPercent = 0;
  bool showProgress = true;
  bool needsRender = true;

  void setPage(int current, int total) {
    currentPage = static_cast<int16_t>(current);
    totalPages = static_cast<int16_t>(total);
    if (totalPages > 0) {
      const int pct = (current * 100) / total;
      progressPercent = static_cast<int16_t>(pct < 0 ? 0 : (pct > 100 ? 100 : pct));
    }
    needsRender = true;
  }

  void setShowProgress(bool show) {
    showProgress = show;
    needsRender = true;
  }
};

void renderStatusBar(const GfxRenderer& r, const Theme& t, const ReaderStatusView& v);

// ============================================================================
// CoverPageView - Book cover display (for EPUB cover pages)
// ============================================================================

struct CoverPageView {
  static constexpr int MAX_TITLE_LEN = 128;
  static constexpr int MAX_AUTHOR_LEN = 64;

  // External cover image pointer (not owned)
  const uint8_t* coverData = nullptr;
  int16_t coverWidth = 0;
  int16_t coverHeight = 0;

  char title[MAX_TITLE_LEN] = {0};
  char author[MAX_AUTHOR_LEN] = {0};
  bool needsRender = true;

  void setCover(const uint8_t* data, int w, int h) {
    coverData = data;
    coverWidth = static_cast<int16_t>(w);
    coverHeight = static_cast<int16_t>(h);
    needsRender = true;
  }

  void setTitle(const char* t) {
    // UTF-8 safe so the reader cover page doesn't render '?' at the
    // break for long CJK titles.
    utf8SafeCopy(title, t, MAX_TITLE_LEN);
    needsRender = true;
  }

  void setAuthor(const char* a) {
    utf8SafeCopy(author, a, MAX_AUTHOR_LEN);
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const CoverPageView& v);

// ============================================================================
// ReaderMenuView - In-reader quick menu overlay
// ============================================================================

struct ReaderMenuView {
  static constexpr const char* const ITEMS[] = {"Chapters", "Settings", "Home"};
  static constexpr int ITEM_COUNT = 3;

  int8_t selected = 0;
  bool visible = false;
  bool needsRender = true;

  void show() {
    visible = true;
    selected = 0;
    needsRender = true;
  }

  void hide() {
    visible = false;
    needsRender = true;
  }

  void moveUp() {
    if (selected > 0) {
      selected--;
      needsRender = true;
    }
  }

  void moveDown() {
    if (selected < ITEM_COUNT - 1) {
      selected++;
      needsRender = true;
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, const ReaderMenuView& v);

// ============================================================================
// JumpToPageView - Page number input for reader
// ============================================================================

struct JumpToPageView {
  ButtonBar buttons{"Cancel", "Go", "-10", "+10"};
  int16_t targetPage = 1;
  int16_t maxPage = 1;
  bool needsRender = true;

  void setMaxPage(int max) {
    maxPage = static_cast<int16_t>(max);
    if (targetPage > maxPage) {
      targetPage = maxPage;
    }
    needsRender = true;
  }

  void setPage(int page) {
    if (page >= 1 && page <= maxPage) {
      targetPage = static_cast<int16_t>(page);
      needsRender = true;
    }
  }

  void incrementPage(int delta) {
    int newPage = targetPage + delta;
    if (newPage < 1) newPage = 1;
    if (newPage > maxPage) newPage = maxPage;
    if (newPage != targetPage) {
      targetPage = static_cast<int16_t>(newPage);
      needsRender = true;
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, const JumpToPageView& v);

// ============================================================================
// BookmarkListView - Scrollable list of bookmarks for the current book
// ============================================================================

struct BookmarkListView {
  static constexpr int MAX_VISIBLE = 12;

  std::vector<uint32_t> pages;  // sorted page numbers
  int totalBookPages = 1;       // for "page X of Y" display
  int selectedIndex = 0;
  int scrollOffset = 0;
  bool needsRender = true;

  void moveUp() {
    if (pages.empty()) return;
    selectedIndex = (selectedIndex == 0) ? static_cast<int>(pages.size()) - 1 : selectedIndex - 1;
    ensureVisible();
    needsRender = true;
  }

  void moveDown() {
    if (pages.empty()) return;
    selectedIndex = (selectedIndex + 1) % static_cast<int>(pages.size());
    ensureVisible();
    needsRender = true;
  }

  uint32_t selectedPage() const {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(pages.size())) {
      return pages[selectedIndex];
    }
    return 0;
  }

  void ensureVisible() {
    if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
    if (selectedIndex >= scrollOffset + MAX_VISIBLE) scrollOffset = selectedIndex - MAX_VISIBLE + 1;
  }
};

void render(const GfxRenderer& r, const Theme& t, const BookmarkListView& v);

// ============================================================================
// GlobalBookmarkListView - Cross-book bookmark index viewer
// ============================================================================

struct GlobalBookmarkListView {
  static constexpr int MAX_VISIBLE = 10;
  static constexpr int MAX_ENTRIES = 200;

  struct DisplayEntry {
    char bookTitle[48];
    char snippet[60];
    uint32_t page;
  };

  std::vector<DisplayEntry> entries;
  int selectedIndex = 0;
  int scrollOffset = 0;
  bool needsRender = true;

  void moveUp() {
    if (entries.empty()) return;
    selectedIndex = (selectedIndex == 0) ? static_cast<int>(entries.size()) - 1 : selectedIndex - 1;
    ensureVisible();
    needsRender = true;
  }

  void moveDown() {
    if (entries.empty()) return;
    selectedIndex = (selectedIndex + 1) % static_cast<int>(entries.size());
    ensureVisible();
    needsRender = true;
  }

  void ensureVisible() {
    if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
    if (selectedIndex >= scrollOffset + MAX_VISIBLE) scrollOffset = selectedIndex - MAX_VISIBLE + 1;
  }
};

void render(const GfxRenderer& r, const Theme& t, const GlobalBookmarkListView& v);

}  // namespace ui

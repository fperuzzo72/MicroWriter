#pragma once

#include <GfxRenderer.h>
#include <Theme.h>
#include <Utf8.h>

#include <cstdint>
#include <cstring>

#include "../Elements.h"

namespace ui {

// ============================================================================
// HomeView - CrossPoint-style home screen: last book read on top, a plain
// menu list directly below it. No carousel, no decorative art background —
// see docs/HOME_LAYOUT.md for why.
// ============================================================================

// Book block + menu list geometry, computed once from screen/theme metrics
// and shared between ui::render() (draws the in-memory cover, if any) and
// HomeState::renderCoverToCard() (draws an SD-loaded BMP cover on top of
// it afterwards). Keeping this in one function instead of duplicating the
// numbers in both places is what keeps those two draws lined up.
struct BookBlockLayout {
  int coverX, coverY, coverMaxW, coverMaxH;
  int menuStartY;  // First menu row's y, i.e. where the block "ends"
};

BookBlockLayout calculateBookBlockLayout(const GfxRenderer& r, const Theme& t);

struct HomeView {
  static constexpr int MAX_TITLE_LEN = 64;
  static constexpr int MAX_AUTHOR_LEN = 48;
  static constexpr int MAX_PATH_LEN = 128;

  // Current book info
  char bookTitle[MAX_TITLE_LEN] = {0};
  char bookAuthor[MAX_AUTHOR_LEN] = {0};
  char bookPath[MAX_PATH_LEN] = {0};
  bool hasBook = false;

  // Book progress (from LibraryIndex)
  uint16_t bookCurrentPage = 0;
  uint16_t bookTotalPages = 0;
  int16_t bookProgress = -1;  // 0-100, or -1 if unknown.
                              // Widened from int8_t in Batch 7 alongside
                              // LibraryIndex::Entry::progressPercent (audit
                              // #35). The -1 sentinel still fits; the wider
                              // type documents that we trust LibraryIndex's
                              // [0,100] clamp rather than the int8_t range.
  bool isChapterBased = false;  // true for EPUB (spine-based progress)

  // Cover image (external pointer - not owned)
  const uint8_t* coverData = nullptr;
  int16_t coverWidth = 0;
  int16_t coverHeight = 0;

  // Cover from BMP file (rendered by HomeState after ui::render)
  bool hasCoverBmp = false;

  // Font override for title/author (resolved by state, -1 = use theme default)
  int titleFontId = -1;

  // UI state
  int8_t batteryPercent = 100;
  bool needsRender = true;

  // Menu list — a flat, arrow-navigable list of rows directly below the
  // book block. "Continue reading" (when a book is open) is just row 0,
  // same as CrossPoint's own CLASSIC home theme treats it — not a
  // separate widget with its own input handling.
  enum class MenuTarget : uint8_t {
    ContinueReading,
    Files,
    Write,
    Dictionary,
    Games,
    Settings,
  };

  struct MenuEntry {
    char label[24];
    MenuTarget target;
  };

  static constexpr int MAX_MENU_ITEMS = 8;
  MenuEntry menuItems[MAX_MENU_ITEMS];
  int menuItemCount = 0;
  int selection = 0;  // Index into menuItems

  void setBook(const char* title, const char* author, const char* path) {
    // UTF-8 safe: a CJK title/author that would be sliced mid-codepoint
    // by strncpy shows up on Home as '?' at the break.
    utf8SafeCopy(bookTitle, title, MAX_TITLE_LEN);
    utf8SafeCopy(bookAuthor, author, MAX_AUTHOR_LEN);
    utf8SafeCopy(bookPath, path, MAX_PATH_LEN);
    hasBook = true;
    needsRender = true;
  }

  void clearBook() {
    bookTitle[0] = '\0';
    bookAuthor[0] = '\0';
    bookPath[0] = '\0';
    hasBook = false;
    coverData = nullptr;
    coverWidth = 0;
    coverHeight = 0;
    hasCoverBmp = false;
    bookCurrentPage = 0;
    bookTotalPages = 0;
    bookProgress = -1;
    isChapterBased = false;
    needsRender = true;
  }

  void setCover(const uint8_t* data, int w, int h) {
    coverData = data;
    coverWidth = static_cast<int16_t>(w);
    coverHeight = static_cast<int16_t>(h);
    needsRender = true;
  }

  void setBattery(int percent) {
    if (batteryPercent != percent) {
      batteryPercent = static_cast<int8_t>(percent);
      needsRender = true;
    }
  }

  void clearMenu() {
    menuItemCount = 0;
    selection = 0;
  }

  bool addMenuItem(const char* label, MenuTarget target) {
    if (menuItemCount >= MAX_MENU_ITEMS) return false;
    utf8SafeCopy(menuItems[menuItemCount].label, label, sizeof(menuItems[menuItemCount].label));
    menuItems[menuItemCount].target = target;
    menuItemCount++;
    return true;
  }

  void moveSelectionUp() {
    if (menuItemCount == 0) return;
    selection = (selection == 0) ? menuItemCount - 1 : selection - 1;
    needsRender = true;
  }

  void moveSelectionDown() {
    if (menuItemCount == 0) return;
    selection = (selection + 1) % menuItemCount;
    needsRender = true;
  }

  const MenuEntry* selectedEntry() const {
    if (selection < 0 || selection >= menuItemCount) return nullptr;
    return &menuItems[selection];
  }

  void clear() {
    clearBook();
    clearMenu();
    batteryPercent = 100;
  }
};

void render(const GfxRenderer& r, const Theme& t, const HomeView& v);

// ============================================================================
// FileListView - Paginated file browser
// ============================================================================

struct FileListView {
  static constexpr int MAX_FILES = 64;
  static constexpr int NAME_LEN = 48;
  static constexpr int PATH_LEN = 128;
  static constexpr int PAGE_SIZE = 12;

  // File entry structure (packed for memory efficiency)
  struct FileEntry {
    char name[NAME_LEN];
    bool isDirectory;
  };

  ButtonBar buttons{"Back", "Open", "", ""};

  // Path and file list
  char currentPath[PATH_LEN] = "/";
  FileEntry files[MAX_FILES];
  uint8_t fileCount = 0;
  uint8_t page = 0;
  uint8_t selected = 0;
  bool needsRender = true;

  void clear() {
    fileCount = 0;
    page = 0;
    selected = 0;
    needsRender = true;
  }

  bool addFile(const char* name, bool isDir) {
    if (fileCount < MAX_FILES) {
      // UTF-8 safe: a CJK filename longer than NAME_LEN-1 bytes would
      // otherwise be sliced mid-codepoint and render as '?' in the
      // file list. NAME_LEN=48 fits only ~15 CJK characters.
      utf8SafeCopy(files[fileCount].name, name, NAME_LEN);
      files[fileCount].isDirectory = isDir;
      fileCount++;
      return true;
    }
    return false;
  }

  void setPath(const char* path) {
    // Paths are ASCII-safe, but directory names on FAT32 LFN can be
    // UTF-8 encoded — use utf8SafeCopy for consistency.
    utf8SafeCopy(currentPath, path, PATH_LEN);
    needsRender = true;
  }

  int getPageCount() const { return (fileCount + PAGE_SIZE - 1) / PAGE_SIZE; }

  int getPageStart() const { return page * PAGE_SIZE; }

  int getPageEnd() const {
    int end = (page + 1) * PAGE_SIZE;
    return end > fileCount ? fileCount : end;
  }

  void moveUp() {
    if (selected > 0) {
      selected--;
      // Update page if needed
      if (selected < getPageStart()) {
        page--;
      }
      needsRender = true;
    }
  }

  void moveDown() {
    if (selected < fileCount - 1) {
      selected++;
      // Update page if needed
      if (selected >= getPageEnd()) {
        page++;
      }
      needsRender = true;
    }
  }

  void pageUp() {
    if (page > 0) {
      page--;
      selected = page * PAGE_SIZE;
      needsRender = true;
    }
  }

  void pageDown() {
    if (page < getPageCount() - 1) {
      page++;
      selected = page * PAGE_SIZE;
      needsRender = true;
    }
  }

  const FileEntry* getSelectedFile() const {
    if (selected < fileCount) {
      return &files[selected];
    }
    return nullptr;
  }
};

void render(const GfxRenderer& r, const Theme& t, const FileListView& v);

// ============================================================================
// ChapterListView - Chapter/TOC selection for readers
// ============================================================================

struct ChapterListView {
  static constexpr int MAX_CHAPTERS = 64;
  static constexpr int TITLE_LEN = 64;

  struct Chapter {
    char title[TITLE_LEN];
    uint16_t pageNum;
    uint8_t depth;  // Nesting level (0 = root)
  };

  ButtonBar buttons{"Back", "Go", "", ""};
  Chapter chapters[MAX_CHAPTERS];
  uint8_t chapterCount = 0;
  uint8_t currentChapter = 0;  // The chapter user is currently reading
  uint8_t selected = 0;
  uint8_t scrollOffset = 0;  // First visible item
  bool needsRender = true;

  void clear() {
    chapterCount = 0;
    selected = 0;
    scrollOffset = 0;
    needsRender = true;
  }

  bool addChapter(const char* title, uint16_t pageNum, uint8_t depth = 0) {
    if (chapterCount < MAX_CHAPTERS) {
      // UTF-8 safe: chapter titles in CJK books like 「第一章」 etc.
      // would otherwise be sliced mid-codepoint near the buffer limit.
      utf8SafeCopy(chapters[chapterCount].title, title, TITLE_LEN);
      chapters[chapterCount].pageNum = pageNum;
      chapters[chapterCount].depth = depth;
      chapterCount++;
      return true;
    }
    return false;
  }

  void setCurrentChapter(uint8_t idx) {
    // Clamp to valid range. The caller (populateTocView path) is expected
    // to pass an index < chapterCount, but defensive bounds prevent a
    // stale `idx` past the new list size from leaving `selected` past
    // chapters[chapterCount - 1] (which subsequent rendering would
    // dereference out-of-bounds).
    if (chapterCount == 0) {
      currentChapter = 0;
      selected = 0;
      scrollOffset = 0;
      needsRender = true;
      return;
    }
    if (idx >= chapterCount) idx = chapterCount - 1;
    currentChapter = idx;
    selected = idx;
    scrollOffset = idx;  // Start with current chapter at top
    needsRender = true;
  }

  void moveUp() {
    if (chapterCount == 0) return;
    selected = (selected == 0) ? chapterCount - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    if (chapterCount == 0) return;
    selected = (selected + 1) % chapterCount;
    needsRender = true;
  }

  void movePageUp(int count) {
    if (chapterCount == 0 || count <= 0) return;
    selected = (selected >= count) ? selected - count : 0;
    needsRender = true;
  }

  void movePageDown(int count) {
    if (chapterCount == 0 || count <= 0) return;
    int target = selected + count;
    selected = (target < chapterCount) ? static_cast<uint8_t>(target) : chapterCount - 1;
    needsRender = true;
  }

  // Adjust scroll to keep selected visible (call before rendering)
  void ensureVisible(int visibleCount) {
    if (chapterCount == 0 || visibleCount <= 0) return;
    const int sel = selected;
    const int off = scrollOffset;
    if (sel < off) {
      scrollOffset = static_cast<uint8_t>(sel);
    } else if (sel >= off + visibleCount) {
      scrollOffset = static_cast<uint8_t>(sel - visibleCount + 1);
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, ChapterListView& v);

}  // namespace ui

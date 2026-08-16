#pragma once

#include <GfxRenderer.h>
#include <Theme.h>
#include <Utf8.h>

#include <cstdint>
#include <cstring>

#include "../Elements.h"

namespace ui {

// ============================================================================
// HomeView - Main home screen with current book and direct action buttons
// ============================================================================

struct CardDimensions {
  int x, y, width, height;

  struct CoverArea {
    int x, y, width, height;
  };

  static CardDimensions calculate(int screenWidth, int screenHeight) {
    // Matched to sumi-e art template cover rectangle
    const int w = 300;
    const int h = 415;
    const int x = (screenWidth - w) / 2;  // centered: (480-300)/2 = 90
    constexpr int y = 128;
    return {x, y, w, h};
  }

  CoverArea getCoverArea() const {
    constexpr int padding = 4;
    return {x + padding, y + padding, width - 2 * padding, height - 2 * padding};
  }
};

struct HomeView {
  static constexpr int MAX_TITLE_LEN = 64;
  static constexpr int MAX_AUTHOR_LEN = 48;
  static constexpr int MAX_PATH_LEN = 128;
  static constexpr int MAX_RECENT_BOOKS = 10;  // All recent books in carousel

  // Current book info (the one shown large)
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
  bool useArtBackground = false;  // When true, skip clearScreen/buttonBar (baked into art)
  
  // Library carousel state
  static constexpr int MAX_THUMB_LEN = 80;

  struct RecentBookEntry {
    char title[MAX_TITLE_LEN];
    char author[MAX_AUTHOR_LEN];
    char path[MAX_PATH_LEN];
    char thumbPath[MAX_THUMB_LEN];  // Persisted thumbnail path
    uint16_t progress;
    bool hasThumbnail;
  };
  
  RecentBookEntry recentBooks[MAX_RECENT_BOOKS];
  int recentBookCount = 0;
  int selectedBookIndex = 0;  // 0 = current book, 1+ = recent books
  bool inLibraryMode = false;  // When true, show carousel at bottom

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
  
  void addRecentBook(const char* title, const char* author, const char* path,
                     uint16_t progress, bool hasThumbnail,
                     const char* thumbPath = nullptr) {
    if (recentBookCount >= MAX_RECENT_BOOKS) return;
    auto& entry = recentBooks[recentBookCount];
    // UTF-8 safe: these are the carousel entries on the Home view.
    utf8SafeCopy(entry.title, title, MAX_TITLE_LEN);
    utf8SafeCopy(entry.author, author, MAX_AUTHOR_LEN);
    utf8SafeCopy(entry.path, path, MAX_PATH_LEN);
    if (thumbPath && thumbPath[0] != '\0') {
      utf8SafeCopy(entry.thumbPath, thumbPath, MAX_THUMB_LEN);
    } else {
      entry.thumbPath[0] = '\0';
    }
    entry.progress = progress;
    entry.hasThumbnail = hasThumbnail;
    recentBookCount++;
  }
  
  void clearRecentBooks() {
    recentBookCount = 0;
    selectedBookIndex = 0;
    inLibraryMode = false;
  }
  
  void selectNextBook() {
    if (recentBookCount > 0) {
      selectedBookIndex = (selectedBookIndex + 1) % (recentBookCount + 1);
      needsRender = true;
    }
  }
  
  void selectPrevBook() {
    if (recentBookCount > 0) {
      selectedBookIndex = (selectedBookIndex + recentBookCount) % (recentBookCount + 1);
      needsRender = true;
    }
  }
  
  const char* getSelectedPath() const {
    if (selectedBookIndex == 0) {
      return bookPath;
    } else if (selectedBookIndex - 1 < recentBookCount) {
      return recentBooks[selectedBookIndex - 1].path;
    }
    return bookPath;
  }

  void clear() {
    clearBook();
    clearRecentBooks();
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

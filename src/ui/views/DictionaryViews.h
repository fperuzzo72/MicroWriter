#pragma once

#include <GfxRenderer.h>
#include <Theme.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../Elements.h"

// Forward declarations so we don't pull the whole Epub/Page.h into every
// caller. The ReaderState includes Page.h directly when populating the view.
class Page;

namespace ui {

// ============================================================================
// DictWordSelectView — Word picker overlay for the in-reader dictionary
//
// Shows the current page with a single word highlighted; directional buttons
// move the cursor across words/rows; Confirm triggers a lookup. Ported from
// the Crosspoint fork's DictionaryWordSelectActivity, adapted to SUMI's
// view/state split (the view is pure data + renderer — the state owns the
// Page and drives the lookup flow).
// ============================================================================

struct DictWordSelectView {
  // Each extracted word with its screen position and row assignment.
  // `lookupText` differs from `text` when the word spans a hyphenated line
  // break and we merged the two halves for the lookup.
  struct WordInfo {
    std::string text;         // Rendered glyph text (just the piece on this line)
    std::string lookupText;   // What to actually pass to Dictionary::lookup
    int16_t screenX = 0;
    int16_t screenY = 0;
    int16_t width = 0;
    int16_t row = 0;
    int continuationIndex = -1;  // Sister half on the next row, if hyphenated
    int continuationOf = -1;     // We are ourselves a second half
  };

  struct Row {
    int16_t yPos;
    std::vector<int> wordIndices;

    Row() : yPos(0) {}
    explicit Row(int16_t y) : yPos(y) {}
  };

  // Non-owning pointer to the page to render underneath the highlight.
  // ReaderState keeps the Page alive for the lifetime of the overlay.
  const Page* page = nullptr;
  int fontId = 0;
  int marginLeft = 0;
  int marginTop = 0;

  std::vector<WordInfo> words;
  std::vector<Row> rows;

  int currentRow = 0;
  int currentWordInRow = 0;
  bool needsRender = true;

  // Rebuild `words` and `rows` from the page. `renderer` is needed only to
  // compute word widths; it is not retained.
  void extractWords(const GfxRenderer& renderer);

  // Merge hyphenated line breaks within the page. Optionally also merges
  // the last word with the first word of the next page if it ended with
  // a hyphen (soft or hard). Pass an empty string for nextPageFirstWord
  // if there is no next page.
  void mergeHyphenatedWords(const std::string& nextPageFirstWord = "");

  // Returns the currently-highlighted WordInfo, or nullptr if there is
  // nothing to pick (empty page, no rows).
  const WordInfo* selectedWord() const;

  // Directional navigation helpers. prev/next are relative to the
  // user's current orientation — ReaderState maps physical buttons
  // to these calls based on Settings::orientation.
  void movePrevRow();
  void moveNextRow();
  void movePrevWord();
  void moveNextWord();

 private:
  // Find the word in `targetRow` whose horizontal center is closest to
  // the currently-selected word's center. Used to preserve column
  // position when jumping rows.
  int findClosestWord(int targetRow) const;
};

void render(const GfxRenderer& r, const Theme& t, const DictWordSelectView& v);

// ============================================================================
// DictDefinitionView — Paginated display of a single dictionary entry.
//
// The definition body can be many screens long; we pre-wrap once when the
// view is populated and page through with Left/Right.
// ============================================================================

struct DictDefinitionView {
  // One labelled body from a single dictionary. The multi-dict lookup path
  // produces one of these per dictionary that matched; the legacy single-
  // dict path is just a single-element vector.
  struct Section {
    std::string sourceLabel;  // dictionary display name (+ "(stem: foo)" if stemmed)
    std::string body;         // already-stripped definition text
  };

  std::string headword;
  std::string definition;  // legacy single-section field; mirrors sections[0].body when sections.size() == 1
  std::vector<Section> sections;
  int fontId = 0;

  std::vector<std::string> wrappedLines;
  int currentPage = 0;
  int linesPerPage = 1;
  int totalPages = 1;

  bool needsRender = true;

  // Sentinel byte that wrapText() prepends to section-header lines so
  // render() can style them differently. Picked from the C0 control range
  // so it can never collide with anything in a UTF-8 dictionary body.
  static constexpr char SECTION_HEADER_MARKER = '\x01';

  // Populate wrappedLines. If `sections` is non-empty, lays out each section
  // with a styled header line; otherwise falls back to wrapping `definition`
  // alone (legacy single-section behaviour). Call after populating fields.
  void wrapText(const GfxRenderer& renderer, const Theme& theme);

  void pageForward() {
    if (currentPage < totalPages - 1) {
      currentPage++;
      needsRender = true;
    }
  }

  void pageBackward() {
    if (currentPage > 0) {
      currentPage--;
      needsRender = true;
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, const DictDefinitionView& v);

// ============================================================================
// DictSuggestionsView — "Did you mean?" list for fuzzy-match fallbacks.
// ============================================================================

struct DictSuggestionsView {
  static constexpr int MAX_SUGGESTIONS = 12;

  std::string originalWord;
  std::vector<std::string> suggestions;
  int selectedIndex = 0;
  bool needsRender = true;

  void moveUp() {
    if (suggestions.empty()) return;
    selectedIndex = (selectedIndex == 0) ? static_cast<int>(suggestions.size()) - 1 : selectedIndex - 1;
    needsRender = true;
  }

  void moveDown() {
    if (suggestions.empty()) return;
    selectedIndex = (selectedIndex + 1) % static_cast<int>(suggestions.size());
    needsRender = true;
  }

  const std::string* selected() const {
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(suggestions.size())) {
      return nullptr;
    }
    return &suggestions[selectedIndex];
  }
};

void render(const GfxRenderer& r, const Theme& t, const DictSuggestionsView& v);

// ============================================================================
// DictDictSelectView — Chooser for the active dictionary (multi-dict support)
//
// Shown from Settings when the user picks "Dictionary" so they can switch
// between the dictionaries dropped into /dictionary/<name>/.
// ============================================================================

struct DictDictSelectView {
  struct Entry {
    std::string key;          // directory name — what gets persisted
    std::string displayName;  // human-readable name from .ifo
    uint32_t wordCount = 0;
  };

  std::vector<Entry> entries;
  int selectedIndex = 0;
  bool needsRender = true;

  void moveUp() {
    if (entries.empty()) return;
    selectedIndex = (selectedIndex == 0) ? static_cast<int>(entries.size()) - 1 : selectedIndex - 1;
    needsRender = true;
  }

  void moveDown() {
    if (entries.empty()) return;
    selectedIndex = (selectedIndex + 1) % static_cast<int>(entries.size());
    needsRender = true;
  }

  const Entry* selected() const {
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
      return nullptr;
    }
    return &entries[selectedIndex];
  }
};

void render(const GfxRenderer& r, const Theme& t, const DictDictSelectView& v);

// ============================================================================
// DictHistoryView — Scrollable list of previously looked-up words.
//
// Loaded from LookupHistory::load() for the current book. The user can
// re-look up a word (Center/Right) or delete it from history (Left).
// ============================================================================

struct DictHistoryView {
  static constexpr int MAX_VISIBLE = 12;

  std::vector<std::string> words;  // loaded from LookupHistory::load()
  int selectedIndex = 0;
  int scrollOffset = 0;
  bool needsRender = true;

  void moveUp() {
    if (words.empty()) return;
    selectedIndex = (selectedIndex == 0) ? static_cast<int>(words.size()) - 1 : selectedIndex - 1;
    ensureVisible();
    needsRender = true;
  }

  void moveDown() {
    if (words.empty()) return;
    selectedIndex = (selectedIndex + 1) % static_cast<int>(words.size());
    ensureVisible();
    needsRender = true;
  }

  const std::string* selected() const {
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(words.size())) return nullptr;
    return &words[selectedIndex];
  }

  void ensureVisible() {
    if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
    if (selectedIndex >= scrollOffset + MAX_VISIBLE) scrollOffset = selectedIndex - MAX_VISIBLE + 1;
  }
};

void render(const GfxRenderer& r, const Theme& t, const DictHistoryView& v);

}  // namespace ui

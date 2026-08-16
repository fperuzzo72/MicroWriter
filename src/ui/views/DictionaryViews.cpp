#include "DictionaryViews.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ui {

// ============================================================================
// DictWordSelectView — word extraction, navigation, rendering
// ============================================================================

namespace {

// en-dash (U+2013) and em-dash (U+2014) — split a WordData's text on these
// so the lookup picks the right side of "before—after".
bool isDashBytes(const std::string& s, size_t i) {
  if (i + 2 >= s.size()) return false;
  const auto b0 = static_cast<uint8_t>(s[i]);
  const auto b1 = static_cast<uint8_t>(s[i + 1]);
  const auto b2 = static_cast<uint8_t>(s[i + 2]);
  return b0 == 0xE2 && b1 == 0x80 && (b2 == 0x93 || b2 == 0x94);
}

// Soft hyphen U+00AD = 0xC2 0xAD. Ends-with check.
bool endsWithSoftHyphen(const std::string& s) {
  return s.size() >= 2 && static_cast<uint8_t>(s[s.size() - 2]) == 0xC2 &&
         static_cast<uint8_t>(s[s.size() - 1]) == 0xAD;
}

bool endsWithHyphen(const std::string& s) {
  if (s.empty()) return false;
  return s.back() == '-' || endsWithSoftHyphen(s);
}

std::string stripTrailingHyphen(const std::string& s) {
  if (s.empty()) return s;
  if (s.back() == '-') return s.substr(0, s.size() - 1);
  if (endsWithSoftHyphen(s)) return s.substr(0, s.size() - 2);
  return s;
}

}  // namespace

void DictWordSelectView::extractWords(const GfxRenderer& renderer) {
  words.clear();
  rows.clear();

  if (page == nullptr) return;

  for (const auto& element : page->elements) {
    if (!element || element->getTag() != TAG_PageLine) {
      continue;
    }

    const auto& line = static_cast<const PageLine&>(*element);
    const TextBlock& block = line.getTextBlock();
    const auto& wordList = block.getWords();  // std::vector<TextBlock::WordData>

    for (const auto& wd : wordList) {
      // SUMI TextBlock stores per-word xPos relative to the block start;
      // element.xPos is the block's origin on the page, plus whatever
      // margin we're rendering at. The Crosspoint version stored xPos
      // separately from wordList — our shape is the same data with one
      // fewer indirection.
      const int16_t blockX = line.xPos;
      const int16_t screenX = blockX + wd.xPos + marginLeft;
      const int16_t screenY = line.yPos + marginTop;
      const std::string& wordText = wd.text;

      // Find en-dash / em-dash split points. A word like "before—after"
      // arrives as a single TextBlock word; for dictionary lookup we
      // want two separate hits.
      std::vector<size_t> splitStarts;
      size_t partStart = 0;
      for (size_t i = 0; i < wordText.size();) {
        if (isDashBytes(wordText, i)) {
          if (i > partStart) splitStarts.push_back(partStart);
          i += 3;
          partStart = i;
        } else {
          i++;
        }
      }
      if (partStart < wordText.size()) splitStarts.push_back(partStart);

      // Fast path: no em/en-dash in the word — push it whole.
      if (splitStarts.size() <= 1 && partStart == 0) {
        WordInfo wi;
        wi.text = wordText;
        wi.lookupText = wordText;
        wi.screenX = screenX;
        wi.screenY = screenY;
        wi.width = renderer.getTextWidth(fontId, wordText.c_str());
        words.push_back(std::move(wi));
        continue;
      }

      // Slow path: split on dashes.
      for (size_t splitIndex = 0; splitIndex < splitStarts.size(); splitIndex++) {
        const size_t start = splitStarts[splitIndex];
        const size_t end = (splitIndex + 1 < splitStarts.size()) ? splitStarts[splitIndex + 1] : wordText.size();

        // Trim trailing dash bytes off this sub-word.
        size_t textEnd = end;
        while (textEnd >= 3 && isDashBytes(wordText, textEnd - 3)) {
          textEnd -= 3;
        }
        if (textEnd <= start) continue;

        std::string part = wordText.substr(start, textEnd - start);
        if (part.empty()) continue;

        const std::string prefix = wordText.substr(0, start);
        const int16_t offsetX = prefix.empty() ? 0 : renderer.getTextWidth(fontId, prefix.c_str());
        const int16_t partWidth = renderer.getTextWidth(fontId, part.c_str());

        WordInfo wi;
        wi.text = part;
        wi.lookupText = part;
        wi.screenX = static_cast<int16_t>(screenX + offsetX);
        wi.screenY = screenY;
        wi.width = partWidth;
        words.push_back(std::move(wi));
      }
    }
  }

  if (words.empty()) return;

  int16_t currentY = words[0].screenY;
  rows.push_back(Row(currentY));

  for (size_t i = 0; i < words.size(); i++) {
    if (std::abs(words[i].screenY - currentY) > 2) {
      currentY = words[i].screenY;
      rows.push_back(Row(currentY));
    }
    words[i].row = static_cast<int16_t>(rows.size() - 1);
    rows.back().wordIndices.push_back(static_cast<int>(i));
  }

  // Start the cursor one-third of the way down the page — the Crosspoint
  // fork does this so the first button press usually stays on the same
  // row (reading from left to right) instead of jumping immediately.
  if (!rows.empty()) {
    currentRow = static_cast<int>(rows.size()) / 3;
    currentWordInRow = 0;
  }
}

void DictWordSelectView::mergeHyphenatedWords(const std::string& nextPageFirstWord) {
  // Within-page hyphenation: last word of row N ends with '-' or U+00AD,
  // merge it with first word of row N+1 for the lookup text.
  for (size_t rowIndex = 0; rowIndex + 1 < rows.size(); rowIndex++) {
    if (rows[rowIndex].wordIndices.empty() || rows[rowIndex + 1].wordIndices.empty()) continue;

    const int lastWordIdx = rows[rowIndex].wordIndices.back();
    const std::string& lastWord = words[lastWordIdx].text;
    if (!endsWithHyphen(lastWord)) continue;

    const int nextWordIdx = rows[rowIndex + 1].wordIndices.front();

    words[lastWordIdx].continuationIndex = nextWordIdx;
    words[nextWordIdx].continuationOf = lastWordIdx;

    const std::string firstPart = stripTrailingHyphen(lastWord);
    const std::string merged = firstPart + words[nextWordIdx].text;
    words[lastWordIdx].lookupText = merged;
    words[nextWordIdx].lookupText = merged;
    // Crosspoint fork quirk: the second half "knows" it's been merged
    // even though its continuationIndex points at itself.
    words[nextWordIdx].continuationIndex = nextWordIdx;
  }

  // Cross-page hyphenation: last word of the LAST row, if hyphenated,
  // merges with the first word of the next page (supplied by caller).
  if (!nextPageFirstWord.empty() && !rows.empty()) {
    const int lastWordIdx = rows.back().wordIndices.back();
    const std::string& lastWord = words[lastWordIdx].text;
    if (endsWithHyphen(lastWord)) {
      const std::string firstPart = stripTrailingHyphen(lastWord);
      words[lastWordIdx].lookupText = firstPart + nextPageFirstWord;
    }
  }

  // Drop any empty rows we may have accidentally left behind.
  rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& row) { return row.wordIndices.empty(); }),
             rows.end());

  // Clamp cursor if rows shrank.
  if (currentRow >= static_cast<int>(rows.size())) {
    currentRow = std::max(0, static_cast<int>(rows.size()) - 1);
  }
  if (!rows.empty() && currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    currentWordInRow = static_cast<int>(rows[currentRow].wordIndices.size()) - 1;
  }
}

const DictWordSelectView::WordInfo* DictWordSelectView::selectedWord() const {
  if (rows.empty()) return nullptr;
  if (currentRow < 0 || currentRow >= static_cast<int>(rows.size())) return nullptr;
  const auto& row = rows[currentRow];
  if (currentWordInRow < 0 || currentWordInRow >= static_cast<int>(row.wordIndices.size())) return nullptr;
  const int idx = row.wordIndices[currentWordInRow];
  if (idx < 0 || idx >= static_cast<int>(words.size())) return nullptr;
  return &words[idx];
}

int DictWordSelectView::findClosestWord(int targetRow) const {
  if (targetRow < 0 || targetRow >= static_cast<int>(rows.size())) return 0;
  // Defensive bounds: currentRow / currentWordInRow / wordIdx are mutable
  // state that the layout-clamp pass at the top of render() corrects, but
  // findClosestWord can be called between layout updates (e.g. movePrevRow
  // → findClosestWord → currentRow=targetRow). A stale currentRow past
  // rows.size() — or a stale currentWordInRow past the row's length, or
  // a wordIdx past words.size() — would otherwise read out of bounds and
  // crash. Cheap insurance for a hot UI path.
  if (currentRow < 0 || currentRow >= static_cast<int>(rows.size())) return 0;
  if (rows[currentRow].wordIndices.empty()) return 0;
  if (currentWordInRow < 0 ||
      currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    return 0;
  }

  const int wordIdx = rows[currentRow].wordIndices[currentWordInRow];
  if (wordIdx < 0 || wordIdx >= static_cast<int>(words.size())) return 0;
  const int currentCenterX = words[wordIdx].screenX + words[wordIdx].width / 2;

  int bestMatch = 0;
  int bestDist = INT_MAX;
  for (int i = 0; i < static_cast<int>(rows[targetRow].wordIndices.size()); i++) {
    const int idx = rows[targetRow].wordIndices[i];
    if (idx < 0 || idx >= static_cast<int>(words.size())) continue;
    const int centerX = words[idx].screenX + words[idx].width / 2;
    const int dist = std::abs(centerX - currentCenterX);
    if (dist < bestDist) {
      bestDist = dist;
      bestMatch = i;
    }
  }
  return bestMatch;
}

void DictWordSelectView::movePrevRow() {
  if (rows.empty()) return;
  const int targetRow = (currentRow > 0) ? currentRow - 1 : static_cast<int>(rows.size()) - 1;
  currentWordInRow = findClosestWord(targetRow);
  currentRow = targetRow;
  needsRender = true;
}

void DictWordSelectView::moveNextRow() {
  if (rows.empty()) return;
  const int targetRow = (currentRow < static_cast<int>(rows.size()) - 1) ? currentRow + 1 : 0;
  currentWordInRow = findClosestWord(targetRow);
  currentRow = targetRow;
  needsRender = true;
}

void DictWordSelectView::movePrevWord() {
  if (rows.empty()) return;
  if (currentWordInRow > 0) {
    currentWordInRow--;
  } else if (rows.size() > 1) {
    currentRow = (currentRow > 0) ? currentRow - 1 : static_cast<int>(rows.size()) - 1;
    currentWordInRow = static_cast<int>(rows[currentRow].wordIndices.size()) - 1;
  }
  needsRender = true;
}

void DictWordSelectView::moveNextWord() {
  if (rows.empty()) return;
  if (currentWordInRow < static_cast<int>(rows[currentRow].wordIndices.size()) - 1) {
    currentWordInRow++;
  } else if (rows.size() > 1) {
    currentRow = (currentRow < static_cast<int>(rows.size()) - 1) ? currentRow + 1 : 0;
    currentWordInRow = 0;
  }
  needsRender = true;
}

void render(const GfxRenderer& r, const Theme& t, const DictWordSelectView& v) {
  r.clearScreen(t.backgroundColor);

  // Render the underlying page first so the user sees where each word is.
  if (v.page != nullptr) {
    v.page->render(const_cast<GfxRenderer&>(r), v.fontId, v.marginLeft, v.marginTop, t.primaryTextBlack);
  }

  // Bounds-check currentRow / currentWordInRow / wordIdx against the
  // current vectors before dereferencing — same defensive pattern as
  // findClosestWord. If the layout-clamp pass at the top of render
  // (lines 197-202) hasn't run yet for this frame, stale indices from
  // the previous layout could be out of range.
  const bool indicesValid =
      !v.words.empty() && !v.rows.empty() &&
      v.currentRow >= 0 && v.currentRow < static_cast<int>(v.rows.size()) &&
      !v.rows[v.currentRow].wordIndices.empty() &&
      v.currentWordInRow >= 0 &&
      v.currentWordInRow < static_cast<int>(v.rows[v.currentRow].wordIndices.size());

  if (!indicesValid) {
    // No words to pick — show a hint so the user knows back is the only
    // way out of this mode.
    centeredText(r, t, r.getScreenHeight() / 2, "No words on this page");
  } else {
    const int wordIdx = v.rows[v.currentRow].wordIndices[v.currentWordInRow];
    if (wordIdx < 0 || wordIdx >= static_cast<int>(v.words.size())) {
      centeredText(r, t, r.getScreenHeight() / 2, "No words on this page");
      return;
    }
    const auto& w = v.words[wordIdx];
    const int lineHeight = r.getLineHeight(v.fontId);

    // Inverted highlight: black fill + white text. SUMI uses bool for
    // ink colour so "!primaryTextBlack" gets us the opposite.
    r.fillRect(w.screenX - 1, w.screenY - 1, w.width + 2, lineHeight + 2, t.primaryTextBlack);
    r.drawText(v.fontId, w.screenX, w.screenY, w.text.c_str(), !t.primaryTextBlack);

    // If this word has a hyphenated sister on the next row, highlight
    // both halves so the user can see they're treated as one lookup.
    int otherIdx = (w.continuationOf >= 0) ? w.continuationOf : -1;
    if (otherIdx < 0 && w.continuationIndex >= 0 && w.continuationIndex != wordIdx) {
      otherIdx = w.continuationIndex;
    }
    if (otherIdx >= 0 && otherIdx < static_cast<int>(v.words.size())) {
      const auto& other = v.words[otherIdx];
      r.fillRect(other.screenX - 1, other.screenY - 1, other.width + 2, lineHeight + 2, t.primaryTextBlack);
      r.drawText(v.fontId, other.screenX, other.screenY, other.text.c_str(), !t.primaryTextBlack);
    }
  }

  buttonBar(r, t, "Back", "Look up", "Prev", "Next");
  r.displayBuffer();
}

// ============================================================================
// DictDefinitionView — pre-wrap + render paginated definition
// ============================================================================

void DictDefinitionView::wrapText(const GfxRenderer& renderer, const Theme& theme) {
  wrappedLines.clear();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(fontId);

  const int sidePad = theme.screenMarginSide + 8;
  const int topReserve = 60;     // headword + gap
  const int bottomReserve = 55;  // button hints + page indicator

  const int maxWidth = screenWidth - (sidePad * 2);
  linesPerPage = std::max(1, (screenHeight - topReserve - bottomReserve) / lineHeight);

  std::string currentLine;
  std::string currentWord;

  auto flushWord = [&](bool forceBreak) {
    if (currentWord.empty()) {
      if (forceBreak) {
        wrappedLines.push_back(currentLine);
        currentLine.clear();
      }
      return;
    }
    if (currentLine.empty()) {
      currentLine = currentWord;
    } else {
      const std::string test = currentLine + " " + currentWord;
      if (renderer.getTextWidth(fontId, test.c_str()) <= maxWidth) {
        currentLine = test;
      } else {
        wrappedLines.push_back(currentLine);
        currentLine = currentWord;
      }
    }
    currentWord.clear();
    if (forceBreak) {
      wrappedLines.push_back(currentLine);
      currentLine.clear();
    }
  };

  auto wrapBody = [&](const std::string& body) {
    currentLine.clear();
    currentWord.clear();
    for (size_t i = 0; i <= body.size(); i++) {
      const char c = (i < body.size()) ? body[i] : '\0';
      if (c == '\n' || c == '\0') {
        flushWord(true);
      } else if (c == ' ') {
        flushWord(false);
      } else {
        currentWord += c;
      }
    }
    // Drop a trailing blank if the body ended on a '\n' followed by nothing.
    if (!wrappedLines.empty() && wrappedLines.back().empty() &&
        body.find_last_not_of('\n') != std::string::npos) {
      wrappedLines.pop_back();
    }
  };

  if (!sections.empty()) {
    // Multi-section path: each section gets a header line marked with the
    // sentinel byte so render() can style it (centered + bold + divider
    // above). One blank line between sections so the visual separation is
    // obvious even when a section ends close to a page boundary.
    for (size_t s = 0; s < sections.size(); s++) {
      if (s > 0) wrappedLines.push_back("");
      std::string header;
      header.push_back(SECTION_HEADER_MARKER);
      header += sections[s].sourceLabel;
      wrappedLines.push_back(std::move(header));
      wrapBody(sections[s].body);
    }
  } else {
    wrapBody(definition);
  }

  totalPages = std::max(1, (static_cast<int>(wrappedLines.size()) + linesPerPage - 1) / linesPerPage);
  if (currentPage >= totalPages) currentPage = totalPages - 1;
  if (currentPage < 0) currentPage = 0;
}

void render(const GfxRenderer& r, const Theme& t, const DictDefinitionView& v) {
  r.clearScreen(t.backgroundColor);

  // Headword in bold at the top.
  title(r, t, t.screenMarginTop, v.headword.c_str());

  const int screenW = r.getScreenWidth();
  const int screenH = r.getScreenHeight();
  const int lineHeight = r.getLineHeight(v.fontId);
  const int sidePad = t.screenMarginSide + 8;
  const int bodyTop = 60;
  const int bodyBottom = screenH - 55;

  // Draw the current page of wrapped lines. Section header lines (marked
  // with DictDefinitionView::SECTION_HEADER_MARKER as the first byte) get
  // a divider rule above and render in the small font, centered — visually
  // distinct from definition body but unobtrusive on a 480-wide screen.
  const int startLine = v.currentPage * v.linesPerPage;
  const int endLine =
      std::min(startLine + v.linesPerPage, static_cast<int>(v.wrappedLines.size()));
  for (int i = startLine; i < endLine; i++) {
    const int y = bodyTop + (i - startLine) * lineHeight;
    if (y + lineHeight > bodyBottom) break;
    const std::string& line = v.wrappedLines[i];
    if (!line.empty() && line[0] == DictDefinitionView::SECTION_HEADER_MARKER) {
      const char* label = line.c_str() + 1;
      const int labelW = r.getTextWidth(t.smallFontId, label);
      const int lx = std::max(sidePad, (screenW - labelW) / 2);
      // Divider rule across the body width, above the label.
      const int ruleY = y + lineHeight / 2 - 1;
      r.drawLine(sidePad, ruleY, screenW - sidePad, ruleY);
      r.drawText(t.smallFontId, lx, y + 2, label, t.primaryTextBlack);
    } else {
      r.drawText(v.fontId, sidePad, y, line.c_str(), t.primaryTextBlack);
    }
  }

  // Page indicator at bottom-right when multi-page.
  if (v.totalPages > 1) {
    char pageStr[24];
    std::snprintf(pageStr, sizeof(pageStr), "%d / %d", v.currentPage + 1, v.totalPages);
    const int w = r.getTextWidth(t.smallFontId, pageStr);
    r.drawText(t.smallFontId, screenW - sidePad - w, bodyBottom + 4, pageStr, t.primaryTextBlack);
  }

  buttonBar(r, t, "Back", "Done", "<", ">");
  r.displayBuffer();
}

// ============================================================================
// DictSuggestionsView — "Did you mean?" list
// ============================================================================

void render(const GfxRenderer& r, const Theme& t, const DictSuggestionsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Did you mean?");

  // Subtitle: the word the user typed/picked that wasn't found.
  const int subtitleY = 50;
  std::string subtitle = "\"";
  subtitle += v.originalWord;
  subtitle += "\" not found";
  centeredText(r, t, subtitleY, subtitle.c_str());

  if (v.suggestions.empty()) {
    centeredText(r, t, r.getScreenHeight() / 2, "No suggestions");
  } else {
    const int startY = 90;
    const int itemH = t.menuItemHeight + t.itemSpacing;
    const int screenH = r.getScreenHeight();
    const int bottomReserve = 45;
    const int visibleItems = std::max(1, (screenH - startY - bottomReserve) / itemH);

    // Window the list around the selection so long lists still show the cursor.
    int scrollOffset = 0;
    const int total = static_cast<int>(v.suggestions.size());
    if (total > visibleItems) {
      scrollOffset = v.selectedIndex - visibleItems / 2;
      if (scrollOffset < 0) scrollOffset = 0;
      if (scrollOffset + visibleItems > total) scrollOffset = total - visibleItems;
    }

    const int end = std::min(scrollOffset + visibleItems, total);
    for (int i = scrollOffset; i < end; i++) {
      const int y = startY + (i - scrollOffset) * itemH;
      menuItem(r, t, y, v.suggestions[i].c_str(), i == v.selectedIndex);
    }
  }

  buttonBar(r, t, "Back", "Select", "Up", "Down");
  r.displayBuffer();
}

// ============================================================================
// DictDictSelectView — dictionary chooser (Settings → Dictionary)
// ============================================================================

void render(const GfxRenderer& r, const Theme& t, const DictDictSelectView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Choose Dictionary");

  if (v.entries.empty()) {
    centeredText(r, t, r.getScreenHeight() / 2, "No dictionaries installed");
    centeredText(r, t, r.getScreenHeight() / 2 + 30, "Add one to /dictionary/ on SD");
  } else {
    const int startY = 60;
    const int itemH = t.menuItemHeight + t.itemSpacing;
    const int screenH = r.getScreenHeight();
    const int bottomReserve = 45;
    const int visibleItems = std::max(1, (screenH - startY - bottomReserve) / itemH);

    int scrollOffset = 0;
    const int total = static_cast<int>(v.entries.size());
    if (total > visibleItems) {
      scrollOffset = v.selectedIndex - visibleItems / 2;
      if (scrollOffset < 0) scrollOffset = 0;
      if (scrollOffset + visibleItems > total) scrollOffset = total - visibleItems;
    }

    const int end = std::min(scrollOffset + visibleItems, total);
    for (int i = scrollOffset; i < end; i++) {
      const int y = startY + (i - scrollOffset) * itemH;
      menuItem(r, t, y, v.entries[i].displayName.c_str(), i == v.selectedIndex);
    }
  }

  buttonBar(r, t, "Back", "Select", "Up", "Down");
  r.displayBuffer();
}

// ============================================================================
// DictHistoryView — previously looked-up words
// ============================================================================

void render(const GfxRenderer& r, const Theme& t, const DictHistoryView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Lookup History");

  if (v.words.empty()) {
    centeredText(r, t, r.getScreenHeight() / 2, "No words looked up yet");
    buttonBar(r, t, "Back", "", "", "");
    r.displayBuffer();
    return;
  }

  // Word count in the top-right corner
  char countStr[32];
  snprintf(countStr, sizeof(countStr), "%d word%s",
           static_cast<int>(v.words.size()), v.words.size() == 1 ? "" : "s");
  r.drawText(t.smallFontId,
             r.getScreenWidth() - r.getTextWidth(t.smallFontId, countStr) - 10,
             t.screenMarginTop + 4, countStr, t.primaryTextBlack);

  const int startY = 60;
  const int itemH = t.menuItemHeight + t.itemSpacing;
  const int screenH = r.getScreenHeight();
  const int bottomReserve = 45;
  const int visibleItems = std::min(static_cast<int>(DictHistoryView::MAX_VISIBLE),
                                    std::max(1, (screenH - startY - bottomReserve) / itemH));
  const int end = std::min(v.scrollOffset + visibleItems, static_cast<int>(v.words.size()));

  for (int i = v.scrollOffset; i < end; i++) {
    const int y = startY + (i - v.scrollOffset) * itemH;
    menuItem(r, t, y, v.words[i].c_str(), i == v.selectedIndex);
  }

  // Scroll indicators (arrows at the right edge)
  const int arrowX = r.getScreenWidth() - 20;
  if (v.scrollOffset > 0) {
    // Up arrow
    r.drawLine(arrowX, startY - 4, arrowX - 6, startY + 6, t.primaryTextBlack);
    r.drawLine(arrowX, startY - 4, arrowX + 6, startY + 6, t.primaryTextBlack);
    r.drawLine(arrowX - 6, startY + 6, arrowX + 6, startY + 6, t.primaryTextBlack);
  }
  if (end < static_cast<int>(v.words.size())) {
    // Down arrow
    const int bottomY = startY + visibleItems * itemH + 4;
    r.drawLine(arrowX, bottomY + 10, arrowX - 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX, bottomY + 10, arrowX + 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX - 6, bottomY, arrowX + 6, bottomY, t.primaryTextBlack);
  }

  buttonBar(r, t, "Back", "Look up", "Delete", "");
  r.displayBuffer();
}

}  // namespace ui

#include "HomeView.h"

#include <CoverHelpers.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

namespace ui {

BookBlockLayout calculateBookBlockLayout(const GfxRenderer& r, const Theme& t) {
  const int headerBottom = 8 + r.getLineHeight(t.uiFontId) + 10;  // brand/battery row + gap
  constexpr int coverMaxW = 100;
  constexpr int coverMaxH = 150;
  const int coverX = t.screenMarginSide + 10;
  const int coverY = headerBottom + 8;

  // Reserve the full cover height for the block regardless of whether the
  // wrapped title+author actually needs that much — simpler than measuring
  // wrapped-text height, and 150px comfortably fits a 2-line title next to
  // the cover at UI font sizes.
  const int progressY = coverY + coverMaxH + 8;
  const int progressBottom = progressY + 16 + r.getLineHeight(t.smallFontId) + 5;

  return {coverX, coverY, coverMaxW, coverMaxH, progressBottom + 10};
}

void render(const GfxRenderer& r, const Theme& t, const HomeView& v) {
  r.clearScreen(t.backgroundColor);

  const int pageWidth = r.getScreenWidth();
  const int pageHeight = r.getScreenHeight();

  // "SUMI" brand - small bold, top-left with padding from screen edge
  r.drawText(t.uiFontId, 10, 8, "SUMI", t.primaryTextBlack, EpdFontFamily::BOLD);

  // Battery indicator - top right
  battery(r, t, pageWidth - 80, 10, v.batteryPercent);

  const auto layout = calculateBookBlockLayout(r, t);
  const int titleFontId = (v.hasBook && v.titleFontId >= 0) ? v.titleFontId : t.uiFontId;

  // === BOOK BLOCK (cover + title/author beside it, matches CrossPoint's
  // "last book read on top" layout — see docs/HOME_LAYOUT.md) ===
  if (v.hasBook) {
    const bool hasCover = v.coverData != nullptr || v.hasCoverBmp;
    if (v.coverData != nullptr && v.coverWidth > 0 && v.coverHeight > 0) {
      const auto rect = CoverHelpers::calculateCenteredRect(v.coverWidth, v.coverHeight, layout.coverX, layout.coverY,
                                                            layout.coverMaxW, layout.coverMaxH);
      r.drawImage(v.coverData, rect.x, rect.y, v.coverWidth, v.coverHeight);
    } else if (!hasCover) {
      bookPlaceholder(r, t, layout.coverX, layout.coverY, layout.coverMaxW, layout.coverMaxH);
    }
    // hasCoverBmp: HomeState draws the SD-loaded cover on top after this
    // call returns, at the same coverX/coverY/coverMaxW/coverMaxH rect.

    const int textX = layout.coverX + layout.coverMaxW + 15;
    const int maxTextW = pageWidth - textX - t.screenMarginSide - 10;
    const int titleLineHeight = r.getLineHeight(titleFontId);
    int textY = layout.coverY + 4;
    const auto titleLines = r.wrapTextWithHyphenation(titleFontId, v.bookTitle, maxTextW, 3, EpdFontFamily::BOLD);
    for (const auto& line : titleLines) {
      r.drawText(titleFontId, textX, textY, line.c_str(), t.primaryTextBlack, EpdFontFamily::BOLD);
      textY += titleLineHeight;
    }
    if (v.bookAuthor[0] != '\0') {
      textY += titleLineHeight / 4;
      const auto trunc = r.truncatedText(titleFontId, v.bookAuthor, maxTextW);
      r.drawText(titleFontId, textX, textY, trunc.c_str(), t.secondaryTextBlack);
    }

    if (v.bookProgress >= 0) {
      const int barY = layout.coverY + layout.coverMaxH + 8;
      progress(r, t, barY, v.bookProgress, 100);
    }
  } else {
    const int centerY = layout.coverY + layout.coverMaxH / 2;
    const char* noBookText = _tr(HOME_NO_BOOK_OPEN);
    const int nbw = r.getTextWidth(t.uiFontId, noBookText);
    r.drawText(t.uiFontId, (pageWidth - nbw) / 2, centerY, noBookText, t.secondaryTextBlack);
  }

  r.drawLine(t.screenMarginSide, layout.menuStartY - 8, pageWidth - t.screenMarginSide, layout.menuStartY - 8,
             t.primaryTextBlack);

  // === MENU LIST (arrow-navigable, directly below the book block) ===
  const int rowH = t.menuItemHeight + t.itemSpacing;
  int y = layout.menuStartY;
  for (int i = 0; i < v.menuItemCount; i++) {
    menuItem(r, t, y, v.menuItems[i].label, i == v.selection);
    y += rowH;
  }

  // === BOTTOM BUTTON HINTS ===
  {
    constexpr int hintMargin = 12;
    const int hintY = pageHeight - 18;
    const char* leftHint  = _tr(HOME_FILES);
    const char* rightHint = _tr(HOME_MENU);
    r.drawText(t.smallFontId, hintMargin, hintY, leftHint, t.secondaryTextBlack);
    const int rw = r.getTextWidth(t.smallFontId, rightHint);
    r.drawText(t.smallFontId, pageWidth - hintMargin - rw, hintY, rightHint, t.secondaryTextBlack);
  }

  // Note: displayBuffer() is NOT called here; HomeState will call it
  // after rendering the cover image on top of the card area
}

void render(const GfxRenderer& r, const Theme& t, const FileListView& v) {
  r.clearScreen(t.backgroundColor);

  // Title with path
  title(r, t, t.screenMarginTop, "Files");

  // Current path (truncated if needed)
  const int pathY = 40;
  const int maxPathW = r.getScreenWidth() - 2 * t.screenMarginSide - 16;
  const auto truncPath = r.truncatedText(t.smallFontId, v.currentPath, maxPathW);
  r.drawText(t.smallFontId, t.screenMarginSide + 8, pathY, truncPath.c_str(), t.secondaryTextBlack);

  // File list
  const int listStartY = 65;
  const int pageStart = v.getPageStart();
  const int pageEnd = v.getPageEnd();

  for (int i = pageStart; i < pageEnd; i++) {
    const int y = listStartY + (i - pageStart) * (t.itemHeight + t.itemSpacing);
    fileEntry(r, t, y, v.files[i].name, v.files[i].isDirectory, i == v.selected);
  }

  // Page indicator
  if (v.getPageCount() > 1) {
    char pageStr[16];
    snprintf(pageStr, sizeof(pageStr), "%d/%d", v.page + 1, v.getPageCount());
    const int pageY = r.getScreenHeight() - 50;
    centeredText(r, t, pageY, pageStr);
  }

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, ChapterListView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Chapters");

  constexpr int listStartY = 60;
  const int availableHeight = r.getScreenHeight() - listStartY - 50;
  const int itemHeight = t.itemHeight + t.itemSpacing;
  const int visibleCount = availableHeight / itemHeight;

  v.ensureVisible(visibleCount);

  const int end = std::min(v.scrollOffset + visibleCount, static_cast<int>(v.chapterCount));
  for (int i = v.scrollOffset; i < end; i++) {
    const int y = listStartY + (i - v.scrollOffset) * itemHeight;
    chapterItem(r, t, t.uiFontId, y, v.chapters[i].title, v.chapters[i].depth, i == v.selected, i == v.currentChapter);
  }

  r.displayBuffer();
}

}  // namespace ui

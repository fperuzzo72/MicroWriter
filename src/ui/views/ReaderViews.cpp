#include "ReaderViews.h"

#include <SumiClock.h>
#include <cstdio>

namespace ui {

// Static definitions
constexpr const char* const ReaderMenuView::ITEMS[];

void renderStatusBar(const GfxRenderer& r, const Theme& t, const ReaderStatusView& v) {
  // Draw status bar at bottom of screen
  statusBar(r, t, v.currentPage, v.totalPages, v.progressPercent);

  // Clock display in the center of the status bar when time is available
  if (sumi::SumiClock::hasTime()) {
    char timeStr[12];
    sumi::SumiClock::getTimeStr(timeStr, sizeof(timeStr));
    if (timeStr[0] != '\0') {
      const int y = r.getScreenHeight() - 25;
      const int tw = r.getTextWidth(t.smallFontId, timeStr);
      const int cx = (r.getScreenWidth() - tw) / 2;
      r.drawText(t.smallFontId, cx, y, timeStr, t.primaryTextBlack);
    }
  }
}

void render(const GfxRenderer& r, const Theme& t, const CoverPageView& v) {
  r.clearScreen(t.backgroundColor);

  const int screenW = r.getScreenWidth();
  const int screenH = r.getScreenHeight();

  // Draw cover image centered in upper portion
  if (v.coverData != nullptr) {
    // Calculate scaled size to fit screen while maintaining aspect ratio
    const int maxW = screenW - 40;
    const int maxH = screenH - 200;  // Leave room for title/author

    int drawW = v.coverWidth;
    int drawH = v.coverHeight;

    if (drawW > maxW || drawH > maxH) {
      const float scaleW = static_cast<float>(maxW) / drawW;
      const float scaleH = static_cast<float>(maxH) / drawH;
      const float scale = (scaleW < scaleH) ? scaleW : scaleH;
      drawW = static_cast<int>(drawW * scale);
      drawH = static_cast<int>(drawH * scale);
    }

    const int coverX = (screenW - drawW) / 2;
    const int coverY = 20;
    image(r, coverX, coverY, v.coverData, drawW, drawH);
  }

  // Title below cover
  const int titleY = screenH - 120;
  const int maxTitleW = screenW - 40;

  if (v.title[0] != '\0') {
    // Wrap title if needed
    const auto titleLines = r.wrapTextWithHyphenation(t.readerFontId, v.title, maxTitleW, 2, EpdFontFamily::BOLD);
    int lineY = titleY;
    const int lineHeight = r.getLineHeight(t.readerFontId);

    for (const auto& line : titleLines) {
      r.drawCenteredText(t.readerFontId, lineY, line.c_str(), t.primaryTextBlack, EpdFontFamily::BOLD);
      lineY += lineHeight;
    }
  }

  // Author below title
  if (v.author[0] != '\0') {
    const int authorY = screenH - 50;
    r.drawCenteredText(t.uiFontId, authorY, v.author, t.secondaryTextBlack);
  }

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ReaderMenuView& v) {
  if (!v.visible) return;

  const int screenW = r.getScreenWidth();
  const int screenH = r.getScreenHeight();

  // Menu overlay box
  const int menuW = 200;
  const int menuH = ReaderMenuView::ITEM_COUNT * (t.itemHeight + 5) + 30;
  const int menuX = (screenW - menuW) / 2;
  const int menuY = (screenH - menuH) / 2;

  // Draw menu background with border
  r.clearArea(menuX, menuY, menuW, menuH, t.backgroundColor);
  r.drawRect(menuX, menuY, menuW, menuH, t.primaryTextBlack);

  // Menu title
  r.drawCenteredText(t.uiFontId, menuY + 10, "Menu", t.primaryTextBlack, EpdFontFamily::BOLD);

  // Menu items
  const int itemStartY = menuY + 40;
  for (int i = 0; i < ReaderMenuView::ITEM_COUNT; i++) {
    const int itemY = itemStartY + i * (t.itemHeight + 5);
    const int itemX = menuX + 10;
    const int itemW = menuW - 20;

    if (i == v.selected) {
      r.fillRect(itemX, itemY, itemW, t.itemHeight, t.selectionFillBlack);
      r.drawCenteredText(t.uiFontId, itemY + 5, ReaderMenuView::ITEMS[i], t.selectionTextBlack);
    } else {
      r.drawCenteredText(t.uiFontId, itemY + 5, ReaderMenuView::ITEMS[i], t.primaryTextBlack);
    }
  }

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const JumpToPageView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Go to Page");

  const int centerY = r.getScreenHeight() / 2 - 40;

  // Current page number (large)
  char pageStr[16];
  snprintf(pageStr, sizeof(pageStr), "%d", v.targetPage);
  r.drawCenteredText(t.readerFontIdLarge, centerY, pageStr, t.primaryTextBlack, EpdFontFamily::BOLD);

  // Range info
  char rangeStr[32];
  snprintf(rangeStr, sizeof(rangeStr), "of %d", v.maxPage);
  centeredText(r, t, centerY + 50, rangeStr);


  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const BookmarkListView& v) {
  r.clearScreen(t.backgroundColor);
  title(r, t, t.screenMarginTop, "Bookmarks");

  if (v.pages.empty()) {
    r.drawCenteredText(t.smallFontId, r.getScreenHeight() / 2,
                       "No bookmarks yet", t.primaryTextBlack);
    r.drawCenteredText(t.smallFontId, r.getScreenHeight() / 2 + 30,
                       "Use reader settings to add one", t.primaryTextBlack);
    ButtonBar bar{"Back", "", "", ""};
    buttonBar(r, t, bar);
    r.displayBuffer();
    return;
  }

  // Bookmark count in top-right
  char countStr[32];
  snprintf(countStr, sizeof(countStr), "%d bookmark%s", (int)v.pages.size(), v.pages.size() == 1 ? "" : "s");
  r.drawText(t.smallFontId, r.getScreenWidth() - r.getTextWidth(t.smallFontId, countStr) - 10,
             t.screenMarginTop + 4, countStr, t.primaryTextBlack);

  const int startY = 60;
  const int itemH = t.menuItemHeight + t.itemSpacing;
  const int screenH = r.getScreenHeight();
  const int availableH = screenH - startY - 45;
  const int visibleItems = std::min(static_cast<int>(BookmarkListView::MAX_VISIBLE),
                                    std::max(1, availableH / itemH));
  const int end = std::min(v.scrollOffset + visibleItems, static_cast<int>(v.pages.size()));

  for (int i = v.scrollOffset; i < end; i++) {
    const int y = startY + (i - v.scrollOffset) * itemH;
    bool selected = (i == v.selectedIndex);

    if (selected) {
      r.fillRect(0, y - 2, r.getScreenWidth(), itemH, !t.primaryTextBlack);
    }

    char label[48];
    snprintf(label, sizeof(label), "Page %u of %d", v.pages[i], v.totalBookPages);
    r.drawText(t.uiFontId, 20, y, label, selected ? !t.primaryTextBlack : t.primaryTextBlack);
  }

  // Scroll indicators
  const int pageW = r.getScreenWidth();
  const int arrowX = pageW - 20;
  if (v.scrollOffset > 0) {
    r.drawLine(arrowX, startY - 4, arrowX - 6, startY + 6, t.primaryTextBlack);
    r.drawLine(arrowX, startY - 4, arrowX + 6, startY + 6, t.primaryTextBlack);
    r.drawLine(arrowX - 6, startY + 6, arrowX + 6, startY + 6, t.primaryTextBlack);
  }
  if (end < static_cast<int>(v.pages.size())) {
    const int bottomY = startY + visibleItems * itemH + 4;
    r.drawLine(arrowX, bottomY + 10, arrowX - 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX, bottomY + 10, arrowX + 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX - 6, bottomY, arrowX + 6, bottomY, t.primaryTextBlack);
  }

  ButtonBar bar{"Back", "Go to", "Delete", ""};
  buttonBar(r, t, bar);
  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const GlobalBookmarkListView& v) {
  r.clearScreen(t.backgroundColor);
  title(r, t, t.screenMarginTop, "All Bookmarks");

  if (v.entries.empty()) {
    r.drawCenteredText(t.smallFontId, r.getScreenHeight() / 2,
                       "No bookmarks across books", t.primaryTextBlack);
    r.drawCenteredText(t.smallFontId, r.getScreenHeight() / 2 + 30,
                       "Bookmarks appear here automatically", t.primaryTextBlack);
    ButtonBar bar{"Back", "", "", ""};
    buttonBar(r, t, bar);
    r.displayBuffer();
    return;
  }

  // Entry count in top-right
  char countStr[32];
  snprintf(countStr, sizeof(countStr), "%d total", (int)v.entries.size());
  r.drawText(t.smallFontId, r.getScreenWidth() - r.getTextWidth(t.smallFontId, countStr) - 10,
             t.screenMarginTop + 4, countStr, t.primaryTextBlack);

  const int startY = 60;
  const int itemH = t.menuItemHeight + t.itemSpacing;
  const int screenH = r.getScreenHeight();
  const int availableH = screenH - startY - 45;
  const int visibleItems = std::min(static_cast<int>(GlobalBookmarkListView::MAX_VISIBLE),
                                    std::max(1, availableH / itemH));
  const int end = std::min(v.scrollOffset + visibleItems, static_cast<int>(v.entries.size()));

  for (int i = v.scrollOffset; i < end; i++) {
    const int y = startY + (i - v.scrollOffset) * itemH;
    bool selected = (i == v.selectedIndex);

    if (selected) {
      r.fillRect(0, y - 2, r.getScreenWidth(), itemH, !t.primaryTextBlack);
    }

    const bool color = selected ? !t.primaryTextBlack : t.primaryTextBlack;

    // Show book title and page number
    char label[96];
    if (v.entries[i].bookTitle[0] != '\0') {
      snprintf(label, sizeof(label), "%.40s  p.%u", v.entries[i].bookTitle, v.entries[i].page);
    } else {
      snprintf(label, sizeof(label), "Page %u", v.entries[i].page);
    }
    r.drawText(t.uiFontId, 20, y, label, color);
  }

  // Scroll indicators
  const int pageW = r.getScreenWidth();
  const int arrowX = pageW - 20;
  if (v.scrollOffset > 0) {
    r.drawLine(arrowX, startY - 4, arrowX - 6, startY + 6, t.primaryTextBlack);
    r.drawLine(arrowX, startY - 4, arrowX + 6, startY + 6, t.primaryTextBlack);
    r.drawLine(arrowX - 6, startY + 6, arrowX + 6, startY + 6, t.primaryTextBlack);
  }
  if (end < static_cast<int>(v.entries.size())) {
    const int bottomY = startY + visibleItems * itemH + 4;
    r.drawLine(arrowX, bottomY + 10, arrowX - 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX, bottomY + 10, arrowX + 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX - 6, bottomY, arrowX + 6, bottomY, t.primaryTextBlack);
  }

  ButtonBar bar{"Back", "", "", ""};
  buttonBar(r, t, bar);
  r.displayBuffer();
}

}  // namespace ui

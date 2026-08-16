#include "SettingsViews.h"
#include <I18n.h>
#include <SDCardManager.h>

namespace ui {

// Static definitions for constexpr arrays
constexpr const char* const SettingsMenuView::ITEMS[];
constexpr const char* const CleanupMenuView::ITEMS[];

// ReaderSettingsView static definitions
constexpr const char* const ReaderSettingsView::FONT_SIZE_VALUES[];
constexpr const char* const ReaderSettingsView::TEXT_LAYOUT_VALUES[];
constexpr const char* const ReaderSettingsView::LINE_SPACING_VALUES[];
constexpr const char* const ReaderSettingsView::ALIGNMENT_VALUES[];
constexpr const char* const ReaderSettingsView::IMAGE_DISPLAY_VALUES[];
constexpr const char* const ReaderSettingsView::STATUS_BAR_VALUES[];
constexpr const char* const ReaderSettingsView::ORIENTATION_VALUES[];

const ReaderSettingsView::SettingDef ReaderSettingsView::DEFS[SETTING_COUNT] = {
    {"Theme", SettingType::ThemeSelect, nullptr, 0},
    {"Font", SettingType::FontSelect, nullptr, 0},
    {"Font Size", SettingType::Enum, FONT_SIZE_VALUES, 4},
    {"Text Layout", SettingType::Enum, TEXT_LAYOUT_VALUES, 3},
    {"Line Spacing", SettingType::Enum, LINE_SPACING_VALUES, 4},
    {"Text Anti-Aliasing", SettingType::Toggle, nullptr, 0},
    {"Paragraph Alignment", SettingType::Enum, ALIGNMENT_VALUES, 5},
    {"Hyphenation", SettingType::Toggle, nullptr, 0},
    {"Show Images", SettingType::Enum, IMAGE_DISPLAY_VALUES, 3},
    {"Show Tables", SettingType::Toggle, nullptr, 0},
    {"Status Bar", SettingType::Enum, STATUS_BAR_VALUES, 2},
    {"Reading Orientation", SettingType::Enum, ORIENTATION_VALUES, 4},
};

// DeviceSettingsView static definitions
constexpr const char* const DeviceSettingsView::SLEEP_TIMEOUT_VALUES[];
constexpr const char* const DeviceSettingsView::SLEEP_SCREEN_VALUES[];
constexpr const char* const DeviceSettingsView::STARTUP_VALUES[];
constexpr const char* const DeviceSettingsView::SHORT_PWR_VALUES[];
constexpr const char* const DeviceSettingsView::PAGES_REFRESH_VALUES[];
constexpr const char* const DeviceSettingsView::TOGGLE_VALUES[];
constexpr const char* const DeviceSettingsView::FRONT_BUTTON_VALUES[];
constexpr const char* const DeviceSettingsView::SIDE_BUTTON_VALUES[];

// InReaderSettingsView static definitions
// Reuses the same value string arrays as ReaderSettingsView
static constexpr const char* const TEXT_DARKNESS_VALUES[] = {"Normal", "Dark", "Extra Dark", "Maximum"};

const InReaderSettingsView::SettingDef InReaderSettingsView::DEFS[SETTING_COUNT] = {
    // Dictionary lookup at the top — highest-frequency reader action.
    // One long-press + Confirm gets you word-select on the current page.
    // validForXtc defaults to true; reflow-related entries below set it
    // false so XTC books (pre-laid-out, font/size/spacing baked in) hide
    // controls that would silently do nothing.
    {"Look up Word", SettingType::Action, nullptr, 0, true},
    {"Lookup History", SettingType::Action, nullptr, 0, true},
    {"Toggle Bookmark", SettingType::Action, nullptr, 0, true},
    {"View Bookmarks", SettingType::Action, nullptr, 0, true},
    {"All Bookmarks", SettingType::Action, nullptr, 0, true},
    {"Font", SettingType::FontSelect, nullptr, 0, false},
    {"Font Size", SettingType::Enum, ReaderSettingsView::FONT_SIZE_VALUES, 4, false},
    {"Text Layout", SettingType::Enum, ReaderSettingsView::TEXT_LAYOUT_VALUES, 3, false},
    {"Line Spacing", SettingType::Enum, ReaderSettingsView::LINE_SPACING_VALUES, 4, false},
    {"Alignment", SettingType::Enum, ReaderSettingsView::ALIGNMENT_VALUES, 5, false},
    {"Hyphenation", SettingType::Toggle, nullptr, 0, false},
    {"Anti-Aliasing", SettingType::Toggle, nullptr, 0, true},
    {"Show Images", SettingType::Enum, ReaderSettingsView::IMAGE_DISPLAY_VALUES, 3, false},
    {"Status Bar", SettingType::Enum, ReaderSettingsView::STATUS_BAR_VALUES, 2, true},
    {"Text Darkness", SettingType::Enum, TEXT_DARKNESS_VALUES, 4, true},
#if FEATURE_BLUETOOTH
    {"Bluetooth", SettingType::Action, nullptr, 0, true},
#endif
};

constexpr const char* const DeviceSettingsView::LANGUAGE_VALUES[];

const DeviceSettingsView::SettingDef DeviceSettingsView::DEFS[SETTING_COUNT] = {
    {"Auto Sleep Timeout", SLEEP_TIMEOUT_VALUES, 5}, {"Sleep Screen", SLEEP_SCREEN_VALUES, 5},
    {"Startup Behavior", STARTUP_VALUES, 2},         {"Short Power Button", SHORT_PWR_VALUES, 4},
    {"Pages Per Refresh", PAGES_REFRESH_VALUES, 6},  {"Sunlight Fading Fix", TOGGLE_VALUES, 2},
    {"Front Buttons", FRONT_BUTTON_VALUES, 2},       {"Side Buttons", SIDE_BUTTON_VALUES, 2},
    {"Language", LANGUAGE_VALUES, 13},
#if FEATURE_PLUGINS
    {"App Visibility", nullptr, 0},  // Sub-menu action: opens AppVisibility screen
#endif
};

// Render functions

// Map SettingsMenuView item indices to I18n StrIds for translation.
// The order must match the ITEMS[] array in SettingsViews.h.
static const sumi::StrId SETTINGS_MENU_STR_IDS[] = {
#if FEATURE_PLUGINS
  sumi::StrId::SETTINGS_APPS,
#endif
  sumi::StrId::SETTINGS_HOME_ART,
  sumi::StrId::SETTINGS_WIRELESS,
  sumi::StrId::SETTINGS_READER,
  sumi::StrId::SETTINGS_DEVICE,
#if FEATURE_BLUETOOTH
  sumi::StrId::SETTINGS_BLUETOOTH,
#endif
  sumi::StrId::SETTINGS_CLEANUP,
  sumi::StrId::SETTINGS_SYSTEM_INFO,
};

void render(const GfxRenderer& r, const Theme& t, const SettingsMenuView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, _tr(SETTINGS_TITLE));

  const int startY = 60;
  for (int i = 0; i < SettingsMenuView::ITEM_COUNT; i++) {
    const int y = startY + i * (t.menuItemHeight + t.itemSpacing);
    menuItem(r, t, y, sumi::I18n::instance().get(SETTINGS_MENU_STR_IDS[i]), i == v.selected);
  }


  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const CleanupMenuView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, _tr(SETTINGS_CLEANUP));

  const int startY = 60;
  for (int i = 0; i < CleanupMenuView::ITEM_COUNT; i++) {
    const int y = startY + i * (t.menuItemHeight + t.itemSpacing);
    menuItem(r, t, y, CleanupMenuView::ITEMS[i], i == v.selected);
  }


  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, HomeArtSettingsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, _tr(SETTINGS_HOME_ART));
  
  const int startY = 60;
  
  // Draw list items using standard menuItem style
  for (int i = 0; i < HomeArtSettingsView::VISIBLE_ITEMS && i + v.scrollOffset < v.themeCount; i++) {
    int themeIdx = v.scrollOffset + i;
    int itemY = startY + i * (t.menuItemHeight + t.itemSpacing);
    
    bool isSelected = (themeIdx == v.selectedIndex);
    bool isApplied = (themeIdx == v.appliedIndex);
    
    // Build display string with checkmark for applied theme. For the
    // applied case, snprintf %s already stops at a NUL (displayNames is
    // already a valid UTF-8 string because theme loader goes through
    // utf8SafeCopy); for the non-applied case, use utf8SafeCopy so a
    // very long CJK theme displayName isn't sliced mid-codepoint.
    char displayStr[48];
    if (isApplied) {
      snprintf(displayStr, sizeof(displayStr), "%s  *", v.displayNames[themeIdx]);
    } else {
      utf8SafeCopy(displayStr, v.displayNames[themeIdx], sizeof(displayStr));
    }
    
    menuItem(r, t, itemY, displayStr, isSelected);
  }

  r.displayBuffer();
}


void render(const GfxRenderer& r, const Theme& t, const SystemInfoView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, _tr(SETTINGS_SYSTEM_INFO));

  const int lineHeight = r.getLineHeight(t.menuFontId) + 14;
  const int startY = 60;

  for (int i = 0; i < v.fieldCount; i++) {
    const int y = startY + i * lineHeight;
    twoColumnRow(r, t, y, v.fields[i].label, v.fields[i].value);
  }


  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ReaderSettingsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, _tr(READER_SETTINGS_TITLE));

  const int startY = 60;
  const int itemH = t.menuItemHeight + t.itemSpacing;
  const int screenH = r.getScreenHeight();
  const int availableH = screenH - startY - 45;
  const int visibleItems = std::min(static_cast<int>(ReaderSettingsView::VISIBLE_ITEMS),
                                    std::max(1, availableH / itemH));
  const int end = std::min(v.scrollOffset + visibleItems,
                           static_cast<int>(ReaderSettingsView::SETTING_COUNT));
  for (int i = v.scrollOffset; i < end; i++) {
    const int y = startY + (i - v.scrollOffset) * itemH;
    const auto& def = ReaderSettingsView::DEFS[i];

    if (def.type == ReaderSettingsView::SettingType::Toggle) {
      toggle(r, t, y, def.label, v.values[i] != 0, i == v.selected);
    } else {
      enumValue(r, t, y, def.label, v.getCurrentValueStr(i), i == v.selected);
    }
  }

  // Scroll indicators when list overflows
  const int pageW = r.getScreenWidth();
  const int arrowX = pageW - 20;
  if (v.scrollOffset > 0) {
    r.drawLine(arrowX, startY - 4, arrowX - 6, startY + 6, t.primaryTextBlack);
    r.drawLine(arrowX, startY - 4, arrowX + 6, startY + 6, t.primaryTextBlack);
    r.drawLine(arrowX - 6, startY + 6, arrowX + 6, startY + 6, t.primaryTextBlack);
  }
  if (end < ReaderSettingsView::SETTING_COUNT) {
    const int bottomY = startY + visibleItems * itemH + 4;
    r.drawLine(arrowX, bottomY + 10, arrowX - 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX, bottomY + 10, arrowX + 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX - 6, bottomY, arrowX + 6, bottomY, t.primaryTextBlack);
  }

  buttonBar(r, t, v.buttons);
  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const DeviceSettingsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, _tr(SETTINGS_DEVICE));

  const int startY = 60;
  for (int i = 0; i < DeviceSettingsView::SETTING_COUNT; i++) {
    const int y = startY + i * (t.menuItemHeight + t.itemSpacing);
    enumValue(r, t, y, DeviceSettingsView::DEFS[i].label, v.getCurrentValueStr(i), i == v.selected);
  }


  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const InReaderSettingsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Reader Settings");

  const int startY = 60;
  const int itemH = t.menuItemHeight + t.itemSpacing;
  const int screenH = r.getScreenHeight();
  // Dynamically calculate visible items based on screen height
  // Reserve space for title (60px) and button bar (~40px)
  const int availableH = screenH - startY - 45;
  const int visibleItems = std::min(static_cast<int>(InReaderSettingsView::VISIBLE_ITEMS),
                                    std::max(1, availableH / itemH));
  // Iterate DEFS, skipping rows that aren't valid for the current
  // content type (XTC hides reflow controls). `slot` is the post-hiding
  // index used for both the scroll window and on-screen row position;
  // `i` is the DEFS index, which is what `selected` and `values[]`
  // are keyed by.
  int slot = 0;
  for (int i = 0; i < static_cast<int>(InReaderSettingsView::SETTING_COUNT); i++) {
    if (!v.isVisible(i)) continue;
    if (slot >= v.scrollOffset && slot < v.scrollOffset + visibleItems) {
      const int y = startY + (slot - v.scrollOffset) * itemH;
      const auto& def = InReaderSettingsView::DEFS[i];

      if (def.type == InReaderSettingsView::SettingType::Toggle) {
        toggle(r, t, y, def.label, v.values[i] != 0, i == v.selected);
      } else if (def.type == InReaderSettingsView::SettingType::Action) {
        // Action items render as plain menu rows (no right-side value
        // column). The "None" placeholder dictActionStatus was confusing
        // — Look up Word / Lookup History / Toggle Bookmark are pure
        // buttons, you press them, no setting attached. Bluetooth's
        // "Connect/Connected" status still has meaning, so for that
        // specific item show the status as a small suffix only.
        const char* status = v.getCurrentValueStr(i);
        if (status && status[0] != '\0' && strcmp(status, "None") != 0) {
          enumValue(r, t, y, def.label, status, i == v.selected);
        } else {
          menuItem(r, t, y, def.label, i == v.selected);
        }
      } else {
        enumValue(r, t, y, def.label, v.getCurrentValueStr(i), i == v.selected);
      }
    }
    slot++;
  }
  const int totalSlots = slot;

  // Scroll indicators when list overflows
  const int pageW = r.getScreenWidth();
  const int arrowX = pageW - 20;
  if (v.scrollOffset > 0) {
    // Up arrow at top-right: small triangle pointing up
    r.drawLine(arrowX, startY - 4, arrowX - 6, startY + 6, t.primaryTextBlack);
    r.drawLine(arrowX, startY - 4, arrowX + 6, startY + 6, t.primaryTextBlack);
    r.drawLine(arrowX - 6, startY + 6, arrowX + 6, startY + 6, t.primaryTextBlack);
  }
  if (v.scrollOffset + visibleItems < totalSlots) {
    // Down arrow at bottom-right: small triangle pointing down
    const int bottomY = startY + visibleItems * itemH + 4;
    r.drawLine(arrowX, bottomY + 10, arrowX - 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX, bottomY + 10, arrowX + 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX - 6, bottomY, arrowX + 6, bottomY, t.primaryTextBlack);
  }

  buttonBar(r, t, v.buttons);
  r.displayBuffer();
}

#if FEATURE_PLUGINS
void render(const GfxRenderer& r, const Theme& t, const AppVisibilityView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "App Visibility");

  const int startY = 60;
  const int itemH = t.menuItemHeight + t.itemSpacing;
  const int end = std::min(v.scrollOffset + static_cast<int>(AppVisibilityView::VISIBLE_ITEMS), v.appCount);

  for (int i = v.scrollOffset; i < end; i++) {
    const int y = startY + (i - v.scrollOffset) * itemH;
    toggle(r, t, y, v.appNames[i], v.visible[i], i == v.selected);
  }

  // Scroll indicators
  const int pageW = r.getScreenWidth();
  const int arrowX = pageW / 2;
  if (v.scrollOffset > 0) {
    r.drawLine(arrowX, startY - 6, arrowX - 6, startY - 1, t.primaryTextBlack);
    r.drawLine(arrowX, startY - 6, arrowX + 6, startY - 1, t.primaryTextBlack);
  }
  if (end < v.appCount) {
    const int bottomY = startY + AppVisibilityView::VISIBLE_ITEMS * itemH + 4;
    r.drawLine(arrowX, bottomY + 6, arrowX - 6, bottomY, t.primaryTextBlack);
    r.drawLine(arrowX, bottomY + 6, arrowX + 6, bottomY, t.primaryTextBlack);
  }

  buttonBar(r, t, v.buttons);
  r.displayBuffer();
}
#endif  // FEATURE_PLUGINS

void render(const GfxRenderer& r, const Theme& t, const FileActionMenuView& v) {
  const int pageWidth = r.getScreenWidth();
  const int pageHeight = r.getScreenHeight();
  const int lineHeight = r.getLineHeight(t.menuFontId);

  r.clearScreen(t.backgroundColor);

  // File label as bold title near the top of the popup region. Keep it
  // a single line — the menu is vertically the most-important content.
  const int titleY = pageHeight / 2 - lineHeight * 4;
  r.drawCenteredText(t.readerFontId, titleY, v.title, t.primaryTextBlack,
                     EpdFontFamily::BOLD);

  // 3 stacked menu rows, each centered. Selected row gets a filled bg.
  static const char* const LABELS[FileActionMenuView::ITEM_COUNT] = {
      "Index for faster reading",
      "Delete",
      "Cancel",
  };
  constexpr int rowHeight = 56;
  constexpr int rowWidth = 320;
  const int firstRowY = pageHeight / 2 - lineHeight;
  const int rowX = (pageWidth - rowWidth) / 2;

  for (int i = 0; i < FileActionMenuView::ITEM_COUNT; i++) {
    const int rowY = firstRowY + i * (rowHeight + 8);
    const bool selected = (v.selected == i);
    if (selected) {
      r.fillRect(rowX, rowY, rowWidth, rowHeight, t.selectionFillBlack);
    } else {
      r.drawRect(rowX, rowY, rowWidth, rowHeight, t.primaryTextBlack);
    }
    const bool textColor = selected ? t.selectionTextBlack : t.primaryTextBlack;
    const int textWidth = r.getTextWidth(t.menuFontId, LABELS[i]);
    const int textX = rowX + (rowWidth - textWidth) / 2;
    const int textY = rowY + (rowHeight - r.getFontAscenderSize(t.menuFontId)) / 2;
    r.drawText(t.menuFontId, textX, textY, LABELS[i], textColor);
  }

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ConfirmDialogView& v) {
  const int pageWidth = r.getScreenWidth();
  const int pageHeight = r.getScreenHeight();
  const int lineHeight = r.getLineHeight(t.menuFontId);
  const int top = (pageHeight - lineHeight * 3) / 2;

  r.clearScreen(t.backgroundColor);

  // Title (bold, centered)
  r.drawCenteredText(t.readerFontId, top - 50, v.title, t.primaryTextBlack, EpdFontFamily::BOLD);

  // Description lines
  r.drawCenteredText(t.menuFontId, top, v.line1, t.primaryTextBlack);
  if (v.line2[0] != '\0') {
    r.drawCenteredText(t.menuFontId, top + lineHeight + 4, v.line2, t.primaryTextBlack);
  }

  // Yes/No buttons
  const int buttonY = top + lineHeight * 3 + 10;
  constexpr int buttonWidth = 120;
  constexpr int buttonHeight = 48;
  constexpr int buttonSpacing = 30;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (pageWidth - totalWidth) / 2;

  const char* buttonLabels[] = {"Yes", "No"};
  const int buttonPositions[] = {startX, startX + buttonWidth + buttonSpacing};

  for (int i = 0; i < 2; i++) {
    const bool isSelected = (v.selection == i);
    const int btnX = buttonPositions[i];

    if (isSelected) {
      r.fillRect(btnX, buttonY, buttonWidth, buttonHeight, t.selectionFillBlack);
    } else {
      r.drawRect(btnX, buttonY, buttonWidth, buttonHeight, t.primaryTextBlack);
    }

    const bool textColor = isSelected ? t.selectionTextBlack : t.primaryTextBlack;
    const int textWidth = r.getTextWidth(t.menuFontId, buttonLabels[i]);
    const int textX = btnX + (buttonWidth - textWidth) / 2;
    const int textY = buttonY + (buttonHeight - r.getFontAscenderSize(t.menuFontId)) / 2;
    r.drawText(t.menuFontId, textX, textY, buttonLabels[i], textColor);
  }

  // Button hints

  r.displayBuffer();
}

}  // namespace ui

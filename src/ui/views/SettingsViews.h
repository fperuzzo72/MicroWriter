#pragma once

#include <GfxRenderer.h>
#include <Theme.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "../Elements.h"
#include "../../config.h"

namespace ui {

// ============================================================================
// SettingsMenuView - Main settings category selection
// ============================================================================

struct SettingsMenuView {
#if FEATURE_PLUGINS && FEATURE_BLUETOOTH
  static constexpr const char* const ITEMS[] = {"Apps", "Home Art", "Wireless Transfer", "Reader", "Device", "Bluetooth", "Cleanup", "System Info"};
  static constexpr int ITEM_COUNT = 8;
#elif FEATURE_PLUGINS
  static constexpr const char* const ITEMS[] = {"Apps", "Home Art", "Wireless Transfer", "Reader", "Device", "Cleanup", "System Info"};
  static constexpr int ITEM_COUNT = 7;
#elif FEATURE_BLUETOOTH
  static constexpr const char* const ITEMS[] = {"Home Art", "Wireless Transfer", "Reader", "Device", "Bluetooth", "Cleanup", "System Info"};
  static constexpr int ITEM_COUNT = 7;
#else
  static constexpr const char* const ITEMS[] = {"Home Art", "Wireless Transfer", "Reader", "Device", "Cleanup", "System Info"};
  static constexpr int ITEM_COUNT = 6;
#endif

  ButtonBar buttons{"Back", "Open", "", ""};
  int8_t selected = 0;
  bool needsRender = true;

  void moveUp() {
    selected = (selected == 0) ? ITEM_COUNT - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % ITEM_COUNT;
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const SettingsMenuView& v);

// ============================================================================
// CleanupMenuView - Storage cleanup options
// ============================================================================

struct CleanupMenuView {
#if FEATURE_BLUETOOTH
  static constexpr const char* const ITEMS[] = {"Clear Book Cache", "Forget Bluetooth Devices", "Clear Device Storage", "Factory Reset"};
  static constexpr int ITEM_COUNT = 4;
#else
  static constexpr const char* const ITEMS[] = {"Clear Book Cache", "Clear Device Storage", "Factory Reset"};
  static constexpr int ITEM_COUNT = 3;
#endif

  ButtonBar buttons{"Back", "Run", "", ""};
  int8_t selected = 0;
  bool needsRender = true;

  void moveUp() {
    selected = (selected == 0) ? ITEM_COUNT - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % ITEM_COUNT;
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const CleanupMenuView& v);

// ============================================================================
// HomeArtSettingsView - Home screen art theme selection (simple list)
// ============================================================================

struct HomeArtSettingsView {
  static constexpr int MAX_THEMES = 16;
  static constexpr int VISIBLE_ITEMS = 12;
  
  // Available themes
  char themeNames[MAX_THEMES][32] = {};
  char displayNames[MAX_THEMES][32] = {};
  int themeCount = 0;
  int selectedIndex = 0;
  int appliedIndex = 0;
  int scrollOffset = 0;
  bool needsRender = true;
  
  void moveUp() {
    if (selectedIndex > 0) {
      selectedIndex--;
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      }
      needsRender = true;
    }
  }
  
  void moveDown() {
    if (selectedIndex < themeCount - 1) {
      selectedIndex++;
      if (selectedIndex >= scrollOffset + VISIBLE_ITEMS) {
        scrollOffset = selectedIndex - VISIBLE_ITEMS + 1;
      }
      needsRender = true;
    }
  }
  
  const char* getCurrentThemeName() const {
    if (themeCount > 0 && selectedIndex < themeCount) {
      return themeNames[selectedIndex];
    }
    return "default";
  }
  
  const char* getCurrentDisplayName() const {
    if (themeCount > 0 && selectedIndex < themeCount) {
      return displayNames[selectedIndex];
    }
    return "Default";
  }
  
  const char* getAppliedThemeName() const {
    if (themeCount > 0 && appliedIndex < themeCount) {
      return themeNames[appliedIndex];
    }
    return "default";
  }
};

void render(const GfxRenderer& r, const Theme& t, HomeArtSettingsView& v);

// ============================================================================
// SystemInfoView - Device information display
// ============================================================================

struct SystemInfoView {
  static constexpr int MAX_VALUE_LEN = 32;

  struct InfoField {
    char label[24];
    char value[MAX_VALUE_LEN];
  };

  static constexpr int MAX_FIELDS = 10;
  ButtonBar buttons{"Back", "", "", ""};
  InfoField fields[MAX_FIELDS] = {};
  uint8_t fieldCount = 0;
  bool needsRender = true;

  void clear() {
    fieldCount = 0;
    needsRender = true;
  }

  void addField(const char* label, const char* value) {
    if (fieldCount < MAX_FIELDS) {
      utf8SafeCopy(fields[fieldCount].label, label, sizeof(InfoField::label));
      utf8SafeCopy(fields[fieldCount].value, value, MAX_VALUE_LEN);
      fieldCount++;
      needsRender = true;
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, const SystemInfoView& v);

// ============================================================================
// ReaderSettingsView - Reader configuration
// ============================================================================

struct ReaderSettingsView {
  // Setting types
  enum class SettingType : uint8_t { Toggle, Enum, ThemeSelect, FontSelect };

  struct SettingDef {
    const char* label;
    SettingType type;
    const char* const* enumValues;
    uint8_t enumCount;
  };

  // Static setting definitions
  static constexpr const char* const FONT_SIZE_VALUES[] = {"XSmall", "Small", "Normal", "Large"};
  static constexpr const char* const TEXT_LAYOUT_VALUES[] = {"Compact", "Standard", "Large"};
  static constexpr const char* const LINE_SPACING_VALUES[] = {"Compact", "Normal", "Relaxed", "Large"};
  static constexpr const char* const ALIGNMENT_VALUES[] = {"Justified", "Left", "Center", "Right", "Book's Style"};
  static constexpr const char* const IMAGE_DISPLAY_VALUES[] = {"Off", "Show", "Placeholder"};
  static constexpr const char* const STATUS_BAR_VALUES[] = {"None", "Show"};
  static constexpr const char* const ORIENTATION_VALUES[] = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};

  static constexpr int SETTING_COUNT = 12;
  static constexpr int MAX_THEMES = 16;
  static constexpr int MAX_FONTS = 16;
  static constexpr int VISIBLE_ITEMS = 10;  // 800px screen: (800 - 60 header - 45 footer) / 68px per item = 10
  static const SettingDef DEFS[SETTING_COUNT];

  ButtonBar buttons{"Back", "", "<", ">"};

  // Theme selection state (loaded from ThemeManager)
  char themeNames[MAX_THEMES][32] = {};
  int themeCount = 0;
  int currentThemeIndex = 0;

  // Font selection state (loaded from FontManager)
  char fontNames[MAX_FONTS][32] = {};
  int fontCount = 0;
  int currentFontIndex = 0;

  // Current values (indices for enums, bool for toggles)
  uint8_t values[SETTING_COUNT] = {0};
  int8_t selected = 0;
  int8_t scrollOffset = 0;
  bool needsRender = true;

  void moveUp() {
    selected = (selected == 0) ? SETTING_COUNT - 1 : selected - 1;
    ensureVisible();
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % SETTING_COUNT;
    ensureVisible();
    needsRender = true;
  }

  void cycleValue(int delta) {
    const auto& def = DEFS[selected];
    if (def.type == SettingType::Toggle) {
      values[selected] = values[selected] ? 0 : 1;
    } else if (def.type == SettingType::ThemeSelect) {
      if (themeCount > 0) {
        currentThemeIndex = (currentThemeIndex + themeCount + delta) % themeCount;
      }
    } else if (def.type == SettingType::FontSelect) {
      if (fontCount > 0) {
        currentFontIndex = (currentFontIndex + fontCount + delta) % fontCount;
      }
    } else {
      values[selected] = static_cast<uint8_t>((values[selected] + def.enumCount + delta) % def.enumCount);
    }
    needsRender = true;
  }

  const char* getCurrentValueStr(int index) const {
    const auto& def = DEFS[index];
    if (def.type == SettingType::Toggle) {
      return values[index] ? "ON" : "OFF";
    }
    if (def.type == SettingType::ThemeSelect) {
      if (themeCount > 0 && currentThemeIndex < themeCount) {
        return themeNames[currentThemeIndex];
      }
      return "light";
    }
    if (def.type == SettingType::FontSelect) {
      if (fontCount > 0 && currentFontIndex < fontCount) {
        return fontNames[currentFontIndex];
      }
      return "Default";
    }
    // Bounds check to prevent array out-of-bounds access from corrupted settings
    if (def.enumCount == 0 || values[index] >= def.enumCount) {
      return def.enumCount > 0 ? def.enumValues[0] : "???";
    }
    return def.enumValues[values[index]];
  }

  const char* getCurrentThemeName() const {
    if (themeCount > 0 && currentThemeIndex < themeCount) {
      return themeNames[currentThemeIndex];
    }
    return "light";
  }

  // Returns font family name for storage, or "" for default/builtin
  const char* getCurrentFontName() const {
    if (fontCount > 0 && currentFontIndex > 0 && currentFontIndex < fontCount) {
      return fontNames[currentFontIndex];
    }
    return "";
  }

  void ensureVisible() {
    if (selected < scrollOffset) scrollOffset = selected;
    if (selected >= scrollOffset + VISIBLE_ITEMS) scrollOffset = selected - VISIBLE_ITEMS + 1;
  }
};

void render(const GfxRenderer& r, const Theme& t, const ReaderSettingsView& v);

// ============================================================================
// InReaderSettingsView - Lightweight settings overlay for use inside the reader
// Subset of reader settings that can be changed without exiting the book.
// Excludes Theme and Orientation (require re-cache/restart).
// ============================================================================

struct InReaderSettingsView {
  enum class SettingType : uint8_t { Toggle, Enum, FontSelect, Action };

  struct SettingDef {
    const char* label;
    SettingType type;
    const char* const* enumValues;
    uint8_t enumCount;
    // True for settings that have any effect on a pre-rendered XTC book.
    // Reflow-time choices (font, size, line spacing, hyphenation, etc.)
    // are baked into the XTC at process time and ignored at read time —
    // those get validForXtc=false so the in-reader settings overlay
    // doesn't show menu entries that silently do nothing. Initialized
    // explicitly per-entry in the DEFS table (no default member
    // initializer here, to keep this an aggregate under pre-C++17
    // toolchains).
    bool validForXtc;
  };

#if FEATURE_BLUETOOTH
  static constexpr int SETTING_COUNT = 16;
#else
  static constexpr int SETTING_COUNT = 15;
#endif
  static constexpr int MAX_FONTS = 16;
  static constexpr int VISIBLE_ITEMS = 10;
  static const SettingDef DEFS[SETTING_COUNT];

  ButtonBar buttons{"Back", "", "<", ">"};

  // Font selection state (loaded from FontManager)
  char fontNames[MAX_FONTS][32] = {};
  int fontCount = 0;
  int currentFontIndex = 0;

  uint8_t values[SETTING_COUNT] = {0};
  int8_t selected = 0;
  int8_t scrollOffset = 0;
  bool needsRender = true;
  // Set by ReaderState when populating the view, from
  // core.content.metadata().type. True iff the current book is XTC
  // (pre-rendered). Filters DEFS through validForXtc on render + nav.
  bool isXtc = false;

  bool isVisible(int defsIdx) const {
    if (defsIdx < 0 || defsIdx >= SETTING_COUNT) return false;
    return !isXtc || DEFS[defsIdx].validForXtc;
  }

  int visibleCount() const {
    int n = 0;
    for (int i = 0; i < SETTING_COUNT; i++) if (isVisible(i)) n++;
    return n;
  }

  int slotOf(int defsIdx) const {
    int n = 0;
    for (int i = 0; i < defsIdx; i++) if (isVisible(i)) n++;
    return n;
  }

  void moveUp() {
    int next = selected;
    for (int step = 0; step < SETTING_COUNT; step++) {
      next = (next == 0) ? SETTING_COUNT - 1 : next - 1;
      if (isVisible(next)) break;
    }
    selected = next;
    ensureVisible();
    needsRender = true;
  }

  void moveDown() {
    int next = selected;
    for (int step = 0; step < SETTING_COUNT; step++) {
      next = (next + 1) % SETTING_COUNT;
      if (isVisible(next)) break;
    }
    selected = next;
    ensureVisible();
    needsRender = true;
  }

  void cycleValue(int delta) {
    const auto& def = DEFS[selected];
    if (def.type == SettingType::Action) {
      return;  // Actions are handled externally
    } else if (def.type == SettingType::Toggle) {
      values[selected] = values[selected] ? 0 : 1;
    } else if (def.type == SettingType::FontSelect) {
      if (fontCount > 0) {
        currentFontIndex = (currentFontIndex + fontCount + delta) % fontCount;
      }
    } else {
      values[selected] = static_cast<uint8_t>((values[selected] + def.enumCount + delta) % def.enumCount);
    }
    needsRender = true;
  }

  // Action status text (set externally by reader state). Legacy single
  // slot used by the Bluetooth action; the dictionary action uses a
  // separate slot so the two don't stomp on each other's display.
  char actionStatus[24] = "Connect";
  char dictActionStatus[24] = "None";
  char historyActionStatus[24] = "";
  char bookmarkToggleStatus[24] = "";
  char bookmarkListStatus[24] = "";
  char globalBookmarkStatus[24] = "";

  const char* getCurrentValueStr(int index) const {
    const auto& def = DEFS[index];
    if (def.type == SettingType::Toggle) {
      return values[index] ? "ON" : "OFF";
    }
    if (def.type == SettingType::FontSelect) {
      if (fontCount > 0 && currentFontIndex < fontCount) {
        return fontNames[currentFontIndex];
      }
      return "Default";
    }
    if (def.type == SettingType::Action) {
      // Dictionary lookup shows the active dictionary's display name so the
      // user sees what will be searched; Bluetooth shows its connection
      // state. Both are set externally by ReaderState in loadInReaderSettings.
      if (def.label != nullptr && def.label[0] == 'L') {
        // Distinguish "Look up Word" (label[7]=='W') from "Lookup History" (label[7]=='H')
        if (def.label[7] == 'H') return historyActionStatus;
        return dictActionStatus;
      }
      if (def.label != nullptr && def.label[0] == 'T') return bookmarkToggleStatus;
      if (def.label != nullptr && def.label[0] == 'V') return bookmarkListStatus;
      if (def.label != nullptr && def.label[0] == 'A') return globalBookmarkStatus;
      return actionStatus;
    }
    if (def.enumCount == 0 || values[index] >= def.enumCount) {
      return def.enumCount > 0 ? def.enumValues[0] : "???";
    }
    return def.enumValues[values[index]];
  }

  // Returns font family name for storage, or "" for default/builtin
  const char* getCurrentFontName() const {
    if (fontCount > 0 && currentFontIndex > 0 && currentFontIndex < fontCount) {
      return fontNames[currentFontIndex];
    }
    return "";
  }

  void ensureVisible() {
    // scrollOffset is a *slot* index (post-hiding), not a DEFS index. The
    // selected DEFS index gets mapped to its visible slot before clamping
    // so the row math matches what render() actually draws.
    const int slot = slotOf(selected);
    if (slot < scrollOffset) scrollOffset = slot;
    if (slot >= scrollOffset + VISIBLE_ITEMS) scrollOffset = slot - VISIBLE_ITEMS + 1;
  }
};

void render(const GfxRenderer& r, const Theme& t, const InReaderSettingsView& v);

// ============================================================================
// DeviceSettingsView - Device configuration
// ============================================================================

struct DeviceSettingsView {
  static constexpr const char* const SLEEP_TIMEOUT_VALUES[] = {"5 min", "10 min", "15 min", "30 min", "Never"};
  static constexpr const char* const SLEEP_SCREEN_VALUES[] = {"Dark", "Light", "Custom", "Cover", "Last Page"};
  static constexpr const char* const STARTUP_VALUES[] = {"Last Document", "Home"};
  static constexpr const char* const SHORT_PWR_VALUES[] = {"Ignore", "Sleep", "Page Turn", "Refresh"};
  static constexpr const char* const PAGES_REFRESH_VALUES[] = {"1", "5", "10", "15", "30", "Never"};
  static constexpr const char* const TOGGLE_VALUES[] = {"OFF", "ON"};
  static constexpr const char* const FRONT_BUTTON_VALUES[] = {"B/C/L/R", "L/R/B/C"};
  static constexpr const char* const SIDE_BUTTON_VALUES[] = {"Prev/Next", "Next/Prev"};
  static constexpr const char* const LANGUAGE_VALUES[] = {
    "English", "Espanol", "Francais", "Deutsch", "Portugues",
    "Italiano", "Russkij", "Polski", "Nederlands",
    "Nihongo", "Zhongwen", "Hangugeo", "Arabiy"
  };

  struct SettingDef {
    const char* label;
    const char* const* values;
    uint8_t valueCount;  // 0 = sub-menu action (no cycling)
  };

#if FEATURE_PLUGINS
  static constexpr int SETTING_COUNT = 10;
#else
  static constexpr int SETTING_COUNT = 9;
#endif
  static const SettingDef DEFS[SETTING_COUNT];

  ButtonBar buttons{"Back", "", "<", ">"};
  uint8_t values[SETTING_COUNT] = {0};
  int8_t selected = 0;
  bool needsRender = true;

  void moveUp() {
    selected = (selected == 0) ? SETTING_COUNT - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % SETTING_COUNT;
    needsRender = true;
  }

  void cycleValue(int delta) {
    const auto& def = DEFS[selected];
    if (def.valueCount == 0) return;  // Sub-menu action, handled externally
    values[selected] = static_cast<uint8_t>((values[selected] + def.valueCount + delta) % def.valueCount);
    needsRender = true;
  }

  bool isSubMenu(int index) const { return DEFS[index].valueCount == 0; }

  const char* getCurrentValueStr(int index) const {
    const auto& def = DEFS[index];
    if (def.valueCount == 0) return ">";  // Sub-menu indicator
    // Bounds check to prevent array out-of-bounds access from corrupted settings
    if (values[index] >= def.valueCount) {
      return def.values[0];
    }
    return def.values[values[index]];
  }
};

void render(const GfxRenderer& r, const Theme& t, const DeviceSettingsView& v);

// ============================================================================
// AppVisibilityView - Per-app show/hide toggles
// ============================================================================
#if FEATURE_PLUGINS

struct AppVisibilityView {
  static constexpr int MAX_APPS = 24;
  static constexpr int VISIBLE_ITEMS = 12;

  // App list populated from PluginListState registry
  char appNames[MAX_APPS][24] = {};
  bool visible[MAX_APPS] = {};  // true = shown, false = hidden
  int appCount = 0;
  int8_t selected = 0;
  int8_t scrollOffset = 0;
  bool needsRender = true;

  ButtonBar buttons{"Back", "Toggle", "", ""};

  void moveUp() {
    if (appCount == 0) return;
    selected = (selected == 0) ? appCount - 1 : selected - 1;
    ensureVisible();
    needsRender = true;
  }

  void moveDown() {
    if (appCount == 0) return;
    selected = (selected + 1) % appCount;
    ensureVisible();
    needsRender = true;
  }

  void toggleSelected() {
    if (selected >= 0 && selected < appCount) {
      visible[selected] = !visible[selected];
      needsRender = true;
    }
  }

  void ensureVisible() {
    if (selected < scrollOffset) scrollOffset = selected;
    if (selected >= scrollOffset + VISIBLE_ITEMS) scrollOffset = selected - VISIBLE_ITEMS + 1;
  }
};

void render(const GfxRenderer& r, const Theme& t, const AppVisibilityView& v);

#endif  // FEATURE_PLUGINS

// ============================================================================
// FileActionMenuView - 3-option file action picker (Right-press popup in
// the file browser). Replaces the previous straight-to-confirm-delete
// flow with a small menu offering "Index for faster reading", "Delete",
// and "Cancel". "Index" is the discoverability hook for sumi.page/process
// — even on books where indexing isn't supported on-device, surfacing
// the option here teaches users the path exists.
// ============================================================================

struct FileActionMenuView {
  static constexpr int MAX_TITLE_LEN = 64;
  static constexpr int ITEM_COUNT = 3;

  // Stable order; Index first so it's the default-highlighted row when
  // the menu opens — most users will press Center expecting "the usual"
  // (delete) and the menu briefly shows them the new option. Cancel last
  // matches the same pattern as ConfirmDialogView's "No" default.
  enum Action : uint8_t { ActionIndex = 0, ActionDelete = 1, ActionCancel = 2 };

  ButtonBar buttons{"Back", "Confirm", "", ""};
  char title[MAX_TITLE_LEN] = "";
  uint8_t selected = ActionIndex;
  bool needsRender = true;

  void setup(const char* fileLabel) {
    utf8SafeCopy(title, fileLabel, MAX_TITLE_LEN);
    selected = ActionIndex;
    needsRender = true;
  }

  void moveUp() {
    selected = (selected == 0) ? ITEM_COUNT - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = static_cast<uint8_t>((selected + 1) % ITEM_COUNT);
    needsRender = true;
  }

  Action currentAction() const { return static_cast<Action>(selected); }
};

void render(const GfxRenderer& r, const Theme& t, const FileActionMenuView& v);

// ============================================================================
// ConfirmDialogView - Yes/No confirmation dialog (matches old ConfirmActionActivity)
// ============================================================================

struct ConfirmDialogView {
  static constexpr int MAX_TITLE_LEN = 32;
  static constexpr int MAX_LINE_LEN = 48;

  ButtonBar buttons{"Back", "Confirm", "", ""};
  char title[MAX_TITLE_LEN] = "Confirm?";
  char line1[MAX_LINE_LEN] = "";
  char line2[MAX_LINE_LEN] = "";
  int8_t selection = 1;  // 0 = Yes, 1 = No (default No for safety)
  bool needsRender = true;

  void setup(const char* t, const char* l1, const char* l2) {
    utf8SafeCopy(title, t, MAX_TITLE_LEN);
    utf8SafeCopy(line1, l1, MAX_LINE_LEN);
    if (l2) {
      utf8SafeCopy(line2, l2, MAX_LINE_LEN);
    } else {
      line2[0] = '\0';
    }
    selection = 1;  // Default to No
    needsRender = true;
  }

  void toggleSelection() {
    selection = selection ? 0 : 1;
    needsRender = true;
  }

  bool isYesSelected() const { return selection == 0; }
};

void render(const GfxRenderer& r, const Theme& t, const ConfirmDialogView& v);

}  // namespace ui

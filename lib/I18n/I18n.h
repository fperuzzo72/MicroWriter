#pragma once
#include <cstdint>

namespace sumi {

enum class Language : uint8_t {
  EN = 0,   // English
  ES,       // Spanish
  FR,       // French
  DE,       // German
  PT,       // Portuguese
  IT,       // Italian
  RU,       // Russian
  PL,       // Polish
  NL,       // Dutch
  JA,       // Japanese
  ZH,       // Chinese
  KO,       // Korean
  AR,       // Arabic
  COUNT
};

// String IDs for all translatable UI text.
// The order is load-bearing: every language table in I18n.cpp must list
// entries in the same order. The static_assert at the bottom of that file
// catches drift.
enum class StrId : uint16_t {
  // ── Home ─────────────────────────────────────────────────────────────
  HOME_NO_BOOK_OPEN,
  HOME_PRESS_FILES,
  HOME_FILES,
  HOME_MENU,
  HOME_CHAPTER_OF_FMT,       // "Chapter %d of %d"
  HOME_PAGE_OF_FMT,          // "Page %d of %d"
  HOME_PERCENT_COMPLETE_FMT, // "%d%% complete"
  HOME_CHAPTERS_TITLE,       // "Chapters"
  HOME_FILES_TITLE,          // "Files" (short)

  // ── Settings submenu titles ──────────────────────────────────────────
  SETTINGS_TITLE,
  SETTINGS_APPS,
  SETTINGS_HOME_ART,
  SETTINGS_WIRELESS,
  SETTINGS_READER,
  SETTINGS_DEVICE,
  SETTINGS_BLUETOOTH,
  SETTINGS_CLEANUP,
  SETTINGS_SYSTEM_INFO,
  APP_VISIBILITY_TITLE,

  // ── Reader settings labels ───────────────────────────────────────────
  READER_SETTINGS_TITLE,
  READER_THEME,
  READER_FONT,
  READER_FONT_SIZE,
  READER_TEXT_LAYOUT,
  READER_LINE_SPACING,
  READER_TEXT_DARKNESS,
  READER_HYPHENATION,
  READER_ANTI_ALIASING,
  READER_SHOW_IMAGES,
  READER_SHOW_TABLES,
  READER_STATUS_BAR,
  READER_PARAGRAPH_ALIGNMENT,
  READER_READING_ORIENTATION,
  READER_LOOKUP_WORD,
  READER_LOOKUP_HISTORY,
  READER_TOGGLE_BOOKMARK,
  READER_VIEW_BOOKMARKS,
  READER_ALL_BOOKMARKS,

  // ── Reader value enums ───────────────────────────────────────────────
  READER_XSMALL,
  READER_SMALL,
  READER_NORMAL,
  READER_LARGE,
  READER_COMPACT,
  READER_STANDARD,
  READER_RELAXED,
  READER_JUSTIFIED,
  READER_ALIGN_LEFT,
  READER_ALIGN_CENTER,
  READER_ALIGN_RIGHT,
  READER_BOOKS_STYLE,
  READER_DARK,
  READER_EXTRA_DARK,
  READER_MAXIMUM,
  READER_SHOW,
  READER_PLACEHOLDER,
  READER_NONE,
  READER_PORTRAIT,
  READER_LANDSCAPE_CW,
  READER_INVERTED,
  READER_LANDSCAPE_CCW,

  // ── Device settings labels ───────────────────────────────────────────
  DEVICE_AUTO_SLEEP,
  DEVICE_SLEEP_SCREEN,
  DEVICE_STARTUP,
  DEVICE_SHORT_PWR,
  DEVICE_PAGES_REFRESH,
  DEVICE_SUNLIGHT_FIX,
  DEVICE_FRONT_BTNS,
  DEVICE_SIDE_BTNS,
  DEVICE_LANGUAGE,

  // ── Device value enums ───────────────────────────────────────────────
  DEVICE_SLEEP_5,
  DEVICE_SLEEP_10,
  DEVICE_SLEEP_15,
  DEVICE_SLEEP_30,
  DEVICE_NEVER,
  DEVICE_DARK,
  DEVICE_LIGHT,
  DEVICE_CUSTOM,
  DEVICE_COVER,
  DEVICE_LAST_PAGE,
  DEVICE_LAST_DOC,
  DEVICE_HOME,
  DEVICE_IGNORE,
  DEVICE_SLEEP,
  DEVICE_PAGE_TURN,
  DEVICE_REFRESH,
  DEVICE_PREV_NEXT,
  DEVICE_NEXT_PREV,

  // ── Cleanup submenu ──────────────────────────────────────────────────
  CLEANUP_CLEAR_CACHE,
  CLEANUP_FORGET_BT,
  CLEANUP_CLEAR_STORAGE,
  CLEANUP_FACTORY_RESET,

  // ── File browser ─────────────────────────────────────────────────────
  FILES_TITLE,
  FILES_EMPTY,

  // ── Plugins ──────────────────────────────────────────────────────────
  PLUGINS_TITLE,

  // ── Sleep ────────────────────────────────────────────────────────────
  SLEEP_SLEEPING,

  // ── Common actions ───────────────────────────────────────────────────
  COMMON_BACK,
  COMMON_OK,
  COMMON_CANCEL,
  COMMON_DELETE,
  COMMON_ON,
  COMMON_OFF,
  COMMON_LOADING,
  COMMON_ERROR,
  COMMON_YES,
  COMMON_NO,

  // ── Button bar verbs ─────────────────────────────────────────────────
  BTN_OPEN,
  BTN_RUN,
  BTN_SELECT,
  BTN_GO,
  BTN_TOGGLE,
  BTN_CONFIRM,
  BTN_ENABLE,
  BTN_DISABLE,

  // ── System info ──────────────────────────────────────────────────────
  INFO_VERSION,
  INFO_UPTIME,
  INFO_BATTERY,
  INFO_FREE_MEMORY,
  INFO_SD_CARD,
  INFO_READING,
  INFO_BOOKS,

  COUNT
};

class I18n {
public:
  static I18n& instance();

  // Get translated string for an id.
  const char* get(StrId id) const;
  const char* operator[](StrId id) const { return get(id); }

  // Translate an English literal. Returns the translation if the literal is
  // in the English->StrId lookup table, otherwise returns the input
  // unchanged. Used from draw helpers (buttonBar / toggle / enumValue) so
  // the rest of the codebase doesn't need StrId refactoring at every
  // construction site.
  const char* trEn(const char* en) const;

  // Language management
  Language language() const { return lang_; }
  void setLanguage(Language lang);
  const char* languageName(Language lang) const;

private:
  I18n() : lang_(Language::EN) {}
  Language lang_;
};

}  // namespace sumi

// Convenience macro -- use this everywhere instead of hardcoded strings.
// Named _tr instead of tr to avoid conflict with ChessGame's Move::tr member.
#define _tr(id) sumi::I18n::instance().get(sumi::StrId::id)

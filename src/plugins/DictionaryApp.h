#pragma once

/**
 * @file DictionaryApp.h
 * @brief Standalone dictionary plugin — on-screen keyboard search with
 *        full StarDict lookup pipeline (direct → stem → fuzzy fallback).
 *
 * Lives alongside Notes / TodoList / Flashcards in the plugin list under
 * the "Learning" category. Uses SUMI's existing KeyboardView for typed
 * input (reuses the in-reader dictionary's lib/Dictionary pipeline) so
 * the user can search even without holding a book open.
 *
 * Unlike the in-reader overlay (which enters via Settings → Look up Word),
 * this plugin always starts on the keyboard screen. The user types a
 * query, presses the Confirm key, and we transition through the usual
 * stages: Definition page, Suggestions list, or back to Search with a
 * "Not found" status.
 */

#include "../config.h"

#if FEATURE_PLUGINS

#include <Arduino.h>
#include <Dictionary.h>
#include <Utf8.h>

#include <string>
#include <vector>

#include "../Theme.h"
#include "../core/Core.h"
#include "../core/SumiSettings.h"
#include "../ui/Elements.h"
#include "../ui/views/DictionaryViews.h"
#include "../ui/views/UtilityViews.h"
#include "PluginHelpers.h"
#include "PluginInterface.h"
#include "PluginRenderer.h"
#include "ThemeManager.h"

namespace sumi {

// Global core instance defined in main.cpp — plugins need it to read/write
// Settings without threading a Core pointer through the plugin interface.
extern Core core;

class DictionaryApp : public PluginInterface {
 public:
  explicit DictionaryApp(PluginRenderer& renderer) : d_(renderer) {}

  const char* name() const override { return "Dictionary"; }
  PluginRunMode runMode() const override { return PluginRunMode::Simple; }
  // Portrait is fine — this is a reading/learning tool, not a landscape
  // writing surface like Notes.
  bool wantsLandscape() const override { return false; }

  void init(int screenW, int screenH) override {
    screenW_ = screenW;
    screenH_ = screenH;

    // Pick up the saved active dictionary. If nothing is configured yet,
    // auto-select the first one we find so the plugin is useful on first
    // open — users shouldn't have to visit Settings before they can type
    // a word.
    const auto& settings = core.settings;
    bool activated = false;
    if (settings.dictionaryName[0] != '\0') {
      activated = sumi::Dictionary::setActive(settings.dictionaryName);
    }
    if (!activated) {
      auto available = sumi::Dictionary::listAvailable();
      if (!available.empty()) {
        sumi::Dictionary::setActive(available.front().name);
        activated = true;
      }
    }
    dictActive_ = activated;

    // Pre-populate the dictionary chooser with whatever we found.
    auto available = sumi::Dictionary::listAvailable();
    dictSelectView_.entries.clear();
    for (const auto& info : available) {
      ui::DictDictSelectView::Entry e;
      e.key = info.name;
      e.displayName = info.displayName;
      e.wordCount = info.wordCount;
      dictSelectView_.entries.push_back(std::move(e));
    }

    // Initialize the keyboard view — title tells the user what's active.
    refreshKeyboardTitle();

    screen_ = Screen::Search;
    needsFullRedraw = true;
  }

  bool handleInput(PluginButton btn) override {
    switch (screen_) {
      case Screen::Search:       return handleSearchInput(btn);
      case Screen::Definition:   return handleDefinitionInput(btn);
      case Screen::Suggestions:  return handleSuggestionsInput(btn);
      case Screen::DictSelect:   return handleDictSelectInput(btn);
      case Screen::Message:      return handleMessageInput(btn);
    }
    return false;
  }

  // SUMI plugins can receive BLE keyboard characters. When the user has a
  // real keyboard paired, let them type straight into the search field.
  bool handleChar(char c) override {
    if (screen_ != Screen::Search) return false;
    if (c == '\r' || c == '\n') {
      // Enter = confirm
      submitSearch();
      return true;
    }
    if (c == '\b' || c == 0x7F) {
      searchView_.backspace();
      needsFullRedraw = true;
      return true;
    }
    if (c >= 32 && c < 127) {
      searchView_.appendChar(c);
      needsFullRedraw = true;
      return true;
    }
    return false;
  }

  void draw() override {
    const Theme& theme = THEME_MANAGER.current();
    GfxRenderer& gfx = d_.gfx();
    switch (screen_) {
      case Screen::Search:
        ui::render(gfx, theme, searchView_);
        break;
      case Screen::Definition:
        ui::render(gfx, theme, definitionView_);
        break;
      case Screen::Suggestions:
        ui::render(gfx, theme, suggestionsView_);
        break;
      case Screen::DictSelect:
        ui::render(gfx, theme, dictSelectView_);
        break;
      case Screen::Message:
        drawMessageScreen(gfx, theme);
        break;
    }
    needsFullRedraw = false;
  }

 private:
  enum class Screen : uint8_t { Search, Definition, Suggestions, DictSelect, Message };

  PluginRenderer& d_;
  int screenW_ = 400;
  int screenH_ = 480;

  Screen screen_ = Screen::Search;
  bool dictActive_ = false;

  ui::KeyboardView searchView_;
  ui::DictDefinitionView definitionView_;
  ui::DictSuggestionsView suggestionsView_;
  ui::DictDictSelectView dictSelectView_;

  std::string messageText_;    // "Word not found", etc.
  std::string messageSubtext_;

  void refreshKeyboardTitle() {
    // Searches now span every installed dictionary, so no per-dict label
    // here. If a user has multiple installed, they'll see each as a
    // labelled section in the definition view after submitting.
    if (sumi::Dictionary::anyAvailable()) {
      searchView_.setTitle("Dictionary");
    } else {
      searchView_.setTitle("No dictionary installed");
    }
  }

  // ── Search screen ────────────────────────────────────────────
  bool handleSearchInput(PluginButton btn) {
    switch (btn) {
      case PluginButton::Up:    searchView_.moveUp();    needsFullRedraw = true; return true;
      case PluginButton::Down:  searchView_.moveDown();  needsFullRedraw = true; return true;
      case PluginButton::Left:  searchView_.moveLeft();  needsFullRedraw = true; return true;
      case PluginButton::Right: searchView_.moveRight(); needsFullRedraw = true; return true;
      case PluginButton::Center: {
        const bool confirmed = searchView_.confirmKey();
        needsFullRedraw = true;
        if (confirmed) submitSearch();
        return true;
      }
      case PluginButton::Back:
        // Let PluginHost exit the plugin back to the plugin list.
        return false;
      default:
        return false;
    }
  }

  void submitSearch() {
    // No more "active dictionary" gate — lookupAll() walks every dict under
    // /dictionary/ on every query. Only bail if literally nothing is
    // installed.
    if (!sumi::Dictionary::anyAvailable()) {
      messageText_ = "No dictionaries installed";
      messageSubtext_ = "Add one to /dictionary/ on SD";
      screen_ = Screen::Message;
      needsFullRedraw = true;
      return;
    }

    const std::string raw(searchView_.input);
    const std::string cleaned = sumi::Dictionary::cleanWord(raw);
    if (cleaned.empty()) {
      messageText_ = "Empty search";
      messageSubtext_ = "Type a word and press Confirm";
      screen_ = Screen::Message;
      needsFullRedraw = true;
      return;
    }

    auto results = sumi::Dictionary::lookupAll(cleaned);

    if (!results.empty()) {
      definitionView_ = ui::DictDefinitionView{};
      definitionView_.headword = cleaned;
      definitionView_.fontId = THEME_MANAGER.current().readerFontId;
      definitionView_.sections.reserve(results.size());
      for (auto& r : results) {
        ui::DictDefinitionView::Section s;
        s.sourceLabel = r.dictDisplayName;
        if (r.viaStemmer) {
          s.sourceLabel += " — stem of ‘";
          s.sourceLabel += r.headword;
          s.sourceLabel += "’";
        }
        s.body = std::move(r.definition);
        definitionView_.sections.push_back(std::move(s));
      }
      definitionView_.wrapText(d_.gfx(), THEME_MANAGER.current());
      screen_ = Screen::Definition;
      needsFullRedraw = true;
      return;
    }

    // No hit in any dict. Aggregate "did you mean?" suggestions across all
    // dicts (deduped), then fall through to "Not found" if none.
    auto similar = sumi::Dictionary::findSimilarAll(cleaned, ui::DictSuggestionsView::MAX_SUGGESTIONS);
    if (!similar.empty()) {
      suggestionsView_ = ui::DictSuggestionsView{};
      suggestionsView_.originalWord = cleaned;
      suggestionsView_.suggestions = std::move(similar);
      suggestionsView_.selectedIndex = 0;
      screen_ = Screen::Suggestions;
      needsFullRedraw = true;
      return;
    }

    messageText_ = "Not found";
    messageSubtext_ = "'" + cleaned + "'";
    screen_ = Screen::Message;
    needsFullRedraw = true;
  }

  // ── Definition screen ────────────────────────────────────────
  bool handleDefinitionInput(PluginButton btn) {
    switch (btn) {
      case PluginButton::Left:
        definitionView_.pageBackward();
        if (definitionView_.needsRender) needsFullRedraw = true;
        return true;
      case PluginButton::Right:
        definitionView_.pageForward();
        if (definitionView_.needsRender) needsFullRedraw = true;
        return true;
      case PluginButton::Back:
      case PluginButton::Center:
        // Back / Done → return to search so the user can type another word.
        screen_ = Screen::Search;
        searchView_.clear();
        needsFullRedraw = true;
        return true;
      default:
        return false;
    }
  }

  // ── Suggestions screen ───────────────────────────────────────
  bool handleSuggestionsInput(PluginButton btn) {
    switch (btn) {
      case PluginButton::Up:
        suggestionsView_.moveUp();
        needsFullRedraw = true;
        return true;
      case PluginButton::Down:
        suggestionsView_.moveDown();
        needsFullRedraw = true;
        return true;
      case PluginButton::Center: {
        const auto* sel = suggestionsView_.selected();
        if (sel == nullptr) return true;
        // Re-enter the search pipeline with the suggestion as the query.
        utf8SafeCopy(searchView_.input, sel->c_str(), ui::KeyboardView::MAX_INPUT_LEN);
        searchView_.inputLen = static_cast<uint8_t>(strlen(searchView_.input));
        submitSearch();
        return true;
      }
      case PluginButton::Back:
        screen_ = Screen::Search;
        needsFullRedraw = true;
        return true;
      default:
        return false;
    }
  }

  // ── Dictionary chooser (from search screen when no dict is active) ───
  bool handleDictSelectInput(PluginButton btn) {
    switch (btn) {
      case PluginButton::Up:
        dictSelectView_.moveUp();
        needsFullRedraw = true;
        return true;
      case PluginButton::Down:
        dictSelectView_.moveDown();
        needsFullRedraw = true;
        return true;
      case PluginButton::Center: {
        const auto* sel = dictSelectView_.selected();
        if (sel == nullptr) return true;
        if (sumi::Dictionary::setActive(sel->key)) {
          // Persist the choice so it survives reboot and matches the
          // in-reader overlay's active dictionary. Save immediately so
          // a crash/reboot right after doesn't lose the selection.
          auto& settings = core.settings;
          utf8SafeCopy(settings.dictionaryName, sel->key.c_str(), sizeof(settings.dictionaryName));
          settings.saveToFile();
          dictActive_ = true;
          refreshKeyboardTitle();
        }
        screen_ = Screen::Search;
        needsFullRedraw = true;
        return true;
      }
      case PluginButton::Back:
        screen_ = Screen::Search;
        needsFullRedraw = true;
        return true;
      default:
        return false;
    }
  }

  // ── Message screen (Not Found, Empty, etc.) ───────────────────
  bool handleMessageInput(PluginButton btn) {
    (void)btn;
    // Any press returns to search.
    screen_ = Screen::Search;
    needsFullRedraw = true;
    return true;
  }

  void drawMessageScreen(GfxRenderer& gfx, const Theme& theme) {
    gfx.clearScreen(theme.backgroundColor);
    ui::title(gfx, theme, theme.screenMarginTop, "Dictionary");
    ui::centeredText(gfx, theme, screenH_ / 2 - 20, messageText_.c_str());
    if (!messageSubtext_.empty()) {
      ui::centeredText(gfx, theme, screenH_ / 2 + 10, messageSubtext_.c_str());
    }
    ui::buttonBar(gfx, theme, "Back", "OK", "", "");
    gfx.displayBuffer();
  }
};

}  // namespace sumi

#endif  // FEATURE_PLUGINS

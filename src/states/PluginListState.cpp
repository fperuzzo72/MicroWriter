#include "PluginListState.h"

#include "../config.h"

#if FEATURE_PLUGINS

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <SDCardManager.h>
#include <Utf8.h>

#include <cstring>
#include <string>

#include "../core/Core.h"
#include "../plugins/LuaPlugin.h"
#include "../ui/Elements.h"
#include "ThemeManager.h"

namespace sumi {

int PluginListState::pluginCount = 0;
PluginEntry PluginListState::plugins[PluginListState::MAX_PLUGINS] = {};

// Lua plugin static storage
char PluginListState::luaPaths_[MAX_LUA_PLUGINS][96] = {};  // audit #53
char PluginListState::luaNames_[MAX_LUA_PLUGINS][24] = {};
int PluginListState::luaPluginCount_ = 0;
PluginRenderer* PluginListState::luaRenderer_ = nullptr;

bool PluginListState::registerPlugin(const char* name, const char* category, PluginFactory factory, const char* savePath) {
  if (pluginCount >= MAX_PLUGINS) return false;
  plugins[pluginCount] = {name, category, factory, savePath};
  pluginCount++;
  Serial.printf("[PLUGINS] Registered: %s (%s)%s\n", name, category, savePath ? " [saveable]" : "");
  return true;
}

// Factory functions for each Lua plugin slot (up to 8)
// These are plain function pointers — no captures — using static index storage.
// nothrow matches the built-in plugin factories in main.cpp: PluginHostState
// already null-checks, so heap-exhausted Lua plugin instantiation falls back
// to Home with a log instead of aborting the device.
static sumi::PluginInterface* luaFactory0() { return new (std::nothrow) sumi::LuaPlugin(*sumi::PluginListState::luaRenderer_, sumi::PluginListState::luaPaths_[0]); }
static sumi::PluginInterface* luaFactory1() { return new (std::nothrow) sumi::LuaPlugin(*sumi::PluginListState::luaRenderer_, sumi::PluginListState::luaPaths_[1]); }
static sumi::PluginInterface* luaFactory2() { return new (std::nothrow) sumi::LuaPlugin(*sumi::PluginListState::luaRenderer_, sumi::PluginListState::luaPaths_[2]); }
static sumi::PluginInterface* luaFactory3() { return new (std::nothrow) sumi::LuaPlugin(*sumi::PluginListState::luaRenderer_, sumi::PluginListState::luaPaths_[3]); }
static sumi::PluginInterface* luaFactory4() { return new (std::nothrow) sumi::LuaPlugin(*sumi::PluginListState::luaRenderer_, sumi::PluginListState::luaPaths_[4]); }
static sumi::PluginInterface* luaFactory5() { return new (std::nothrow) sumi::LuaPlugin(*sumi::PluginListState::luaRenderer_, sumi::PluginListState::luaPaths_[5]); }
static sumi::PluginInterface* luaFactory6() { return new (std::nothrow) sumi::LuaPlugin(*sumi::PluginListState::luaRenderer_, sumi::PluginListState::luaPaths_[6]); }
static sumi::PluginInterface* luaFactory7() { return new (std::nothrow) sumi::LuaPlugin(*sumi::PluginListState::luaRenderer_, sumi::PluginListState::luaPaths_[7]); }

static sumi::PluginFactory luaFactories[sumi::PluginListState::MAX_LUA_PLUGINS] = {
  luaFactory0, luaFactory1, luaFactory2, luaFactory3,
  luaFactory4, luaFactory5, luaFactory6, luaFactory7
};

void PluginListState::scanLuaPlugins(PluginRenderer& renderer) {
  luaRenderer_ = &renderer;
  luaPluginCount_ = 0;

  FsFile dir;
  if (!SdMan.exists(PLUGINS_CUSTOM_DIR) || !dir.open(PLUGINS_CUSTOM_DIR, O_RDONLY)) {
    Serial.println("[LUA] No /custom directory found, skipping Lua scan");
    return;
  }

  Serial.println("[LUA] Scanning /custom/ for .lua plugins...");

  FsFile entry;
  char fname[64];
  while (luaPluginCount_ < MAX_LUA_PLUGINS && entry.openNext(&dir, O_RDONLY)) {
    if (entry.isDirectory()) { entry.close(); continue; }

    entry.getName(fname, sizeof(fname));
    size_t len = strlen(fname);

    // Check for .lua extension
    if (len < 5 || strcasecmp(fname + len - 4, ".lua") != 0) {
      entry.close();
      continue;
    }

    // Build full path
    snprintf(luaPaths_[luaPluginCount_], sizeof(luaPaths_[0]),
             "%s/%s", PLUGINS_CUSTOM_DIR, fname);

    // Derive display name from filename. Walk through std::string so
    // utf8SafeCopy can truncate a long CJK name at a codepoint boundary
    // instead of mid-sequence.
    const std::string baseStr(fname, len - 4);  // strip .lua
    utf8SafeCopy(luaNames_[luaPluginCount_], baseStr.c_str(), sizeof(luaNames_[0]));

    // Replace underscores with spaces (ASCII fast path), capitalize first char.
    // Byte-wise underscore-replacement is safe because '_' (0x5F) never appears
    // inside a UTF-8 continuation byte.
    for (size_t i = 0; luaNames_[luaPluginCount_][i] != '\0'; i++) {
      if (luaNames_[luaPluginCount_][i] == '_')
        luaNames_[luaPluginCount_][i] = ' ';
    }
    char& first = luaNames_[luaPluginCount_][0];
    if (first >= 'a' && first <= 'z') first -= 32;

    Serial.printf("[LUA] Found: %s -> \"%s\"\n",
                  luaPaths_[luaPluginCount_], luaNames_[luaPluginCount_]);

    registerPlugin(luaNames_[luaPluginCount_], "Custom",
                   luaFactories[luaPluginCount_]);

    luaPluginCount_++;
    entry.close();
  }

  dir.close();
  Serial.printf("[LUA] Registered %d Lua plugin(s)\n", luaPluginCount_);
}

PluginListState::PluginListState(GfxRenderer& renderer) : renderer_(renderer) {
  visiblePluginCount_ = 0;
}

void PluginListState::buildVisibleList(const Settings& settings) {
  visiblePluginCount_ = 0;
  displayCount_ = 0;

  // First pass: collect visible plugins
  for (int i = 0; i < pluginCount && visiblePluginCount_ < MAX_PLUGINS; i++) {
    if (!settings.isPluginHidden(i)) {
      visiblePlugins_[visiblePluginCount_++] = static_cast<int8_t>(i);
    }
  }

  // Second pass: build display list grouped by category.
  // Walk unique categories in registration order, emit a header + entries for each.
  // Size the registry to MAX_PLUGINS because every plugin could theoretically
  // define its own category. Previously this was fixed at 8; once a user
  // installed Lua plugins that bumped the unique-category count past 8, the
  // "already emitted" registry stopped being updated and every subsequent
  // plugin in any new category re-emitted a duplicate header + entries on
  // each visit.
  const char* emittedCategories[MAX_PLUGINS] = {};
  int categoryCount = 0;

  for (int i = 0; i < visiblePluginCount_; i++) {
    int idx = visiblePlugins_[i];
    const char* cat = plugins[idx].category;

    // Check if we already emitted this category
    bool found = false;
    for (int c = 0; c < categoryCount; c++) {
      if (strcmp(emittedCategories[c], cat) == 0) { found = true; break; }
    }
    if (found) continue;

    // Emit category header
    if (categoryCount < static_cast<int>(sizeof(emittedCategories) / sizeof(emittedCategories[0]))) {
      emittedCategories[categoryCount++] = cat;
    }
    if (displayCount_ < static_cast<int>(sizeof(displayList_) / sizeof(displayList_[0]))) {
      displayList_[displayCount_++] = {-1, cat};
    }

    // Emit all visible plugins in this category
    for (int j = 0; j < visiblePluginCount_; j++) {
      int jIdx = visiblePlugins_[j];
      if (strcmp(plugins[jIdx].category, cat) == 0) {
        if (displayCount_ < static_cast<int>(sizeof(displayList_) / sizeof(displayList_[0]))) {
          displayList_[displayCount_++] = {static_cast<int8_t>(jIdx), nullptr};
        }
      }
    }
  }
}

void PluginListState::moveSelection(int direction) {
  if (displayCount_ == 0) return;
  int attempts = 0;
  do {
    selected_ += direction;
    if (selected_ < 0) selected_ = displayCount_ - 1;
    if (selected_ >= displayCount_) selected_ = 0;
    attempts++;
  } while (displayList_[selected_].pluginIndex == -1 && attempts < displayCount_);

  // Ensure visible
  int vis = visibleCount();
  if (selected_ < scrollOffset_) scrollOffset_ = selected_;
  if (selected_ >= scrollOffset_ + vis) scrollOffset_ = selected_ - vis + 1;
  // If header is at scroll top, show it
  if (scrollOffset_ > 0 && displayList_[scrollOffset_].pluginIndex == -1 &&
      scrollOffset_ > 0) {
    // Keep header visible by not scrolling past it
  }
  needsRender_ = true;
}

void PluginListState::enter(Core& core) {
  needsRender_ = true;
  goHome_ = false;
  launchPlugin_ = false;

  // Build filtered list based on visibility settings
  buildVisibleList(core.settings);

  // Ensure selection is on a selectable entry (not a category header)
  if (selected_ >= displayCount_) selected_ = 0;
  if (displayCount_ > 0 && displayList_[selected_].pluginIndex == -1) {
    moveSelection(+1);  // skip to first plugin
  }
}

void PluginListState::exit(Core& core) {}

int PluginListState::visibleCount() const {
  const Theme& t = THEME;
  const int startY = 60;
  const int footerH = 40;
  int available = renderer_.getScreenHeight() - startY - footerH;
  int itemTotal = t.menuItemHeight + t.itemSpacing;
  return (itemTotal > 0) ? available / itemTotal : 10;
}

StateTransition PluginListState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
    if (e.type != EventType::ButtonPress) continue;

    switch (e.button) {
      case Button::Up:
        moveSelection(-1);
        break;

      case Button::Down:
        moveSelection(+1);
        break;

      case Button::Left:
      case Button::Back:
        goHome_ = true;
        break;

      case Button::Center:
      case Button::Right:
        if (selected_ >= 0 && selected_ < displayCount_ &&
            displayList_[selected_].pluginIndex >= 0) {
          launchPlugin_ = true;
        }
        break;

      case Button::Power:
        return StateTransition::to(StateId::Sleep);

      default:
        break;
    }
  }

  if (goHome_) {
    goHome_ = false;
    return StateTransition::to(StateId::Settings);
  }

  if (launchPlugin_ && hostState_) {
    launchPlugin_ = false;
    if (selected_ >= 0 && selected_ < displayCount_ && displayList_[selected_].pluginIndex >= 0) {
      int actualIdx = displayList_[selected_].pluginIndex;
      hostState_->setPluginFactory(plugins[actualIdx].factory);
      return StateTransition::to(StateId::PluginHost);
    }
  }

  return StateTransition::stay(StateId::PluginList);
}

void PluginListState::render(Core& core) {
  if (!needsRender_) return;
  drawList();
  renderer_.displayBuffer(EInkDisplay::FAST_REFRESH);
  needsRender_ = false;
  core.display.markDirty();
}

void PluginListState::drawList() const {
  const Theme& t = THEME;

  renderer_.clearScreen(t.backgroundColor);

  // Header
  ui::title(renderer_, t, t.screenMarginTop, _tr(PLUGINS_TITLE));

  const int startY = 60;
  const int itemH = t.menuItemHeight + t.itemSpacing;
  int vis = visibleCount();
  const int W = renderer_.getScreenWidth();

  for (int i = scrollOffset_; i < displayCount_ && i < scrollOffset_ + vis; i++) {
    const int y = startY + (i - scrollOffset_) * itemH;
    const auto& entry = displayList_[i];

    if (entry.pluginIndex == -1) {
      // Category header: draw as a subtle separator with the category name
      const int lineY = y + t.menuItemHeight / 2;
      const int textW = renderer_.getTextWidth(t.smallFontId, entry.categoryName);
      const int textX = t.screenMarginSide + 8;
      const int lineStart = textX + textW + 8;
      const int lineEnd = W - t.screenMarginSide;

      renderer_.drawText(t.smallFontId, textX, y + 4, entry.categoryName, t.secondaryTextBlack);
      if (lineStart < lineEnd) {
        renderer_.drawLine(lineStart, lineY, lineEnd, lineY, t.secondaryTextBlack);
      }
    } else {
      // Plugin entry: indented menuItem
      int actualIdx = entry.pluginIndex;
      ui::menuItem(renderer_, t, y, plugins[actualIdx].name, i == selected_);

      // "Continue" indicator for games with saved progress
      if (plugins[actualIdx].savePath && SdMan.exists(plugins[actualIdx].savePath)) {
        const int h = t.menuItemHeight;
        const int textY2 = y + (h - renderer_.getLineHeight(t.smallFontId)) / 2;
        const int rightEdge = W - t.screenMarginSide - t.itemPaddingX;
        const int tw = renderer_.getTextWidth(t.smallFontId, "Continue");
        bool black = (i == selected_) ? t.selectionTextBlack : t.secondaryTextBlack;
        renderer_.drawText(t.smallFontId, rightEdge - tw, textY2, "Continue", black);
      }
    }
  }

  // Scroll indicators
  if (scrollOffset_ > 0) {
    int cx = W / 2;
    renderer_.drawLine(cx, startY - 6, cx - 6, startY - 1, true);
    renderer_.drawLine(cx, startY - 6, cx + 6, startY - 1, true);
  }
  if (scrollOffset_ + vis < displayCount_) {
    int cx = W / 2;
    int ay = renderer_.getScreenHeight() - 38;
    renderer_.drawLine(cx, ay, cx - 6, ay - 6, true);
    renderer_.drawLine(cx, ay, cx + 6, ay - 6, true);
  }
}

}  // namespace sumi

#endif

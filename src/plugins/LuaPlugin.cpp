#include "LuaPlugin.h"

#include "../config.h"

#if FEATURE_PLUGINS

#include <Arduino.h>
#include <SDCardManager.h>
#include <Utf8.h>

#include <string>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "../core/MemoryArena.h"
#include "LuaBindings.h"
#include "PluginHelpers.h"

#if FEATURE_BLUETOOTH
#include "LuaBridgeBindings.h"
#include "../ble/BleBridge.h"
#endif

namespace sumi {

// ---------------------------------------------------------------------------
// Custom allocator with memory cap
// ---------------------------------------------------------------------------
//
// Lua's allocator contract: nsize == 0 means free; otherwise (re)allocate to
// nsize bytes. The previous implementation computed `delta = nsize - osize`
// in unsigned arithmetic and applied the limit check unconditionally. When
// Lua shrinks (nsize < osize) — which the GC does routinely on table
// compactions and string-buffer trimming — that subtraction underflowed to
// a huge size_t. The limit check then either spuriously denied the shrink
// (Lua VM panics on a denied alloc) or, after `memUsed_ + delta` wrapped,
// permanently poisoned `memUsed_`. Either way the 40 KB cap was unreliable
// and Lua plugins would die mid-session on innocuous garbage collection.
//
// Fix: branch on grow vs. shrink. Apply the limit check only when growing.
// Shrinks always succeed (realloc-down is defined to succeed in practice).
// Account each direction with positive arithmetic; defensively clamp the
// shrink path against `memUsed_` so a future drift can't underflow.
void* LuaPlugin::luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* self = static_cast<LuaPlugin*>(ud);

  if (nsize == 0) {
    // Free path. Lua passes the real osize per spec.
    if (ptr) {
      // Defensive: never let memUsed_ underflow if osize is somehow stale.
      self->memUsed_ = (osize > self->memUsed_) ? 0 : self->memUsed_ - osize;
      free(ptr);
    }
    return nullptr;
  }

  const size_t oldSize = ptr ? osize : 0;

  // Limit check only when growing. Shrinks always succeed.
  if (nsize > oldSize) {
    const size_t growth = nsize - oldSize;
    if (self->memUsed_ + growth > LUA_MEM_LIMIT) {
      Serial.printf("[LUA] Memory limit reached (%zu + %zu > %zu)\n",
                    self->memUsed_, growth, LUA_MEM_LIMIT);
      return nullptr;
    }
  }

  void* newPtr = realloc(ptr, nsize);
  if (newPtr) {
    if (nsize > oldSize) {
      self->memUsed_ += (nsize - oldSize);
    } else if (oldSize > nsize) {
      const size_t shrink = oldSize - nsize;
      self->memUsed_ = (shrink > self->memUsed_) ? 0 : self->memUsed_ - shrink;
    }
    // nsize == oldSize: realloc may relocate, accounting unchanged.
  }
  return newPtr;
}

// ---------------------------------------------------------------------------
// Instruction count hook — fires after INSTRUCTION_LIMIT ops
// ---------------------------------------------------------------------------
void LuaPlugin::luaHook(lua_State* L, lua_Debug* ar) {
  (void)ar;
  luaL_error(L, "Script exceeded instruction limit (infinite loop?)");
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
LuaPlugin::LuaPlugin(PluginRenderer& renderer, const char* scriptPath)
    : renderer_(renderer) {
  // UTF-8 safe: CJK-named Lua scripts would otherwise lose their last
  // character in the plugin list.
  utf8SafeCopy(scriptPath_, scriptPath, sizeof(scriptPath_));

  // Derive name from filename: "/custom/my_game.lua" → "my_game"
  const char* slash = strrchr(scriptPath_, '/');
  const char* base = slash ? slash + 1 : scriptPath_;
  const char* dot = strrchr(base, '.');
  const size_t baseLen = dot ? (size_t)(dot - base) : strlen(base);
  // Use utf8SafeCopy via a temporary std::string so a CJK display name
  // lands on a codepoint boundary rather than the raw byte limit.
  const std::string baseStr(base, baseLen);
  utf8SafeCopy(name_, baseStr.c_str(), sizeof(name_));
  const size_t len = strlen(name_);

  // Replace underscores with spaces for display
  for (size_t i = 0; i < len; i++) {
    if (name_[i] == '_') name_[i] = ' ';
  }
  // Capitalize first letter
  if (name_[0] >= 'a' && name_[0] <= 'z') {
    name_[0] -= 32;
  }

  errorMsg_[0] = '\0';
}

LuaPlugin::~LuaPlugin() {
  cleanup();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void LuaPlugin::init(int screenW, int screenH) {
  screenW_ = screenW;
  screenH_ = screenH;
  hasError_ = false;
  errorMsg_[0] = '\0';

  Serial.printf("[LUA] Init: %s (%dx%d)\n", scriptPath_, screenW, screenH);
  Serial.printf("[LUA] Heap before: free=%lu, largest=%lu\n",
                (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());

  // (v1 used to release MemoryArena here to free 80 KB for the Lua VM.
  //  v2's arena is .bss-resident and never freed; the LUA_MEM_LIMIT cap
  //  caps Lua's appetite to 40 KB out of the heap that remains after
  //  arena + NimBLE.)

  // Create Lua state with custom allocator
  memUsed_ = 0;
  L_ = lua_newstate(luaAlloc, this);
  if (!L_) {
    snprintf(errorMsg_, sizeof(errorMsg_), "Failed to create Lua VM");
    hasError_ = true;
    Serial.printf("[LUA] ERROR: %s\n", errorMsg_);
    return;
  }

  // Set instruction count hook
  lua_sethook(L_, luaHook, LUA_MASKCOUNT, INSTRUCTION_LIMIT);

  // Open safe standard libraries
  luaL_openlibs(L_);

  // Sandbox: remove dangerous globals
  lua_bind::sandboxGlobals(L_);

  // Store renderer pointer in registry
  lua_pushlightuserdata(L_, &renderer_);
  lua_setfield(L_, LUA_REGISTRYINDEX, "sumi_renderer");

  // Store screen dimensions in registry
  lua_pushinteger(L_, screenW);
  lua_setfield(L_, LUA_REGISTRYINDEX, "sumi_screenW");
  lua_pushinteger(L_, screenH);
  lua_setfield(L_, LUA_REGISTRYINDEX, "sumi_screenH");

  // Store sandbox directory in registry: "/custom/myplugin.lua" -> "/custom/myplugin_data/"
  {
    const char* slash = strrchr(scriptPath_, '/');
    const char* base = slash ? slash + 1 : scriptPath_;
    const char* dot = strrchr(base, '.');
    char sandboxDir[80];
    // Build prefix (directory part including trailing slash)
    size_t prefixLen = (size_t)(base - scriptPath_);
    if (prefixLen >= sizeof(sandboxDir)) prefixLen = sizeof(sandboxDir) - 1;
    memcpy(sandboxDir, scriptPath_, prefixLen);
    // Append basename without extension + "_data/"
    size_t baseLen = dot ? (size_t)(dot - base) : strlen(base);
    int wrote = snprintf(sandboxDir + prefixLen, sizeof(sandboxDir) - prefixLen,
                         "%.*s_data/", (int)baseLen, base);
    if (wrote > 0 && prefixLen + (size_t)wrote < sizeof(sandboxDir)) {
      lua_pushstring(L_, sandboxDir);
    } else {
      lua_pushstring(L_, "/custom/_fallback_data/");
    }
    lua_setfield(L_, LUA_REGISTRYINDEX, "sumi_plugin_dir");
  }

  // Register all SUMI drawing bindings
  lua_bind::registerAll(L_);

#if FEATURE_BLUETOOTH
  // Register the `bridge` global table and install the inbound dispatcher.
  // The underlying BLE service is NOT brought up here — the bindings
  // lazy-initialize it on the first bridge.publish / bridge.on /
  // bridge.keep_awake call. A pure drawing plugin (Chess, 2048, etc.)
  // that never touches bridge.* pays zero heap/battery cost for BLE.
  lua_bridge_bind::registerInto(L_);
  {
    lua_State* L_ref = L_;
    ble_bridge::setInboundHandler(
      [L_ref](const char* topic, const char* json) {
        lua_bridge_bind::dispatchInbound(L_ref, topic, json);
      });
  }
#endif  // FEATURE_BLUETOOTH

  // Set SCREEN_W and SCREEN_H constants
  lua_pushinteger(L_, screenW); lua_setglobal(L_, "SCREEN_W");
  lua_pushinteger(L_, screenH); lua_setglobal(L_, "SCREEN_H");

  Serial.printf("[LUA] VM created, mem=%zu bytes\n", memUsed_);

  // Load and execute the script
  if (!loadScript()) {
    return;  // hasError_ already set
  }

  // Check if update() is defined
  lua_getglobal(L_, "update");
  hasUpdate_ = lua_isfunction(L_, -1);
  lua_pop(L_, 1);

  // Call init(w, h)
  lua_getglobal(L_, "init");
  if (lua_isfunction(L_, -1)) {
    lua_pushinteger(L_, screenW);
    lua_pushinteger(L_, screenH);
    callLuaFunc("init", 2, 0);
  } else {
    lua_pop(L_, 1);  // init is optional
  }

  Serial.printf("[LUA] Ready, mem=%zu bytes, hasUpdate=%d\n", memUsed_, hasUpdate_);
}

void LuaPlugin::cleanup() {
#if FEATURE_BLUETOOTH
  // Drop the inbound handler before closing L_ — otherwise a late BLE
  // inbound message could call into a freed Lua state.
  ble_bridge::setInboundHandler(nullptr);
  // Clear the keep-awake window so the next plugin doesn't inherit it.
  ble_bridge::keepAwake(0);
#endif

  if (L_) {
    lua_close(L_);
    L_ = nullptr;
    memUsed_ = 0;
    Serial.printf("[LUA] VM closed\n");
  }

  // (v1 used to reclaim the MemoryArena here. v2 never released it, so
  //  there is nothing to reclaim.)

  Serial.printf("[LUA] Cleanup done, heap: free=%lu\n", (unsigned long)ESP.getFreeHeap());
}

// ---------------------------------------------------------------------------
// Script loading
// ---------------------------------------------------------------------------
bool LuaPlugin::loadScript() {
  FsFile f;
  if (!SdMan.openFileForRead("LUA", scriptPath_, f)) {
    snprintf(errorMsg_, sizeof(errorMsg_), "Cannot open: %s", scriptPath_);
    hasError_ = true;
    Serial.printf("[LUA] ERROR: %s\n", errorMsg_);
    return false;
  }

  size_t fileSize = f.size();
  if (fileSize > MAX_SCRIPT_SIZE) {
    f.close();
    snprintf(errorMsg_, sizeof(errorMsg_), "Script too large (%zuKB > %zuKB)",
             fileSize / 1024, MAX_SCRIPT_SIZE / 1024);
    hasError_ = true;
    Serial.printf("[LUA] ERROR: %s\n", errorMsg_);
    return false;
  }

  // Read script into temporary buffer
  char* buf = static_cast<char*>(malloc(fileSize + 1));
  if (!buf) {
    f.close();
    snprintf(errorMsg_, sizeof(errorMsg_), "No memory for script (%zu bytes)", fileSize);
    hasError_ = true;
    Serial.printf("[LUA] ERROR: %s\n", errorMsg_);
    return false;
  }

  // FsFile::read returns int — -1 on error. Clamp to avoid SIZE_MAX
  // becoming the terminator index and writing far out of bounds.
  const int rawRead = f.read(reinterpret_cast<uint8_t*>(buf), fileSize);
  f.close();
  if (rawRead <= 0) {
    snprintf(errorMsg_, sizeof(errorMsg_), "Read failed for script");
    hasError_ = true;
    Serial.printf("[LUA] ERROR: %s\n", errorMsg_);
    free(buf);
    return false;
  }
  const size_t bytesRead = static_cast<size_t>(rawRead);
  buf[bytesRead] = '\0';

  Serial.printf("[LUA] Loaded %zu bytes from %s\n", bytesRead, scriptPath_);

  // Execute the script (defines global functions)
  int err = luaL_dostring(L_, buf);
  free(buf);

  if (err != LUA_OK) {
    const char* msg = lua_tostring(L_, -1);
    snprintf(errorMsg_, sizeof(errorMsg_), "%.79s", msg ? msg : "Unknown error");
    lua_pop(L_, 1);
    hasError_ = true;
    Serial.printf("[LUA] Load error: %s\n", errorMsg_);
    return false;
  }

  // Verify required functions exist
  lua_getglobal(L_, "draw");
  bool hasDraw = lua_isfunction(L_, -1);
  lua_pop(L_, 1);

  lua_getglobal(L_, "onButton");
  bool hasOnButton = lua_isfunction(L_, -1);
  lua_pop(L_, 1);

  if (!hasDraw) {
    snprintf(errorMsg_, sizeof(errorMsg_), "Missing required function: draw()");
    hasError_ = true;
    Serial.printf("[LUA] ERROR: %s\n", errorMsg_);
    return false;
  }

  if (!hasOnButton) {
    snprintf(errorMsg_, sizeof(errorMsg_), "Missing required function: onButton()");
    hasError_ = true;
    Serial.printf("[LUA] ERROR: %s\n", errorMsg_);
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Call a Lua function safely with error handling
// ---------------------------------------------------------------------------
bool LuaPlugin::callLuaFunc(const char* funcName, int nargs, int nresults) {
  // Reset instruction counter by re-setting the hook
  lua_sethook(L_, luaHook, LUA_MASKCOUNT, INSTRUCTION_LIMIT);

  int err = lua_pcall(L_, nargs, nresults, 0);
  if (err != LUA_OK) {
    const char* msg = lua_tostring(L_, -1);
    snprintf(errorMsg_, sizeof(errorMsg_), "%s: %.60s",
             funcName, msg ? msg : "error");
    lua_pop(L_, 1);
    hasError_ = true;
    Serial.printf("[LUA] Runtime error in %s: %s\n", funcName, errorMsg_);
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void LuaPlugin::draw() {
  if (hasError_ || !L_) {
    showError();
    return;
  }

  lua_getglobal(L_, "draw");
  if (!lua_isfunction(L_, -1)) {
    lua_pop(L_, 1);
    return;
  }
  callLuaFunc("draw");

  if (hasError_) showError();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
const char* LuaPlugin::buttonName(PluginButton btn) {
  switch (btn) {
    case PluginButton::Up:     return "up";
    case PluginButton::Down:   return "down";
    case PluginButton::Left:   return "left";
    case PluginButton::Right:  return "right";
    case PluginButton::Center: return "center";
    case PluginButton::Back:   return "back";
    case PluginButton::Power:  return "power";
    default:                   return "none";
  }
}

bool LuaPlugin::handleInput(PluginButton btn) {
  if (hasError_ || !L_) return false;

  lua_getglobal(L_, "onButton");
  if (!lua_isfunction(L_, -1)) {
    lua_pop(L_, 1);
    return false;
  }

  lua_pushstring(L_, buttonName(btn));

  if (!callLuaFunc("onButton", 1, 1)) {
    return false;
  }

  bool consumed = lua_toboolean(L_, -1);
  lua_pop(L_, 1);
  needsFullRedraw = true;
  return consumed;
}

// ---------------------------------------------------------------------------
// Update (10Hz tick)
// ---------------------------------------------------------------------------
bool LuaPlugin::update() {
  // Bridge ticking happens unconditionally in main.cpp's main loop so
  // plugins without update() still get inbound dispatch and outbound
  // notifications.

  if (!hasUpdate_ || hasError_ || !L_) return false;

  lua_getglobal(L_, "update");
  if (!lua_isfunction(L_, -1)) {
    lua_pop(L_, 1);
    return false;
  }

  if (!callLuaFunc("update", 0, 1)) {
    return false;
  }

  bool needsRedraw = lua_toboolean(L_, -1);
  lua_pop(L_, 1);
  if (needsRedraw) needsFullRedraw = true;
  return needsRedraw;
}

// ---------------------------------------------------------------------------
// Run mode
// ---------------------------------------------------------------------------
PluginRunMode LuaPlugin::runMode() const {
  return hasUpdate_ ? PluginRunMode::WithUpdate : PluginRunMode::Simple;
}

// ---------------------------------------------------------------------------
// Error display
// ---------------------------------------------------------------------------
void LuaPlugin::showError() {
  renderer_.fillScreen(false);  // WHITE
  PluginUI::drawHeader(renderer_, "Lua Error", screenW_);
  renderer_.setCursor(PLUGIN_MARGIN, PLUGIN_HEADER_H + 20);
  renderer_.setTextColor(true);  // BLACK
  renderer_.print(name_);
  renderer_.setCursor(PLUGIN_MARGIN, PLUGIN_HEADER_H + 50);
  // Word-wrap error message manually for small screen
  const char* p = errorMsg_;
  int y = PLUGIN_HEADER_H + 50;
  int maxW = screenW_ - PLUGIN_MARGIN * 2;
  while (*p && y < screenH_ - PLUGIN_FOOTER_H - 20) {
    // Find how much fits on this line
    char lineBuf[64];
    int len = 0;
    while (p[len] && len < 63) {
      lineBuf[len] = p[len];
      lineBuf[len + 1] = '\0';
      if (renderer_.getTextWidth(lineBuf) > maxW) break;
      len++;
    }
    if (len == 0) { len = 1; lineBuf[0] = *p; lineBuf[1] = '\0'; }
    renderer_.setCursor(PLUGIN_MARGIN, y);
    lineBuf[len] = '\0';
    renderer_.print(lineBuf);
    p += len;
    y += renderer_.getLineHeight() + 2;
  }
  PluginUI::drawFooter(renderer_, "Back: exit", "", screenW_, screenH_);
}

}  // namespace sumi

#endif  // FEATURE_PLUGINS

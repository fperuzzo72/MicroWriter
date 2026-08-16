#pragma once

/**
 * @file LuaBridgeBindings.h
 * @brief Exposes ble_bridge to Lua scripts as a global `bridge` table.
 *
 *   bridge.publish(topic, data)       -- data may be string, table, number, bool, nil
 *   bridge.on(topic, function(data))  -- handler receives the decoded `data` field
 *   bridge.connected()                -- true if a client is currently paired
 *   bridge.keep_awake(seconds)        -- defer auto-sleep for up to `seconds` (max 3600)
 *   bridge.remaining_awake()          -- seconds left in the current keep-awake window (0 if none)
 *
 * The handler registry lives at registry key "sumi_bridge_handlers". The
 * active Lua state is cached at "sumi_bridge_state" so the C++ inbound
 * dispatcher can re-enter Lua safely from the main loop.
 *
 * All data is serialized as compact JSON using ArduinoJson. Oversized
 * payloads (>MAX_MSG_SIZE) are rejected with a return of false from
 * bridge.publish.
 */

#include "../config.h"

#if FEATURE_PLUGINS && FEATURE_BLUETOOTH

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <ArduinoJson.h>

#include "../ble/BleBridge.h"

namespace sumi {
namespace lua_bridge_bind {

// Serialize a Lua value at stack index `idx` into a JSON document. Recurses
// into tables. Arrays and maps are disambiguated by checking for a 1-based
// integer-only key sequence (standard Lua array detection).
inline void luaToJson(lua_State* L, int idx, JsonVariant v, int depth = 0);

// Max recursion depth for luaToJson. A circular reference (a = {}; a.self = a)
// would otherwise stack-overflow the 32 KB main task loop stack. 16 levels is
// deeper than any reasonable plugin payload.
static constexpr int LUA_TO_JSON_MAX_DEPTH = 16;

inline void luaTableToJson(lua_State* L, int idx, JsonVariant v, int depth) {
    if (depth >= LUA_TO_JSON_MAX_DEPTH) {
        v.set(nullptr);
        return;
    }
    // Detect array vs object: an array has keys 1..N as integers only.
    bool isArray = true;
    int arrayLen = 0;
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        arrayLen++;
        if (lua_type(L, -2) != LUA_TNUMBER) { isArray = false; }
        else {
            lua_Number k = lua_tonumber(L, -2);
            if (k != (lua_Number)(lua_Integer)k || (lua_Integer)k != arrayLen) {
                isArray = false;
            }
        }
        lua_pop(L, 1);
        if (!isArray) {
            // Drain the rest so the stack doesn't leak values
            while (lua_next(L, idx) != 0) lua_pop(L, 1);
            break;
        }
    }

    if (isArray) {
        JsonArray arr = v.to<JsonArray>();
        lua_pushnil(L);
        while (lua_next(L, idx) != 0) {
            JsonVariant el = arr.add<JsonVariant>();
            luaToJson(L, lua_gettop(L), el, depth + 1);
            lua_pop(L, 1);
        }
    } else {
        JsonObject obj = v.to<JsonObject>();
        lua_pushnil(L);
        while (lua_next(L, idx) != 0) {
            // key is at -2, value is at -1. Stringify the key.
            char keyBuf[64];
            if (lua_type(L, -2) == LUA_TSTRING) {
                size_t klen;
                const char* ks = lua_tolstring(L, -2, &klen);
                snprintf(keyBuf, sizeof(keyBuf), "%.*s", (int)klen, ks);
            } else if (lua_type(L, -2) == LUA_TNUMBER) {
                snprintf(keyBuf, sizeof(keyBuf), "%g", lua_tonumber(L, -2));
            } else {
                lua_pop(L, 1);
                continue;
            }
            JsonVariant child = obj[keyBuf].to<JsonVariant>();
            luaToJson(L, lua_gettop(L), child, depth + 1);
            lua_pop(L, 1);
        }
    }
}

inline void luaToJson(lua_State* L, int idx, JsonVariant v, int depth) {
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
            v.set(nullptr);
            break;
        case LUA_TBOOLEAN:
            v.set((bool)lua_toboolean(L, idx));
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) v.set((int64_t)lua_tointeger(L, idx));
            else                       v.set(lua_tonumber(L, idx));
            break;
        case LUA_TSTRING: {
            size_t n;
            const char* s = lua_tolstring(L, idx, &n);
            v.set(std::string(s, n));
            break;
        }
        case LUA_TTABLE:
            luaTableToJson(L, idx, v, depth);
            break;
        default:
            v.set(nullptr);
            break;
    }
}

// Reverse direction — walk an ArduinoJson variant into a fresh Lua value
// pushed onto the stack. Depth-capped; ArduinoJson's own NESTING_LIMIT (10
// by default) already bounds parse depth, but an independent cap here
// protects the main-task stack if that library setting ever changes.
static constexpr int JSON_TO_LUA_MAX_DEPTH = 16;

inline void jsonToLua(lua_State* L, JsonVariantConst v, int depth = 0) {
    if (depth >= JSON_TO_LUA_MAX_DEPTH) { lua_pushnil(L); return; }
    if (v.isNull())              { lua_pushnil(L); return; }
    if (v.is<bool>())            { lua_pushboolean(L, v.as<bool>()); return; }
    if (v.is<int64_t>())         { lua_pushinteger(L, v.as<int64_t>()); return; }
    if (v.is<double>())          { lua_pushnumber(L, v.as<double>()); return; }
    if (v.is<const char*>())     { lua_pushstring(L, v.as<const char*>()); return; }
    if (v.is<JsonArrayConst>()) {
        lua_newtable(L);
        int i = 1;
        for (JsonVariantConst el : v.as<JsonArrayConst>()) {
            jsonToLua(L, el, depth + 1);
            lua_rawseti(L, -2, i++);
        }
        return;
    }
    if (v.is<JsonObjectConst>()) {
        lua_newtable(L);
        for (JsonPairConst kv : v.as<JsonObjectConst>()) {
            lua_pushstring(L, kv.key().c_str());
            jsonToLua(L, kv.value(), depth + 1);
            lua_rawset(L, -3);
        }
        return;
    }
    lua_pushnil(L);
}

// ── Lua functions ──────────────────────────────────────────────────────────

// Lazily bring up the BLE bridge on first real use. Pure drawing plugins
// never hit any bridge.* function, so they never pay the BLE init cost.
inline void ensureBridgeUp() {
    if (!ble_bridge::isReady()) {
        ble_bridge::init();
        ble_bridge::startAdvertising();
    }
}

// bridge.publish(topic [, data]) -> true/false
inline int l_publish(lua_State* L) {
    const char* topic = luaL_checkstring(L, 1);
    int argc = lua_gettop(L);

    ensureBridgeUp();

    bool ok = false;
    if (argc < 2 || lua_isnil(L, 2)) {
        ok = ble_bridge::publishEnveloped(topic, nullptr);
    } else {
        // Build JSON for the data argument
        JsonDocument doc;
        JsonVariant root = doc.to<JsonVariant>();
        luaToJson(L, 2, root);
        // Serialize to a stack buffer sized to fit a single BLE message.
        char buf[ble_bridge::MAX_MSG_SIZE + 1];
        size_t n = serializeJson(doc, buf, sizeof(buf));
        if (n == 0 || n >= sizeof(buf)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        ok = ble_bridge::publishEnveloped(topic, buf);
    }

    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// bridge.on(topic, fn) -> true (stores fn in registry)
inline int l_on(lua_State* L) {
    const char* topic = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    ensureBridgeUp();

    // handlers = registry["sumi_bridge_handlers"] or {}
    lua_getfield(L, LUA_REGISTRYINDEX, "sumi_bridge_handlers");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "sumi_bridge_handlers");
    }

    // handlers[topic] = fn
    lua_pushstring(L, topic);
    lua_pushvalue(L, 2);
    lua_rawset(L, -3);
    lua_pop(L, 1);

    lua_pushboolean(L, 1);
    return 1;
}

// bridge.connected() -> bool
inline int l_connected(lua_State* L) {
    lua_pushboolean(L, ble_bridge::isConnected() ? 1 : 0);
    return 1;
}

// bridge.keep_awake(seconds) -> nil
inline int l_keep_awake(lua_State* L) {
    lua_Integer secs = luaL_checkinteger(L, 1);
    if (secs < 0) secs = 0;
    // keep_awake itself doesn't require the BLE service to be up — it's
    // a power-manager request. Call the underlying primitive directly.
    ble_bridge::keepAwake((uint32_t)secs);
    return 0;
}

// bridge.remaining_awake() -> integer
inline int l_remaining_awake(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)ble_bridge::keepAwakeRemainingSeconds());
    return 1;
}

// ── Dispatcher ─────────────────────────────────────────────────────────────
// Invoked from BleBridge::process() (main task) for each parsed inbound
// message. Looks up the registered Lua handler for the message's topic and
// calls it with the decoded `data` value.
inline void dispatchInbound(lua_State* L, const char* topic, const char* json) {
    if (!L) return;
    // handlers = registry["sumi_bridge_handlers"]; handlers[topic]?
    lua_getfield(L, LUA_REGISTRYINDEX, "sumi_bridge_handlers");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_pushstring(L, topic);
    lua_rawget(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return;
    }
    lua_remove(L, -2);  // drop handlers table, keep fn

    // Parse inbound JSON and push the `data` subtree as the single argument.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        lua_pushnil(L);
    } else {
        JsonVariantConst data = doc["data"];
        jsonToLua(L, data);
    }

    if (lua_pcall(L, 1, 0, 0) != 0) {
        Serial.printf("[BRIDGE] Lua handler error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// Register the bridge table into a Lua state.
inline void registerInto(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_publish);         lua_setfield(L, -2, "publish");
    lua_pushcfunction(L, l_on);              lua_setfield(L, -2, "on");
    lua_pushcfunction(L, l_connected);       lua_setfield(L, -2, "connected");
    lua_pushcfunction(L, l_keep_awake);      lua_setfield(L, -2, "keep_awake");
    lua_pushcfunction(L, l_remaining_awake); lua_setfield(L, -2, "remaining_awake");
    lua_setglobal(L, "bridge");

    // Fresh handler table
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "sumi_bridge_handlers");

    // Remember this L so the C++ dispatcher can re-enter it
    lua_pushlightuserdata(L, (void*)L);
    lua_setfield(L, LUA_REGISTRYINDEX, "sumi_bridge_state");
}

// Return the Lua state that owns the active bridge handlers, or nullptr.
inline lua_State* getActiveState(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "sumi_bridge_state");
    lua_State* out = (lua_State*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return out;
}

}  // namespace lua_bridge_bind
}  // namespace sumi

#endif  // FEATURE_PLUGINS && FEATURE_BLUETOOTH

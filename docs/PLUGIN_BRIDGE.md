# SUMI Plugin Bridge

The Plugin Bridge lets a Lua plugin on the device exchange JSON messages
with a Web-Bluetooth client (sumi.page or any client that speaks the
protocol). It's the glue that turns SUMI from a closed reader into an
open companion device — any AI can write you a plugin and its matching
browser UI in one shot, and you deploy both by dragging files into the
site.

This file is the **protocol reference**. For step-by-step plugin
authoring instructions (including the canonical prompt to paste into
your AI of choice), see [`PLUGIN_AUTHORING_PROMPT.md`](PLUGIN_AUTHORING_PROMPT.md).

## Wire protocol

### Service & characteristics

| Role    | UUID                                      | Direction       | Properties       |
|---------|-------------------------------------------|-----------------|------------------|
| Service | `19B10100-E8F2-537E-4F6C-D104768A1214`    | —               | —                |
| TX      | `19B10101-E8F2-537E-4F6C-D104768A1214`    | device → site   | `NOTIFY`, `READ` |
| RX      | `19B10102-E8F2-537E-4F6C-D104768A1214`    | site → device   | `WRITE`, `WRITE_NR` |

### Message format

Every message is a single UTF-8 JSON object, at most **480 bytes** so it
fits in one BLE notification at the negotiated MTU.

```json
{
  "topic": "doorbell/ring",
  "data":  { "at": 1713225600, "rssi": -42 }
}
```

- `topic` is a required string (≤ 63 bytes). Convention: `<plugin>/<verb>`.
  Reserved prefixes: `system/` (device-internal), `bridge/` (transport).
- `data` is optional and may be any JSON value (object, array, string,
  number, boolean, null). Pass whatever structure makes sense.

Messages larger than 480 bytes are rejected at both ends — the sender is
expected to split its payload into multiple `<topic>/chunk` messages and
reassemble on the receiver. V1 does not define a framing helper; the
plugin author picks one.

## Device-side Lua API

All functions live on the global `bridge` table. They are injected only
inside a Lua plugin VM — scripts running elsewhere don't see them.

### `bridge.publish(topic, data)` → `boolean`

Enqueues a message for notification to the connected site. Returns
`false` if the outbound queue is full (4 slots) or the serialized payload
exceeds 480 bytes.

```lua
bridge.publish("doorbell/ring", { at = os.time(), rssi = -42 })
bridge.publish("step/counted", 1234)
bridge.publish("ping")                     -- no data
```

### `bridge.on(topic, fn)` → `true`

Registers a handler for incoming messages whose `topic` field matches
exactly. The handler receives the decoded `data` value (Lua table,
string, number, boolean, or nil). Only one handler per topic — re-
registering replaces the previous function.

```lua
bridge.on("text/import", function(data)
    writeFile("inbox.txt", data.content)
end)
```

### `bridge.connected()` → `boolean`

`true` if a Web-Bluetooth client is currently paired.

### `bridge.keep_awake(seconds)` → `nil`

Requests that auto-sleep be deferred for up to `seconds` (capped at
3600 = one hour). Each call extends the window to the later of the two
deadlines — shorter calls never cut an existing window short. Pass `0`
to clear immediately.

```lua
function update()
    if bridge.connected() then
        bridge.keep_awake(120)  -- renew every tick, 2-minute rolling window
    end
end
```

### `bridge.remaining_awake()` → `integer`

Seconds left in the current keep-awake window, or `0` if inactive.
Useful for rendering a "awake for N more seconds" indicator.

## Lifecycle

1. A Lua plugin is loaded by the Plugin Host state. `ble_bridge::init()`
   is called lazily — the service is idempotent, so multiple plugins
   sharing a session reuse the same server and advertising.
2. `registerInto(L)` injects the `bridge` table into the plugin's Lua
   state and installs an inbound dispatcher that routes messages to
   handlers registered via `bridge.on`.
3. Every main-loop iteration, `ble_bridge::process()` drains the
   outbound queue (one notify per tick) and dispatches any inbound
   messages to the active Lua state.
4. When the plugin exits, `LuaPlugin::cleanup()` clears the inbound
   handler and the keep-awake window — the next plugin starts fresh.
5. The BLE service itself stays advertising until the device reboots or
   another BLE subsystem tears it down. The site can reconnect without
   re-initialization.

## Safety & limits

- **Queue depth**: 4 inbound + 4 outbound messages. If either fills, new
  pushes drop with a `[BRIDGE]` warning on the serial log.
- **Payload size**: 480 bytes max per message (fits in one 512-byte MTU
  BLE notification with headers).
- **Thread safety**: BLE write callbacks run on the NimBLE task. They
  only enqueue; actual Lua dispatch happens on the main task from
  `ble_bridge::process()`.
- **Lua errors in handlers**: caught by a `pcall` wrapper and logged to
  serial. A broken handler doesn't crash the plugin.
- **Oversize publishes**: `bridge.publish` returns `false` silently; the
  plugin can surface an error itself if the payload is critical.
- **Sandbox inheritance**: the `bridge` API is in addition to, not
  instead of, the existing Lua sandbox. `writeFile`, `readFile`,
  drawing primitives, and time/battery/settings accessors continue to
  work exactly as before.

## Limitations (V1)

- The plugin only receives inbound messages while it is the **foreground
  plugin** (inside the Plugin Host state). Switching to Home, Reader,
  or Settings drops the dispatcher. Background service plugins are a V2
  feature — they need a separate long-lived Lua runtime.
- No request/response semantics. Everything is fire-and-forget pub/sub.
  If you need a response, have the other side publish a follow-up
  message on a different topic.
- No topic-based subscription filtering on the BLE layer. The device
  publishes every outbound message to every connected client. Filtering
  is done client-side by inspecting `topic`.

## See also

- [`PLUGIN_AUTHORING_PROMPT.md`](PLUGIN_AUTHORING_PROMPT.md) — the AI
  prompt template, with the full Lua plugin API inlined as context.
- [`BLE_FILE_TRANSFER.md`](BLE_FILE_TRANSFER.md) — the sibling service
  that moves complete files (books, plugins) between site and device.
- `src/ble/BleBridge.{h,cpp}` — implementation.
- `src/plugins/LuaBridgeBindings.h` — Lua binding layer.

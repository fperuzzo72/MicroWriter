#pragma once

/**
 * @file BleBridge.h
 * @brief Generic plugin <-> site messaging over BLE
 *
 * Provides a pair of BLE characteristics that Lua plugins on the device
 * can use to exchange JSON messages with a Web-Bluetooth client running
 * on sumi.page (or any other client that speaks the protocol).
 *
 *   Device -> Site   (TX, notify)      {"topic":"doorbell/ring","data":{...}}
 *   Site   -> Device (RX, write)       {"topic":"text/import","data":{...}}
 *
 * Wire format: one JSON object per BLE notification/write, UTF-8 encoded,
 * ideally <= 480 bytes so it fits inside a single negotiated ATT MTU (512
 * header + attribute overhead). Larger payloads are split by the sender
 * using an "id"+"seq"+"last" envelope; V1 just rejects oversized writes
 * and the sender is expected to chunk its own payload.
 *
 * Characteristics:
 *   SERVICE:   19B10100-E8F2-537E-4F6C-D104768A1214
 *   TX  (not): 19B10101-E8F2-537E-4F6C-D104768A1214   (device -> site)
 *   RX  (wr):  19B10102-E8F2-537E-4F6C-D104768A1214   (site -> device)
 *
 * Safety:
 *   - Inbound messages are queued in a small bounded ring buffer inside the
 *     BLE task context, then drained in process() on the main task where
 *     Lua is safe to call.
 *   - Outbound messages are queued the same way and drained in process()
 *     so plugin code never blocks on a BLE notify.
 *   - keepAwake() lets a plugin postpone auto-sleep for a bounded window
 *     (max 1 hour, renewable). main.cpp's sleep-decision path checks
 *     keepAwakeActive() and defers the state transition while it's set.
 */

#include "../config.h"

#if FEATURE_BLUETOOTH && FEATURE_PLUGINS

#include <Arduino.h>
#include <functional>

namespace ble_bridge {

// Max size of a single JSON message (bytes, excluding NUL). Reduced from
// 480 to 256 to recover ~1.8 KB of static BSS across the two ring queues.
static constexpr size_t MAX_MSG_SIZE = 256;

// Inbound messages delivered here. topicOut is filled with the "topic" field
// extracted from the envelope; jsonOut is the full JSON string (the handler
// is responsible for parsing/decoding data). Returns false if no message.
using InboundHandler = std::function<void(const char* topic, const char* json)>;

// Lifecycle -----------------------------------------------------------------
void init();
void deinit();
bool isReady();

// Connection / advertising --------------------------------------------------
bool isConnected();
void startAdvertising();
void stopAdvertising();

// Outbound queue ------------------------------------------------------------
// Enqueue a full JSON envelope for notification. Returns false if the queue
// is full or the message is over MAX_MSG_SIZE. Safe to call from any task.
bool publishRaw(const char* json, size_t len);

// Convenience: build {"topic":"<topic>","data":<dataJson>} and publishRaw().
// dataJson may be any valid JSON fragment (object, array, string, number,
// bool, null) or nullptr for a topic-only event.
bool publishEnveloped(const char* topic, const char* dataJson);

// Inbound handler -----------------------------------------------------------
// Set the callback invoked on each parsed inbound message. The callback
// runs on the main task from process(). Replaces any previous handler.
void setInboundHandler(InboundHandler handler);

// Drain queued inbound + outbound messages. Call every main-loop tick.
void process();

// Keep-awake ----------------------------------------------------------------
// Request that auto-sleep be deferred for `seconds` (capped at 3600). Each
// call replaces any previous deadline with the later of the two. Pass 0 to
// clear immediately.
void keepAwake(uint32_t seconds);

// True if the current deadline has not yet expired.
bool keepAwakeActive();

// How many seconds are left before the keep-awake window expires. Returns 0
// when inactive. Useful for UIs that want to surface "plugin is holding the
// device awake for <n> seconds".
uint32_t keepAwakeRemainingSeconds();

}  // namespace ble_bridge

#endif  // FEATURE_BLUETOOTH && FEATURE_PLUGINS

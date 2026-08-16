#include "BleBridge.h"

#if FEATURE_BLUETOOTH && FEATURE_PLUGINS

#include <NimBLEDevice.h>
#include <string.h>

namespace ble_bridge {

namespace {

// UUIDs (service = +0x100 offset from file transfer service)
constexpr const char* BRIDGE_SERVICE_UUID = "19B10100-E8F2-537E-4F6C-D104768A1214";
constexpr const char* BRIDGE_TX_UUID      = "19B10101-E8F2-537E-4F6C-D104768A1214";
constexpr const char* BRIDGE_RX_UUID      = "19B10102-E8F2-537E-4F6C-D104768A1214";

// Ring-buffer sizing. Two messages each way is plenty for a UI-driven
// plugin (one in flight + one queued). Halved from 4 to recover ~2 KB
// of static BSS on an already tight 380 KB DRAM budget.
constexpr size_t QUEUE_DEPTH = 2;

struct Message {
    uint16_t len;
    char     data[MAX_MSG_SIZE + 1];
};

struct MessageQueue {
    Message entries[QUEUE_DEPTH];
    volatile uint8_t head = 0;  // next read slot
    volatile uint8_t tail = 0;  // next write slot
    volatile uint8_t count = 0;
    // Per-queue spinlock. portMUX is the FreeRTOS-aware primitive: it
    // guards just this queue's mutation window (3 stores + a memcpy of
    // up to 256 B), whereas the previous noInterrupts()/interrupts()
    // pair globally masked *all* interrupts for the same window. NimBLE's
    // host-side RX timing is sensitive to interrupt latency, so trimming
    // the masked region from "the whole MCU" to "this object" matters
    // even on single-core ESP32-C3, where portENTER_CRITICAL still gates
    // IRQs above the configured threshold but only inside the spinlock
    // scope. Audit #30. portMUX_INITIALIZER_UNLOCKED works as a default
    // member initializer because portMUX_TYPE is a trivially-constructible
    // struct in ESP-IDF.
    portMUX_TYPE q_mux = portMUX_INITIALIZER_UNLOCKED;

    bool push(const char* src, size_t len) {
        if (len > MAX_MSG_SIZE) return false;
        // BLE task and main task can race. Accept a few dropped messages
        // under extreme pressure rather than take a heavier mutex on
        // every push — the queue mutation is bounded and very short.
        // RxCallbacks::onWrite runs on the NimBLE *host task* (not an
        // ISR), so the task-context macro is the right pick.
        portENTER_CRITICAL(&q_mux);
        if (count >= QUEUE_DEPTH) { portEXIT_CRITICAL(&q_mux); return false; }
        memcpy(entries[tail].data, src, len);
        entries[tail].data[len] = '\0';
        entries[tail].len = len;
        tail = (tail + 1) % QUEUE_DEPTH;
        count++;
        portEXIT_CRITICAL(&q_mux);
        return true;
    }

    bool pop(Message& out) {
        portENTER_CRITICAL(&q_mux);
        if (count == 0) { portEXIT_CRITICAL(&q_mux); return false; }
        out.len = entries[head].len;
        memcpy(out.data, entries[head].data, entries[head].len + 1);
        head = (head + 1) % QUEUE_DEPTH;
        count--;
        portEXIT_CRITICAL(&q_mux);
        return true;
    }
};

// State
bool _initialized = false;

NimBLEServer*         _server = nullptr;
NimBLEService*        _service = nullptr;
NimBLECharacteristic* _txChar = nullptr;
NimBLECharacteristic* _rxChar = nullptr;
NimBLEAdvertising*    _advertising = nullptr;

MessageQueue _outbound;
MessageQueue _inbound;

InboundHandler _inboundHandler = nullptr;

// Keep-awake state. Expressed as a millis() deadline; 0 = inactive.
volatile uint32_t _keepAwakeUntilMs = 0;

// ── Callbacks ───────────────────────────────────────────────────────────────
// As of audit #50 the BLE host callbacks are routed through a shared
// dispatcher in src/ble/BleHostCallbacks.cpp — see BleFileTransfer's
// init() for the install + subscribe. The bridge could subscribe too
// (e.g. to clear the inbound queue on disconnect) but currently uses a
// poll-based check via `_server->getConnectedCount() > 0` for the
// advertising watchdog and that's been adequate. If a future plugin
// needs an explicit on-disconnect hook, just subscribe via
// `ble_host::subscribe(...)`.

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo&) override {
        std::string v = ch->getValue();
        if (v.empty() || v.size() > MAX_MSG_SIZE) return;
        if (!_inbound.push(v.data(), v.size())) {
            Serial.println("[BRIDGE] inbound queue full, dropping message");
        }
    }
};

RxCallbacks _rxCallbacks;

// ── Lightweight JSON "topic" extractor ──────────────────────────────────────
//
// Walks the JSON envelope looking for a TOP-LEVEL `"topic": "<string>"`
// pair, with depth tracking and escape-aware string scanning. Was a
// brittle `strstr("\"topic\"", ...)` substring match before Batch 6 —
// that version mismatched on:
//   * `"topic"` keys nested inside `data` (e.g. `{"data":{"topic":"fake"}}`)
//   * topic values containing escaped `"` (truncated mid-string)
//   * data values whose payload contains the literal text `"topic":"…"`
// Audit #37.
//
// We intentionally do NOT pull ArduinoJson just to read one field on the
// BLE hot path — this stays under 90 lines and allocates nothing.
bool extractTopic(const char* json, char* out, size_t outLen) {
    if (!json || !out || outLen == 0) return false;
    out[0] = '\0';
    const char* p = json;

    // Skip leading whitespace + the opening `{`. extractTopic is only
    // ever called on a JSON object envelope.
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '{') return false;
    p++;

    // We're now at depth 1 (just inside the outer object). Walk
    // key:value pairs at this depth. Anything nested deeper is skipped
    // wholesale via skipValue() below.
    auto skipString = [](const char*& q) -> bool {
        // Caller has q pointing at the opening `"`. Advance past the
        // matching close, honouring `\"` and `\\` escapes.
        if (*q != '"') return false;
        q++;
        while (*q) {
            if (*q == '\\' && q[1]) { q += 2; continue; }
            if (*q == '"') { q++; return true; }
            q++;
        }
        return false;  // unterminated
    };
    auto skipValue = [&skipString](const char*& q) -> bool {
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        if (*q == '"') return skipString(q);
        if (*q == '{' || *q == '[') {
            int d = 0;
            bool inStr = false;
            while (*q) {
                if (inStr) {
                    if (*q == '\\' && q[1]) { q += 2; continue; }
                    if (*q == '"') inStr = false;
                    q++;
                    continue;
                }
                if (*q == '"') { inStr = true; q++; continue; }
                if (*q == '{' || *q == '[') { d++; q++; continue; }
                if (*q == '}' || *q == ']') { d--; q++; if (d == 0) return true; continue; }
                q++;
            }
            return false;
        }
        // Primitive (number, bool, null) — read until separator.
        while (*q && *q != ',' && *q != '}' && *q != ']') q++;
        return true;
    };

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
        if (*p == '}' || !*p) return false;
        if (*p != '"') return false;  // expected key
        const char* keyStart = p + 1;
        if (!skipString(p)) return false;
        const char* keyEndQuote = p - 1;  // points at closing `"`
        const size_t keyLen = (size_t)(keyEndQuote - keyStart);

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != ':') return false;
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

        const bool isTopic = (keyLen == 5) && (memcmp(keyStart, "topic", 5) == 0);
        if (!isTopic) {
            if (!skipValue(p)) return false;
            continue;
        }
        // Topic must be a string.
        if (*p != '"') return false;
        const char* valStart = p + 1;
        if (!skipString(p)) return false;
        const char* valEndQuote = p - 1;

        // Copy with minimal unescape: \" \\ \/ -> literal char; other
        // escapes left as-is (BLE bridge topics are ASCII identifiers in
        // practice).
        size_t oi = 0;
        for (const char* q = valStart; q < valEndQuote && oi + 1 < outLen; ) {
            if (*q == '\\' && q + 1 < valEndQuote) {
                const char nx = q[1];
                if (nx == '"' || nx == '\\' || nx == '/') {
                    out[oi++] = nx;
                    q += 2;
                    continue;
                }
            }
            out[oi++] = *q++;
        }
        out[oi] = '\0';
        return true;
    }
    return false;
}

// Append `s` to `out` as a JSON-escaped string body (no surrounding
// quotes — caller writes those). Returns true if the entire input fit.
// Audit #55.
bool appendJsonStringBody(char* out, size_t outCap, size_t* outIdx, const char* s) {
    for (const char* p = s; *p; p++) {
        const unsigned char c = (unsigned char)*p;
        size_t i = *outIdx;
        if (c == '"' || c == '\\') {
            if (i + 2 >= outCap) return false;
            out[i++] = '\\';
            out[i++] = (char)c;
        } else if (c == '\n') {
            if (i + 2 >= outCap) return false;
            out[i++] = '\\'; out[i++] = 'n';
        } else if (c == '\r') {
            if (i + 2 >= outCap) return false;
            out[i++] = '\\'; out[i++] = 'r';
        } else if (c == '\t') {
            if (i + 2 >= outCap) return false;
            out[i++] = '\\'; out[i++] = 't';
        } else if (c < 0x20) {
            // Other control chars → \u00XX
            if (i + 6 >= outCap) return false;
            const int n = snprintf(out + i, outCap - i, "\\u%04x", c);
            if (n != 6) return false;
            i += 6;
        } else {
            if (i + 1 >= outCap) return false;
            out[i++] = (char)c;
        }
        *outIdx = i;
    }
    return true;
}

}  // namespace

// ── Public API ──────────────────────────────────────────────────────────────

void init() {
    if (_initialized) return;

    // NimBLEDevice::init is idempotent in NimBLE-Arduino — a second call
    // with the same name is a no-op. Safe to call regardless of whether
    // another module (file transfer, HID) already brought the stack up.
    NimBLEDevice::init("SUMI");
    NimBLEDevice::setPower(3);

    // createServer() returns the existing singleton if one was already
    // created by BleFileTransfer, so the two services share a single
    // NimBLE server rather than fighting over it.
    _server = NimBLEDevice::createServer();
    // No setCallbacks() — see note in the Callbacks section above.

    _service = _server->createService(BRIDGE_SERVICE_UUID);

    _rxChar = _service->createCharacteristic(
        BRIDGE_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _rxChar->setCallbacks(&_rxCallbacks);

    _txChar = _service->createCharacteristic(
        BRIDGE_TX_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    _txChar->setValue("{\"state\":\"idle\"}");

    _service->start();

    _advertising = NimBLEDevice::getAdvertising();
    _advertising->setName("SUMI");
    _advertising->addServiceUUID(BRIDGE_SERVICE_UUID);

    _initialized = true;
    Serial.println("[BRIDGE] service started");
}

void deinit() {
    if (!_initialized) return;
    stopAdvertising();
    _inbound.count = _inbound.head = _inbound.tail = 0;
    _outbound.count = _outbound.head = _outbound.tail = 0;
    _inboundHandler = nullptr;
    _keepAwakeUntilMs = 0;
    _initialized = false;
    // Deliberately do NOT NimBLEDevice::deinit() here — other services
    // (file transfer) may still be using the stack. Let main own lifecycle.
}

bool isReady()     { return _initialized; }

bool isConnected() {
    // Query the server directly. Pre-audit-#50 this was load-bearing —
    // we couldn't install our own NimBLEServerCallbacks because
    // file-transfer's would lose. Now the BleHostCallbacks fan-out
    // means we *could* subscribe for events; we keep the poll because
    // it's adequate and avoids carrying our own connection-state flag.
    return _server && _server->getConnectedCount() > 0;
}

void startAdvertising() {
    if (_advertising) _advertising->start();
}

void stopAdvertising() {
    if (_advertising) _advertising->stop();
}

bool publishRaw(const char* json, size_t len) {
    if (!_initialized || !json || len == 0 || len > MAX_MSG_SIZE) return false;
    return _outbound.push(json, len);
}

bool publishEnveloped(const char* topic, const char* dataJson) {
    if (!topic || !*topic) return false;
    char buf[MAX_MSG_SIZE + 1];
    size_t idx = 0;

    // Build {"topic":"<escaped>" — escape needed so a topic with `"` or
    // `\` doesn't produce malformed JSON the receiver fails to parse
    // (audit #55). Pre-Batch-6 used snprintf %s straight in.
    static constexpr const char kPrefix[] = "{\"topic\":\"";
    if (sizeof(kPrefix) - 1 >= sizeof(buf)) return false;
    memcpy(buf, kPrefix, sizeof(kPrefix) - 1);
    idx = sizeof(kPrefix) - 1;
    if (!appendJsonStringBody(buf, sizeof(buf), &idx, topic)) return false;

    if (dataJson && *dataJson) {
        // ","data":<dataJson>}  — dataJson is assumed valid JSON; the
        // sender (plugin) is responsible for it being well-formed. We
        // pass it through to keep the bridge transparent.
        static constexpr const char kMid[] = "\",\"data\":";
        if (idx + sizeof(kMid) - 1 + 1 >= sizeof(buf)) return false;
        memcpy(buf + idx, kMid, sizeof(kMid) - 1);
        idx += sizeof(kMid) - 1;
        const size_t dlen = strlen(dataJson);
        if (idx + dlen + 1 + 1 >= sizeof(buf)) return false;  // +1 for '}', +1 for NUL
        memcpy(buf + idx, dataJson, dlen);
        idx += dlen;
        buf[idx++] = '}';
    } else {
        if (idx + 2 + 1 >= sizeof(buf)) return false;  // "}\0
        buf[idx++] = '"';
        buf[idx++] = '}';
    }
    buf[idx] = '\0';
    return publishRaw(buf, idx);
}

void setInboundHandler(InboundHandler handler) {
    _inboundHandler = handler;
}

void process() {
    if (!_initialized) return;

    // Advertising watchdog: NimBLE stops advertising on client connect and
    // doesn't auto-resume on disconnect. File-transfer's onDisconnect
    // handler normally restarts advertising, but if file-transfer isn't
    // active, the bridge needs to do it itself. Checked lazily — cheap,
    // idempotent, and only matters when we're not currently connected.
    static uint32_t lastAdCheckMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastAdCheckMs > 2000) {
        lastAdCheckMs = nowMs;
        if (_advertising && _server && _server->getConnectedCount() == 0 &&
            !_advertising->isAdvertising()) {
            _advertising->start();
        }
    }

    // Drain outbound: one notify per tick keeps the BLE stack happy under
    // backpressure. More than that risks dropped notifications.
    const bool connected = _server && _server->getConnectedCount() > 0;
    if (connected && _txChar) {
        Message msg;
        if (_outbound.pop(msg)) {
            _txChar->setValue((uint8_t*)msg.data, msg.len);
            _txChar->notify();
        }
    }

    // Drain inbound: dispatch to registered handler.
    Message inMsg;
    while (_inbound.pop(inMsg)) {
        if (!_inboundHandler) continue;
        char topic[64];
        if (!extractTopic(inMsg.data, topic, sizeof(topic))) {
            Serial.println("[BRIDGE] inbound message missing 'topic'");
            continue;
        }
        _inboundHandler(topic, inMsg.data);
    }

    // Auto-clear expired keep-awake window.
    if (_keepAwakeUntilMs != 0 &&
        (int32_t)(_keepAwakeUntilMs - millis()) <= 0) {
        _keepAwakeUntilMs = 0;
    }
}

void keepAwake(uint32_t seconds) {
    if (seconds == 0) { _keepAwakeUntilMs = 0; return; }
    if (seconds > 3600) seconds = 3600;  // Cap at 1 hour per call
    uint32_t deadline = millis() + seconds * 1000UL;
    // Only extend; shorter calls don't cut an existing window early.
    if ((int32_t)(deadline - _keepAwakeUntilMs) > 0) {
        _keepAwakeUntilMs = deadline;
    }
}

bool keepAwakeActive() {
    if (_keepAwakeUntilMs == 0) return false;
    return (int32_t)(_keepAwakeUntilMs - millis()) > 0;
}

uint32_t keepAwakeRemainingSeconds() {
    if (!keepAwakeActive()) return 0;
    return (_keepAwakeUntilMs - millis()) / 1000UL;
}

}  // namespace ble_bridge

#endif  // FEATURE_BLUETOOTH && FEATURE_PLUGINS

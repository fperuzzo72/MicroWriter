#include "BleKeyboard.h"

#include <HalStorage.h>
#include <Logging.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

// HID service and characteristic UUIDs
static NimBLEUUID hidServiceUUID("1812");
static NimBLEUUID reportUUID("2a4d");
static NimBLEUUID protocolModeUUID("2a4e");
static NimBLEUUID bootKeyboardInUUID("2a22");

// Module state
static NimBLEClient* pClient = nullptr;
static NimBLERemoteService* pRemoteService = nullptr;
static NimBLERemoteCharacteristic* pInputReportChar = nullptr;

static BLEState bleState = BLEState::DISCONNECTED;
static bool connectToKeyboard = false;
static std::string keyboardAddress = "";
static uint8_t keyboardAddressType = 0;
static uint8_t lastReport[8] = {0};
static uint8_t inputReportId = 0;  // Non-zero if keyboard prefixes reports with a report ID

// NVS storage for persistent pairing
static Preferences prefs;

bool autoReconnectEnabled = true;

// Reconnection backoff
static unsigned long reconnectDelay = 5000;  // 5s initial — gives keyboard time to release old connection after deep sleep
static unsigned long lastReconnectAttempt = 0;
static constexpr unsigned long MAX_RECONNECT_DELAY = 120000;  // Cap at 2min

// Multi-keyboard cycling: index of the keyboard to try on the next auto-reconnect attempt
static int reconnectKeyboardIndex = 0;

// Activity tracking (used by connect task to initialise idle timer)
static unsigned long lastBleKeystrokeMs = 0;
static bool bleConnIdleMode = false;

// Device discovery variables
static std::vector<BleDeviceInfo> discoveredDevices;
static bool isScanning = false;
static uint32_t scanStartMs = 0;
static constexpr uint32_t DEVICE_STALE_MS = 10000;

// FreeRTOS connect task
static TaskHandle_t connectTaskHandle = nullptr;
static volatile bool authSuccess = false;

static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;

static uint32_t currentPasskey = 0;

// Forward declarations
static bool setupHidConnection();
int getLastUsedKeyboardIndex();

// --- Minimal key event queue ---
// Phase 1: bleLoop()'s caller can drain this to verify pairing/typing over
// serial. Real routing (menu nav, reader, writer) is a later phase.
static constexpr int KEY_QUEUE_SIZE = 32;
static BleKeyEvent keyQueue[KEY_QUEUE_SIZE];
static volatile int keyQueueHead = 0;
static volatile int keyQueueTail = 0;
static volatile bool keyQueueFull = false;

static void enqueueKeyEvent(uint8_t keyCode, uint8_t modifiers, bool pressed) {
  noInterrupts();
  if (!keyQueueFull) {
    keyQueue[keyQueueHead] = {keyCode, modifiers, pressed};
    keyQueueHead = (keyQueueHead + 1) % KEY_QUEUE_SIZE;
    if (keyQueueHead == keyQueueTail) keyQueueFull = true;
  }
  interrupts();
}

bool popBleKeyEvent(BleKeyEvent& out) {
  bool got = false;
  noInterrupts();
  if (keyQueueHead != keyQueueTail || keyQueueFull) {
    out = keyQueue[keyQueueTail];
    keyQueueTail = (keyQueueTail + 1) % KEY_QUEUE_SIZE;
    keyQueueFull = false;
    got = true;
  }
  interrupts();
  return got;
}

// Helper: upsert device into discovered list
static void upsertDevice(const BleDeviceInfo& info) {
  for (auto& d : discoveredDevices) {
    if (d.address == info.address) {
      d = info;
      return;
    }
  }
  discoveredDevices.push_back(info);
}

// Keyboard notification callback
static void onKeyboardNotify(NimBLERemoteCharacteristic* pRemChar, uint8_t* pData, size_t length, bool isNotify) {
  // Strip report ID prefix if the keyboard uses one (learned during HID discovery).
  if (inputReportId != 0 && length > 0 && pData[0] == inputReportId) {
    pData++;
    length--;
  }

  // Must be 7 or 8 bytes after stripping. Consumer-control / media-key reports
  // from other report IDs arrive here too — silently ignore them.
  if (length < 7 || length > 8) {
    LOG_DBG("BLE", "Ignoring %d-byte report (not keyboard format)", (int)length);
    return;
  }

  uint8_t modifiers = pData[0];
  uint8_t newReport[8] = {0};

  // Normalize to 8-byte format: [Mod] [Reserved=0] [Key1-Key6]
  if (length == 8) {
    memcpy(newReport, pData, 8);
  } else {
    newReport[0] = pData[0];
    newReport[1] = 0;
    memcpy(&newReport[2], &pData[1], 6);
  }

  // Detect newly pressed keys (bytes 2-7 in normalized format)
  for (int i = 2; i < 8; i++) {
    if (newReport[i] == 0) continue;
    bool wasPressed = false;
    for (int j = 2; j < 8; j++) {
      if (lastReport[j] == newReport[i]) {
        wasPressed = true;
        break;
      }
    }
    if (!wasPressed) {
      LOG_DBG("BLE", "KEY PRESS: 0x%02X mod=0x%02X", newReport[i], modifiers);
      enqueueKeyEvent(newReport[i], modifiers, true);
    }
  }

  // Detect released keys
  for (int i = 2; i < 8; i++) {
    if (lastReport[i] == 0) continue;
    bool stillPressed = false;
    for (int j = 2; j < 8; j++) {
      if (newReport[j] == lastReport[i]) {
        stillPressed = true;
        break;
      }
    }
    if (!stillPressed) {
      LOG_DBG("BLE", "KEY RELEASE: 0x%02X", lastReport[i]);
      enqueueKeyEvent(lastReport[i], modifiers, false);
    }
  }

  memcpy(lastReport, newReport, 8);
  lastBleKeystrokeMs = millis();
}

// --- Callbacks (static instances, no heap allocation) ---

static class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    BleDeviceInfo info;
    info.address = dev->getAddress().toString();
    info.name = dev->haveName() ? dev->getName() : info.address;
    info.rssi = dev->getRSSI();
    info.addressType = dev->getAddress().getType();
    info.lastSeenMs = millis();
    upsertDevice(info);
  }
} scanCallbacks;

static class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pclient) override {
    LOG_DBG("BLE", "Connected to device");
    // Don't call secureConnection() here - the connect task handles it
  }

  void onDisconnect(NimBLEClient* pclient, int reason) override {
    bleState = BLEState::DISCONNECTED;
    pInputReportChar = nullptr;
    pRemoteService = nullptr;
    authSuccess = false;
    memset(lastReport, 0, 8);
    lastReconnectAttempt = millis();
    LOG_INF("BLE", "Disconnected (reason=%d)", reason);
  }

  bool onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params) override {
    // Reject the keyboard's connection parameter update request.
    // Keychron keyboards send a conn param update on the first keypress (idle->active
    // interval switch). Returning true makes NimBLE substitute our m_connParams into
    // the response, which mismatches what the Keychron expects and causes an immediate
    // crash/disconnect. Returning false tells the keyboard to keep the current params
    // (30-50 ms interval negotiated at connect time), which works fine for all keyboards.
    return false;
  }

  void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
    LOG_DBG("BLE", "PassKeyEntry received - entering 123456");
    NimBLEDevice::injectPassKey(connInfo, 123456);
  }

  void onConfirmPasskey(NimBLEConnInfo& connInfo, uint32_t pin) override {
    LOG_DBG("BLE", "Confirm passkey: %06lu - auto-accepting", (unsigned long)pin);
    currentPasskey = pin;
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    LOG_INF("BLE", "Auth complete: encrypted=%d bonded=%d", connInfo.isEncrypted(), connInfo.isBonded());
    authSuccess = connInfo.isEncrypted();
    currentPasskey = 0;
  }
} clientCallbacks;

// --- HID service discovery and subscription ---

static bool setupHidConnection() {
  if (!pClient || !pClient->isConnected()) return false;

  LOG_DBG("BLE", "Discovering services...");
  if (pClient->getServices(true).empty()) {
    LOG_ERR("BLE", "Service discovery failed");
    return false;
  }

  pRemoteService = pClient->getService(hidServiceUUID);
  if (!pRemoteService) {
    LOG_ERR("BLE", "HID service not found");
    return false;
  }

  // Set report protocol mode FIRST (before subscribing)
  NimBLERemoteCharacteristic* pProto = pRemoteService->getCharacteristic(protocolModeUUID);
  if (pProto) {
    uint8_t mode = 1;  // 1 = Report Protocol
    pProto->writeValue(&mode, 1, true);
    LOG_DBG("BLE", "Set Protocol Mode to Report Protocol (1)");
  } else {
    LOG_DBG("BLE", "WARNING: No Protocol Mode characteristic found");
  }

  // Find input report via Report Reference descriptor (type=1 means Input)
  pInputReportChar = nullptr;
  inputReportId = 0;
  const auto& chars = pRemoteService->getCharacteristics(true);
  LOG_DBG("BLE", "Found %d characteristics in HID service", (int)chars.size());

  for (auto& chr : chars) {
    if (chr->getUUID() != reportUUID) continue;

    const auto& descs = chr->getDescriptors();
    for (auto& d : descs) {
      if (d->getUUID() == NimBLEUUID("2908")) {
        NimBLEAttValue refData = d->readValue();
        if (refData.size() >= 2 && (uint8_t)refData[1] == 1) {
          pInputReportChar = chr;
          inputReportId = (uint8_t)refData[0];
          LOG_DBG("BLE", "Selected input report (reportId=%d)", inputReportId);
          break;
        }
      }
    }
    if (pInputReportChar) break;
  }

  // Fallback: subscribe to ALL notifiable report chars to find keyboard input
  if (!pInputReportChar) {
    LOG_DBG("BLE", "No report ref found, subscribing to ALL notifiable report chars");
    int reportCount = 0;
    for (auto& chr : chars) {
      if (chr->getUUID() == reportUUID && chr->canNotify()) {
        if (chr->subscribe(true, onKeyboardNotify)) {
          reportCount++;
          if (reportCount == 1) pInputReportChar = chr;
        }
      }
    }
    if (reportCount == 0) {
      LOG_ERR("BLE", "Failed to subscribe to any report characteristics!");
    }
  }

  // Fallback: boot keyboard input
  if (!pInputReportChar) {
    LOG_DBG("BLE", "No report char found, trying boot keyboard input");
    pInputReportChar = pRemoteService->getCharacteristic(bootKeyboardInUUID);
  }

  if (!pInputReportChar) {
    LOG_ERR("BLE", "No input report found");
    return false;
  }

  if (pInputReportChar->getUUID() != reportUUID || !pInputReportChar->canNotify()) {
    if (!pInputReportChar->subscribe(true, onKeyboardNotify)) {
      LOG_ERR("BLE", "Subscribe failed");
      return false;
    }
  }

  LOG_INF("BLE", "HID setup complete");
  return true;
}

// --- FreeRTOS task: runs connect + security + HID setup off the main loop ---

static void bleConnectTask(void* param) {
  bleState = BLEState::CONNECTING;
  authSuccess = false;

  LOG_DBG("BLE", "Connecting to %s type=%d", keyboardAddress.c_str(), keyboardAddressType);

  // Guard: if the link layer is already up (bleState flipped to DISCONNECTED spuriously
  // while the BLE connection is still alive), don't call connect() again — that crashes
  // the NimBLE stack. Just re-sync state and exit.
  if (pClient && pClient->isConnected()) {
    bleState = BLEState::CONNECTED;
    lastBleKeystrokeMs = millis();
    bleConnIdleMode = false;
    connectTaskHandle = nullptr;
    vTaskDelete(NULL);
    return;
  }

  if (!pClient) {
    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(&clientCallbacks, false);
  }
  pClient->setConnectTimeout(CONNECT_TIMEOUT_MS);

  NimBLEAddress addr(keyboardAddress, keyboardAddressType);
  // Delete any stored bond before connecting — forces a fresh "Just Works" pairing
  // instead of an encrypted reconnect. Prevents a NimBLE security-state crash when
  // the keyboard still holds a stale connection from a previous unclean disconnect.
  NimBLEDevice::deleteBond(addr);
  if (!pClient->connect(addr, true)) {
    LOG_ERR("BLE", "Connection failed");
    bleState = BLEState::DISCONNECTED;
    connectTaskHandle = nullptr;
    vTaskDelete(NULL);
    return;
  }

  LOG_DBG("BLE", "Connected, attempting security...");

  bool secureAttempted = pClient->secureConnection();
  if (secureAttempted) {
    unsigned long secStart = millis();
    while (!authSuccess && (millis() - secStart < 5000)) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    LOG_DBG("BLE", authSuccess ? "Security succeeded" : "Security failed/timeout - trying HID anyway");
  } else {
    LOG_DBG("BLE", "secureConnection() returned false - trying HID anyway");
  }

  if (!setupHidConnection()) {
    LOG_ERR("BLE", "HID setup failed, disconnecting");
    if (pClient->isConnected()) pClient->disconnect();
    bleState = BLEState::DISCONNECTED;
    connectTaskHandle = nullptr;
    vTaskDelete(NULL);
    return;
  }

  // Mark connected immediately so bleLoop doesn't start a second connect task.
  bleState = BLEState::CONNECTED;
  reconnectDelay = 5000;
  bleConnIdleMode = false;
  lastBleKeystrokeMs = millis();
  LOG_INF("BLE", "Keyboard ready!");

  // NOTE: updateConnParams() is intentionally NOT called here — see MicroSlate's
  // original comment (NOTICE.md): triggers a reentrancy crash with Keychron
  // keyboards right after connect.

  // If the device didn't broadcast a name during scanning, read it from GATT now.
  {
    NimBLERemoteService* gasSvc = pClient->getService(NimBLEUUID((uint16_t)0x1800));
    if (gasSvc) {
      NimBLERemoteCharacteristic* nameChr = gasSvc->getCharacteristic(NimBLEUUID((uint16_t)0x2A00));
      if (nameChr) {
        std::string gattName = nameChr->readValue();
        if (!gattName.empty()) {
          for (auto& d : discoveredDevices) {
            if (d.address == keyboardAddress && d.name == keyboardAddress) {
              d.name = gattName;
              break;
            }
          }
        }
      }
    }
  }

  // Store device to NVS for future reconnects, then reset cycling index to this keyboard
  {
    std::string devName = keyboardAddress;
    for (auto& d : discoveredDevices) {
      if (d.address == keyboardAddress) {
        devName = d.name;
        break;
      }
    }
    storePairedDevice(keyboardAddress, devName);
    int newLastKb = getLastUsedKeyboardIndex();
    reconnectKeyboardIndex = (newLastKb >= 0) ? newLastKb : 0;
  }

  connectTaskHandle = nullptr;
  vTaskDelete(NULL);
}

static void startConnectTask() {
  if (connectTaskHandle != nullptr) return;
  // 20480 bytes: Logitech has a complex service tree (keyboard + media + battery reports
  // + many descriptors). NimBLE 2.x uses more per-call stack than 1.4.x — 12288 was
  // sufficient before but overflows during full service discovery on the Logitech.
  xTaskCreate(bleConnectTask, "ble_conn", 20480, NULL, 1, &connectTaskHandle);
}

// --- NVS multi-keyboard helpers ---

static void nvs_kbKey(char* buf, int idx, const char* suffix) { snprintf(buf, 16, "kb_%d_%s", idx, suffix); }

static int nvs_loadCount() { return (int)prefs.getUChar("kb_count", 0); }

static std::string nvs_loadAddr(int idx) {
  char key[16];
  nvs_kbKey(key, idx, "addr");
  return std::string(prefs.getString(key, "").c_str());
}

static std::string nvs_loadName(int idx) {
  char key[16];
  nvs_kbKey(key, idx, "name");
  return std::string(prefs.getString(key, "").c_str());
}

static uint8_t nvs_loadType(int idx) {
  char key[16];
  nvs_kbKey(key, idx, "type");
  return prefs.getUChar(key, 0);
}

static void nvs_saveKb(int idx, const std::string& addr, const std::string& name, uint8_t type) {
  char key[16];
  nvs_kbKey(key, idx, "addr");
  prefs.putString(key, addr.c_str());
  nvs_kbKey(key, idx, "name");
  prefs.putString(key, name.c_str());
  nvs_kbKey(key, idx, "type");
  prefs.putUChar(key, type);
}

static void nvs_clearSlot(int idx) {
  char key[16];
  nvs_kbKey(key, idx, "addr");
  prefs.remove(key);
  nvs_kbKey(key, idx, "name");
  prefs.remove(key);
  nvs_kbKey(key, idx, "type");
  prefs.remove(key);
}

// --- SD card backup for BLE pairing (NVS can be wiped by a firmware flash) ---

static constexpr char BLE_BACKUP_PATH[] = "/microwriter/ble_kb.json";

static void jsonAppendStr(char* buf, size_t bufSize, const char* src) {
  size_t pos = strlen(buf);
  while (*src && pos < bufSize - 2) {
    if (*src == '"' || *src == '\\') buf[pos++] = '\\';
    buf[pos++] = *src++;
  }
  buf[pos] = '\0';
}

static int jsonGetInt(const char* json, const char* key) {
  char needle[32];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char* p = strstr(json, needle);
  if (!p) return -1;
  p += strlen(needle);
  while (*p == ' ') p++;
  return atoi(p);
}

static bool jsonGetStr(const char* json, const char* key, char* out, size_t outSize) {
  char needle[32];
  snprintf(needle, sizeof(needle), "\"%s\":\"", key);
  const char* p = strstr(json, needle);
  if (!p) return false;
  p += strlen(needle);
  size_t i = 0;
  while (*p && i < outSize - 1) {
    if (*p == '"') break;
    if (*p == '\\' && *(p + 1)) {
      p++;
      switch (*p) {
        case '"': out[i++] = '"'; break;
        case '\\': out[i++] = '\\'; break;
        case 'n': out[i++] = '\n'; break;
        default: out[i++] = *p; break;
      }
    } else {
      out[i++] = *p;
    }
    p++;
  }
  out[i] = '\0';
  return true;
}

static void writeBleBackup() {
  static char buf[512];
  int count = nvs_loadCount();
  int last = (int)prefs.getUChar("last_kb", 0);
  snprintf(buf, sizeof(buf), "{\"count\":%d,\"last\":%d", count, last);
  for (int i = 0; i < count; i++) {
    std::string addr = nvs_loadAddr(i);
    std::string name = nvs_loadName(i);
    uint8_t type = nvs_loadType(i);
    char tmp[16];
    snprintf(tmp, sizeof(tmp), ",\"a%d\":\"", i);
    strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
    jsonAppendStr(buf, sizeof(buf), addr.c_str());
    strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
    snprintf(tmp, sizeof(tmp), ",\"n%d\":\"", i);
    strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
    jsonAppendStr(buf, sizeof(buf), name.c_str());
    strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
    snprintf(tmp, sizeof(tmp), ",\"t%d\":%d", i, type);
    strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
  }
  strncat(buf, "}", sizeof(buf) - strlen(buf) - 1);
  Storage.ensureDirectoryExists("/microwriter");
  Storage.writeFile(BLE_BACKUP_PATH, String(buf));
}

static void restoreBleBackup() {
  static char buf[512];
  size_t n = Storage.readFileToBuffer(BLE_BACKUP_PATH, buf, sizeof(buf));
  if (n == 0) return;
  int count = jsonGetInt(buf, "count");
  int last = jsonGetInt(buf, "last");
  if (count < 0) return;
  for (int i = 0; i < count && i < MAX_PAIRED_KEYBOARDS; i++) {
    char aKey[8], nKey[8], tKey[8];
    snprintf(aKey, sizeof(aKey), "a%d", i);
    snprintf(nKey, sizeof(nKey), "n%d", i);
    snprintf(tKey, sizeof(tKey), "t%d", i);
    char addr[32] = "", name[64] = "";
    jsonGetStr(buf, aKey, addr, sizeof(addr));
    jsonGetStr(buf, nKey, name, sizeof(name));
    int type = jsonGetInt(buf, tKey);
    if (addr[0]) nvs_saveKb(i, addr, name, (uint8_t)(type >= 0 ? type : 0));
  }
  prefs.putUChar("kb_count", (uint8_t)count);
  if (last >= 0) prefs.putUChar("last_kb", (uint8_t)last);
  LOG_INF("BLE", "Restored %d paired keyboard(s) from SD backup", count);
}

// --- Public API ---

uint32_t getCurrentPasskey() { return currentPasskey; }

void bleSetup() {
  NimBLEDevice::init("MicroWriter");
  // bond=true, MITM=false (we don't require it), SC=false (legacy compat for Logitech etc.)
  NimBLEDevice::setSecurityAuth(true, false, false);
  // NO_INPUT_OUTPUT = "Just Works" pairing — works for both Logitech and Keychron
  // since MITM=false means we never require authenticated pairing regardless of IO cap.
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  // ENC-only key distribution — omit ID (IRK) which some keyboards don't support.
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC);
  NimBLEDevice::setPower(-9);  // -9dBm — lowest verified working power level

  prefs.begin("ble_kb", false);

  // Restore from SD backup if NVS was wiped (e.g. after a firmware flash)
  if (!prefs.isKey("kb_count")) restoreBleBackup();

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, true);
  scan->setInterval(1349);
  scan->setWindow(449);
  scan->setActiveScan(true);

  int pairedCount = nvs_loadCount();
  int lastKb = (pairedCount > 0) ? (int)prefs.getUChar("last_kb", 0) : -1;
  if (lastKb >= pairedCount) lastKb = 0;
  reconnectKeyboardIndex = (lastKb >= 0) ? lastKb : 0;

  if (pairedCount > 0 && lastKb >= 0) {
    keyboardAddress = nvs_loadAddr(lastKb);
    keyboardAddressType = nvs_loadType(lastKb);
    lastReconnectAttempt = millis();
    LOG_INF("BLE", "Will reconnect to: %s (in %lums, %d paired total)", keyboardAddress.c_str(), reconnectDelay,
            pairedCount);
  } else {
    bleState = BLEState::DISCONNECTED;
    LOG_INF("BLE", "No paired keyboards");
  }
}

void bleLoop() {
  if (isScanning && !NimBLEDevice::getScan()->isScanning()) {
    isScanning = false;
    LOG_DBG("BLE", "Scan complete - found %d devices", (int)discoveredDevices.size());
  }

  if (connectToKeyboard && bleState != BLEState::CONNECTED && connectTaskHandle == nullptr) {
    connectToKeyboard = false;
    startConnectTask();
    return;
  }

  // Auto-reconnect: cycle through all paired keyboards with exponential backoff.
  if (bleState == BLEState::DISCONNECTED && autoReconnectEnabled && connectTaskHandle == nullptr) {
    int count = nvs_loadCount();
    if (count > 0) {
      unsigned long now = millis();
      if (now - lastReconnectAttempt >= reconnectDelay) {
        int idx = reconnectKeyboardIndex % count;
        keyboardAddress = nvs_loadAddr(idx);
        keyboardAddressType = nvs_loadType(idx);
        connectToKeyboard = true;
        lastReconnectAttempt = now;
        LOG_DBG("BLE", "Auto-reconnect: trying keyboard %d/%d (%s)", idx + 1, count, keyboardAddress.c_str());
        reconnectKeyboardIndex = (reconnectKeyboardIndex + 1) % count;
        if (reconnectKeyboardIndex == 0) {
          reconnectDelay = (reconnectDelay * 2 > MAX_RECONNECT_DELAY) ? MAX_RECONNECT_DELAY : reconnectDelay * 2;
        }
      }
    }
  }
}

bool isKeyboardConnected() { return bleState == BLEState::CONNECTED; }

BLEState getConnectionState() { return bleState; }

void cancelPendingConnection() {
  connectToKeyboard = false;
  if (bleState == BLEState::CONNECTING && connectTaskHandle == nullptr) {
    bleState = BLEState::DISCONNECTED;
  }
}

void startDeviceScan() {
  cancelPendingConnection();
  NimBLEDevice::getScan()->stop();
  discoveredDevices.clear();
  NimBLEDevice::getScan()->clearResults();

  scanStartMs = millis();
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, true);
  scan->setActiveScan(true);
  scan->start(5000, false);  // One-shot 5-second scan

  isScanning = true;
  LOG_INF("BLE", "Started one-shot scan (5s)");
}

void stopDeviceScan() {
  NimBLEDevice::getScan()->stop();
  isScanning = false;
}

int getDiscoveredDeviceCount() { return discoveredDevices.size(); }

BleDeviceInfo* getDiscoveredDevices() { return discoveredDevices.empty() ? nullptr : discoveredDevices.data(); }

void connectToDevice(int deviceIndex) {
  if (deviceIndex < 0 || deviceIndex >= (int)discoveredDevices.size()) {
    LOG_ERR("BLE", "Invalid device index");
    return;
  }
  stopDeviceScan();
  if (pClient && pClient->isConnected()) pClient->disconnect();

  keyboardAddress = discoveredDevices[deviceIndex].address;
  keyboardAddressType = discoveredDevices[deviceIndex].addressType;
  connectToKeyboard = true;
  LOG_INF("BLE", "Will connect to: %s (%s)", keyboardAddress.c_str(), discoveredDevices[deviceIndex].name.c_str());
}

void disconnectCurrentDevice() {
  if (pClient && pClient->isConnected()) pClient->disconnect();
  bleState = BLEState::DISCONNECTED;
  pInputReportChar = nullptr;
  pRemoteService = nullptr;
  memset(lastReport, 0, 8);
  lastReconnectAttempt = millis();
  keyboardAddress = "";
}

std::string getCurrentDeviceAddress() { return keyboardAddress; }

// --- Multi-keyboard public API ---

int getPairedKeyboardCount() { return nvs_loadCount(); }

bool getPairedKeyboard(int index, std::string& addr, std::string& name, uint8_t& addrType) {
  if (index < 0 || index >= nvs_loadCount()) return false;
  addr = nvs_loadAddr(index);
  name = nvs_loadName(index);
  addrType = nvs_loadType(index);
  return !addr.empty();
}

int getLastUsedKeyboardIndex() {
  int count = nvs_loadCount();
  if (count == 0) return -1;
  int last = (int)prefs.getUChar("last_kb", 0);
  return (last < count) ? last : 0;
}

bool removePairedKeyboard(int index) {
  int count = nvs_loadCount();
  if (index < 0 || index >= count) return false;

  std::string addr = nvs_loadAddr(index);
  if (!addr.empty()) {
    NimBLEDevice::deleteBond(NimBLEAddress(addr, nvs_loadType(index)));
  }

  for (int i = index; i < count - 1; i++) {
    nvs_saveKb(i, nvs_loadAddr(i + 1), nvs_loadName(i + 1), nvs_loadType(i + 1));
  }
  nvs_clearSlot(count - 1);
  prefs.putUChar("kb_count", (uint8_t)(count - 1));

  int lastKb = (int)prefs.getUChar("last_kb", 0);
  if (count - 1 == 0) {
    prefs.remove("last_kb");
  } else if (lastKb >= count - 1) {
    prefs.putUChar("last_kb", 0);
  } else if (lastKb > index) {
    prefs.putUChar("last_kb", (uint8_t)(lastKb - 1));
  }

  if (keyboardAddress == addr) {
    if (pClient && pClient->isConnected()) pClient->disconnect();
    keyboardAddress = "";
  }

  reconnectKeyboardIndex = 0;
  LOG_INF("BLE", "Removed paired keyboard slot %d (%s)", index, addr.c_str());
  writeBleBackup();
  return true;
}

void connectToPairedKeyboard(int index) {
  int count = nvs_loadCount();
  if (index < 0 || index >= count) return;

  stopDeviceScan();
  if (pClient && pClient->isConnected()) pClient->disconnect();

  keyboardAddress = nvs_loadAddr(index);
  keyboardAddressType = nvs_loadType(index);
  prefs.putUChar("last_kb", (uint8_t)index);
  reconnectKeyboardIndex = (count > 1) ? (index + 1) % count : 0;
  reconnectDelay = 5000;
  connectToKeyboard = true;
  LOG_INF("BLE", "Switching to paired keyboard %d: %s", index, keyboardAddress.c_str());
  writeBleBackup();
}

void storePairedDevice(const std::string& address, const std::string& name) {
  int count = nvs_loadCount();

  for (int i = 0; i < count; i++) {
    if (nvs_loadAddr(i) == address) {
      nvs_saveKb(i, address, name, keyboardAddressType);
      prefs.putUChar("last_kb", (uint8_t)i);
      writeBleBackup();
      return;
    }
  }

  int idx;
  if (count < MAX_PAIRED_KEYBOARDS) {
    idx = count;
    prefs.putUChar("kb_count", (uint8_t)(count + 1));
  } else {
    // List full: evict slot 0 (oldest), shift everything down
    for (int i = 0; i < MAX_PAIRED_KEYBOARDS - 1; i++) {
      nvs_saveKb(i, nvs_loadAddr(i + 1), nvs_loadName(i + 1), nvs_loadType(i + 1));
    }
    idx = MAX_PAIRED_KEYBOARDS - 1;
  }

  nvs_saveKb(idx, address, name, keyboardAddressType);
  prefs.putUChar("last_kb", (uint8_t)idx);
  LOG_INF("BLE", "Stored new paired keyboard slot %d: %s (%s)", idx, name.c_str(), address.c_str());
  writeBleBackup();
}

bool getStoredDevice(std::string& address, std::string& name) {
  int idx = getLastUsedKeyboardIndex();
  if (idx < 0) return false;
  uint8_t addrType;
  return getPairedKeyboard(idx, address, name, addrType);
}

bool isDeviceScanning() { return isScanning; }

uint32_t getScanAgeMs() { return isScanning ? (millis() - scanStartMs) : 0; }

void refreshScanNow() {
  stopDeviceScan();
  discoveredDevices.clear();
  NimBLEDevice::getScan()->clearResults();
  startDeviceScan();
}

void clearAllBluetoothBonds() {
  NimBLEDevice::deleteAllBonds();
  clearStoredDevice();
  LOG_INF("BLE", "Deleted all bonds + cleared stored device");
}

void clearStoredDevice() {
  int count = nvs_loadCount();
  for (int i = 0; i < count; i++) nvs_clearSlot(i);
  prefs.putUChar("kb_count", 0);
  prefs.remove("last_kb");
  NimBLEDevice::deleteAllBonds();
  keyboardAddress = "";
  reconnectKeyboardIndex = 0;
  LOG_INF("BLE", "Cleared all paired keyboards and bonds");
  writeBleBackup();
}

#pragma once

// BLE HID keyboard host, ported from MicroSlate
// (github.com/Josh-writes/microslate-firmware, src/ble_keyboard.cpp) — see
// NOTICE.md. Picked over SUMI's BLE implementation after a side-by-side code
// review: MicroSlate diffs the full 6-key HID report both directions (real
// press AND release per key), explicitly negotiates Report Protocol mode,
// and carries several crash workarounds verified against real keyboards
// (Keychron, Logitech). SUMI's was tuned for a page-turner remote instead —
// not the target device here. Phase 1 of the port: the connection engine
// only (scan/pair/connect/reconnect/multi-keyboard storage) plus a minimal
// key event queue, wired into main.cpp for boot init + loop polling, with
// no UI yet. UI (Settings > Bluetooth, Settings > Paired Keyboards) and
// real input routing come next.

#include <cstdint>
#include <string>

// --- BLE connection state ---
enum class BLEState : uint8_t { DISCONNECTED, SCANNING, CONNECTING, CONNECTED };

// --- Raw key event, before any layout/dead-key decoding ---
// keyCode is a USB HID keyboard usage code (e.g. 0x04 = 'a'), not ASCII.
// Decoding (including dead keys) happens downstream, not in this module —
// same separation MicroSlate uses, kept deliberately since dead-key support
// is a stated requirement for this project.
struct BleKeyEvent {
  uint8_t keyCode;
  uint8_t modifiers;
  bool pressed;
};

// Device info structure for discovered devices
struct BleDeviceInfo {
  std::string address;
  std::string name;
  int rssi;
  uint8_t addressType;  // BLE address type (public/random)
  uint32_t lastSeenMs;  // from millis()
};

// Max keyboards that can be stored
static constexpr int MAX_PAIRED_KEYBOARDS = 4;

// --- Lifecycle ---
// Lazily initializes NimBLE. Idempotent — safe to call every time the Writer
// is entered. NimBLE's runtime heap footprint is large enough to starve the
// EPUB reader if left running full-time, so this is NOT called at boot.
void bleSetup();
// Tears down NimBLE and reclaims its heap. Safe to call even if bleSetup()
// was never called, or repeatedly. If a connect attempt is in flight, the
// actual teardown is deferred until it finishes (see bleLoop()).
void bleShutdown();
// No-op when BLE hasn't been initialized (bleSetup() not called, or already
// shut down) — safe to call unconditionally from the main loop.
void bleLoop();

// --- Connection state ---
bool isKeyboardConnected();
BLEState getConnectionState();
std::string getCurrentDeviceAddress();
void disconnectCurrentDevice();
void cancelPendingConnection();

// Global flag to control auto-reconnect behavior
extern bool autoReconnectEnabled;

// --- Scanning + connecting to a new device ---
void startDeviceScan();
void stopDeviceScan();
bool isDeviceScanning();
uint32_t getScanAgeMs();
void refreshScanNow();
int getDiscoveredDeviceCount();
BleDeviceInfo* getDiscoveredDevices();
void connectToDevice(int deviceIndex);

// --- Multi-keyboard storage (up to MAX_PAIRED_KEYBOARDS) ---
int getPairedKeyboardCount();
bool getPairedKeyboard(int index, std::string& addr, std::string& name, uint8_t& addrType);
int getLastUsedKeyboardIndex();  // -1 if none stored
bool removePairedKeyboard(int index);
void connectToPairedKeyboard(int index);
void storePairedDevice(const std::string& address, const std::string& name);
bool getStoredDevice(std::string& address, std::string& name);
void clearStoredDevice();
void clearAllBluetoothBonds();

// Passkey for UI display during numeric-comparison pairing (0 if none pending)
uint32_t getCurrentPasskey();

// --- Key event queue (phase 1: nothing consumes this yet but bleLoop()
// itself; a real consumer — menu nav, reader, writer — lands with the
// input-routing phase of this project) ---
bool popBleKeyEvent(BleKeyEvent& out);
